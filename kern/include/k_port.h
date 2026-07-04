#ifndef _K_PORT_H_
#define _K_PORT_H_

#include "k_types.h"

// Include necessary headers here

// Define any constants or macros here

// Declare any data types here

// Define the stack element size type
typedef uint32_t kstack_t;

// context save/restore related
typedef struct task_stack_frame {
    // extra registers saved on task stack
    kstack_t control;     // cortex control register
    kstack_t exc_return;  //=>r14

    kstack_t R4;  // Remaining registers saved on task stack (R4~R11) by software
    kstack_t R5;
    kstack_t R6;
    kstack_t R7;
    kstack_t R8;
    kstack_t R9;
    kstack_t R10;
    kstack_t R11;

    kstack_t R0;  // hardware saved R0~R3,R12,LR,PC,xPSR
    kstack_t R1;
    kstack_t R2;
    kstack_t R3;
    kstack_t R12;
    kstack_t LR;    // LR=r14
    kstack_t PC;    // PC=r15
    kstack_t xPSR;  // Cortex state register
} task_stack_frame_t;

// Declare function prototypes here

// Initializes the CPU related stuff.
extern void cpu_kern_init(void);

// Requests cpu to do a context switch.
extern void cpu_sched_request(void);

/**
 * @brief Prepares the task stack frame for a new task.
 *
 * @param stack_top The top of the task stack.
 * @param entry The entry point function of the task.
 * @param arg The argument to be passed to the task.
 * @return A pointer to the task stack frame.
 */
extern struct task_stack_frame *cpu_task_stack_prepair(kstack_t *stack_top, function_ptr_t entry, void *arg);

// extern void cpu_idle(void);

// irq enable/disable related

// Enables CPU interrupts.
extern void cpu_irq_enable(void);
// Disables CPU interrupts.
extern void cpu_irq_disable(void);
// Saves the current interrupt state and disables interrupts.
extern int cpu_irq_save(void);
// Restores the interrupt state.
extern void cpu_irq_restore(int state);
// Checks if interrupts are currently disabled.
extern int cpu_irq_disabled(void);
// Checks if the code is currently executing in an interrupt context.
extern int cpu_in_irq(void);

#endif  // _K_PORT_H_