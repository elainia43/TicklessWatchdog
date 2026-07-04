#include <stdint.h>

#include "k_kern.h"
// #include "core_cm4.h"
// #include "stm32f1xx_hal.h"
#include "stm32f407xx.h"

#define ALIGN_DOWN(x, align) (((unsigned int)(x)) & ~((align)-1))

// Enable IRQs
void cpu_irq_enable(void) {
    asm volatile("CPSIE I" : : : "memory");
}

// Disable IRQs
void cpu_irq_disable(void) {
    asm volatile("CPSID I" : : : "memory");
}

// Save IRQ state and disable IRQs
int cpu_irq_save(void) {
    int primask;
    asm volatile(
        "MRS %0, PRIMASK\n\t"
        "CPSID I\n\t"
        : "=r"(primask)
        :
        : "memory");

    K_ASSERT(kernel_state.irq_lock_level >= 0);
    // K_ASSERT( && kernel_state.irq_lock_level < 20);
    kernel_state.irq_lock_level++;

    return primask;
}

// Restore IRQ state
void cpu_irq_restore(int state) {
    kernel_state.irq_lock_level--;
    K_ASSERT(kernel_state.irq_lock_level >= 0);

    asm volatile(
        "MSR PRIMASK, %0\n\t"
        :
        : "r"(state)
        : "memory");
}

// Check if IRQs are disabled
int cpu_irq_disabled(void) {
    int primask;
    asm volatile("MRS %0, PRIMASK\n\t" : "=r"(primask));
    int disabled = primask & 1;

    K_ASSERT(disabled || kernel_state.irq_lock_level == 0);

    return disabled;
}

// Check if IRQs are enabled
int cpu_irq_enabled(void) {
    return !cpu_irq_disabled();
}

// Check if in IRQ context
int cpu_in_irq(void) {
    int ipsr;
    asm volatile("MRS %0, IPSR\n\t" : "=r"(ipsr));
    return (ipsr & 0xFF) != 0;  // Check if in handler mode
}

static void task_return(void) {
    task_exit();
    K_ASSERT(0);
}

// fill task's init stack
task_stack_frame_t *cpu_task_stack_prepair(kstack_t *stack_top, function_ptr_t entry, void *arg) {
    task_stack_frame_t *frame = (task_stack_frame_t *)ALIGN_DOWN(stack_top, 8);  // align to double word
    frame--;                                                                     // move to the bottom of stack

    // context saved & restore by hardware exception
    frame->xPSR = 0x01000000u;                   // initial xPSR: EPSR.T = 1, Thumb mode
    frame->PC = (kstack_t)ALIGN_DOWN(entry, 2);  // Entry Point. UNPREDICTABLE if the new PC not halfword aligned
    frame->LR = (kstack_t)task_return;           //(unsigned int)-1;		//invalid caller
    frame->R12 = 0x12121212u;
    frame->R3 = 0x03030303u;
    frame->R2 = 0x02020202u;
    frame->R1 = 0x01010101u;
    frame->R0 = (kstack_t)arg;

    // context saved & restore by software (pendsv handler)
    frame->R11 = 0x11111111u;
    frame->R10 = 0x10101010u;
    frame->R9 = 0x09090909u;
    frame->R8 = 0x08080808u;
    frame->R7 = 0x07070707u;
    frame->R6 = 0x06060606u;
    frame->R5 = 0x05050505u;
    frame->R4 = 0x04040404u;

    // frame->exc_return = 0xFFFFFFFDu;  // initial EXC_RETURN begin state: Thread mode +  non-floating-point + PSP
    frame->exc_return = 0xFFFFFFF9u;  // initial EXC_RETURN begin state: Thread mode +  non-floating-point + MSP
    frame->control = 0x2;             // 0x3;		// initial CONTROL : privileged, PSP, no FP

    return frame;
}

#define NVIC_INT_CTRL (*((volatile uint32_t *)0xE000ED04))  // Interrupt Control and State Register
#define NVIC_PENDSVSET (1 << 28)                            // Value to trigger PendSV exception

// #define PendSV_IRQ (-2)

// Trigger PendSV exception
void trigger_PendSV(void) {
    NVIC_INT_CTRL |= NVIC_PENDSVSET;
}

void cpu_sched_request(void) {
    // trigger PendSV exception
    trigger_PendSV();

    // enable interrupts so PendSV can run if interrupts are disabled
    if (cpu_irq_disabled()) {
        cpu_irq_enable();
        // should jump to PendSV here
        cpu_irq_disable();  // disable interrupts again
    }
}

// Initialize CPU related stuff
void cpu_kern_init(void) {
    // Set priority of PendSV to lowest (numerically highest) priority
    NVIC_SetPriority(PendSV_IRQn, 15); /* set Priority for Systick Interrupt */
    // disable systick interrupt
    // SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
}

// PendSV handler to switch tasks context
__attribute__((naked)) void PendSV_Handler(void) {
    __asm volatile(
        // SAVE CPU CONTEXT
        " CPSID   I \n"   // disable interrupt
        " tst lr, #4 \n"  // which stack we were using?
        " ite eq \n"
        " mrseq r0, msp \n"
        " mrsne r0, psp \n"

        // 1.save the current task context from CPU

        // hardware saved xpsr pc lr r12 r3-r0
        " stmdb r0!, {r4-r11} \n"  // software save r11-r4

        " mrs r2, control \n"
        " mov r3, lr \n"
        " stmdb r0!, {r2 - r3} \n"  // software save lr & control

        " tst lr, #4 \n"  // are we on the same stack? ajust sp if yes
        " it eq \n"
        " moveq sp, r0 \n"

// 2.switch to the new task context, stack is fine now, let's call into c
#ifdef KCONF_COROUTINE_ENABLED
        " bl sched_switch_coroutine_stack \n"  // call coroutine switch
#else
        " bl sched_switch_task_stack \n"  // call task switch
#endif

        // 3.restore the new task context to CPU
        " ldmia r0!, {r2, r3, r4-r11} \n"  // new stack is in R0, r2=new control, r3=new exc_return
        " mov lr, r3 \n"
        " msr control, r2 \n"
        " ISB \n"

        " tst lr, #4 \n"
        " ite eq \n"
        " msreq msp, r0 \n"  // return stack = MSP or
        " msrne psp, r0 \n"  // return stack = PSP

        " CPSIE   I \n"  // enable interrupt

        " bx LR \n"  // return to new task from trap. PC & PSR popped out by bx lr
    );
}
