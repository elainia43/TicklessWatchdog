// #define LOCAL_DEBUG

#include "k_kern.h"

// you can override weak functions in your application
#ifndef __WEAK
#define __WEAK __attribute__((weak))
#endif

// called when an assertion fails
__WEAK void assert_fail(const char *assertion, const char *file, unsigned int line, const char *function) {
    K_PRINTF("Failed Assertion '%s' at %s:%u in function %s\r\n", assertion, file, line, function);
    cpu_irq_disable();
    while (1)
        ;
}

#ifdef KCONF_KERN_STATS_ENABLED
// kernel statistics data
kern_stats_t kernel_stats;
#endif

// Fill the end of the task stack (redzone) with a known value to detect stack overflow
#define REDZONE_FILL 0xDEADBEEF
#define REDZONE_SIZE 4

static void redzone_fill(kstack_t *stack, size_t size) {
    for (size_t i = 0; i < size; i++) {
        stack[i] = REDZONE_FILL;
    }
}

static void redzone_check(kstack_t *stack, size_t size) {
    for (size_t i = 0; i < size; i++) {
        if (stack[i] != REDZONE_FILL) {
            PANIC("stack overflow detected at %p", &stack[i]);
            break;
        }
    }
}

// task state string
const char *task_state_str(task_state_t state) {
    switch (state) {
        case TASK_DEAD:
            return "DEAD";
        case TASK_READY:
            return "READY";
        case TASK_RUNNING:
            return "RUNNING";
        case TASK_BLOCKED:
            return "BLOCKED";
        default:
            return "UNKNOWN";
    }
}

__WEAK void task_create_hook(ktask_t *task) {
    redzone_fill(task->stack_base, REDZONE_SIZE);
    K_TRACE("task '%s' created", task->name);
}

__WEAK void task_delete_hook(ktask_t *task) {
    K_TRACE("task '%s' deleted", task->name);
}

__WEAK void task_state_change_hook(ktask_t *task) {
    K_TRACE("'%s': %s -> %s", task->name, task_state_str(task->old_state), task_state_str(task->state));
}

__WEAK void task_switch_hook(ktask_t *from, ktask_t *to) {
    if (from != NULL && to != NULL) {
        K_TRACE("%s (pri %d sp %p) -> %s (pri %d sp %p), reason: %s",
                from->name, from->prio, from->context, to->name, to->prio, to->context, from->yield_reason);
        // K_ASSERT(from->magic == MAGIC_TASK);
        K_ASSERT(KOBJ_MAGIC_CHECK(to, MAGIC_TASK));
        // check task stack overflow
        redzone_check(from->stack_base, REDZONE_SIZE);
        redzone_check(to->stack_base, REDZONE_SIZE);
    }
}

__WEAK void task_idle_hook(void) {
    // K_TRACE("in idle task");
}

// called in pre-schedule/non-task context
__WEAK void kern_init_hook(void) {
    K_TRACE("kernel init...");
    // TODO: add kernel initialization code here
}

// Called in boot task context
__WEAK void usr_init_hook(void) {
    K_TRACE("user init");
    // TODO: add user initialization code here
}

void kern_start(void) {
    K_PRINTF("\nkernel start...\n");

    cpu_irq_disable();  // disable irq
    cpu_kern_init();    // initialize cpu related stuff

    // init rtos modules
    k_sched_init();  // initialize scheduler library
    k_task_init();   // initialize task library
    k_time_init();   // initialize time library

    kern_init_hook();  // call kernel start hook

    // start the scheduler
    sched_start();

    while (1)
        ;  // should never reach here
}
