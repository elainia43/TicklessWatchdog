#ifndef _K_KERN_H_
#define _K_KERN_H_

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "k_config.h"
#include "k_debug.h"
#include "k_port.h"
#include "k_queue.h"
#include "k_types.h"

/*
********************************************************************************
*                            Macros
********************************************************************************
*/

// some macros for bit operations
#define BIT_SET(var, bit) ((var) |= (1 << (bit)))
#define BIT_CLEAR(var, bit) ((var) &= ~(1 << (bit)))
#define BIT_CHECK(var, bit) ((var) & (1 << (bit)))

#define FLAG_SET(var, flag) ((var) |= (flag))
#define FLAG_CLEAR(var, flag) ((var) &= ~(flag))
#define FLAG_CHECK(var, flag) ((var) & (flag))

// some macros for bitmap operations

#define BITMAP_DEFINE(name, nbits) \
    unsigned long name[BITMAP_SIZE(nbits)]

#define BITMAP_SIZE(nbits) (((nbits) + sizeof(long) - 1) / sizeof(long))
#define BITMAP_OFFSET(bit) ((bit) / sizeof(long))
#define BITMAP_SHIFT(bit) ((bit) % sizeof(long))
#define BITMAP_MASK(bit) (0x1ul << BITMAP_SHIFT(bit))

static inline void bitmap_set_bit(unsigned long *bitmap, unsigned int bit) {
    bitmap[BITMAP_OFFSET(bit)] |= BITMAP_MASK(bit);
}

static inline void bitmap_clear_bit(unsigned long *bitmap, unsigned int bit) {
    bitmap[BITMAP_OFFSET(bit)] &= ~BITMAP_MASK(bit);
}

static inline unsigned int bitmap_test_bit(const unsigned long *bitmap, unsigned int bit) {
    return 0x1ul & (bitmap[BITMAP_OFFSET(bit)] >> BITMAP_SHIFT(bit));
}

/*
********************************************************************************
*                           Kernel Objects
********************************************************************************
*/
typedef struct kobject {
    unsigned int magic;  // magic number for validation and type identification (e.g., task, mutex, semaphore)
    unsigned int flag;   // object flag (e.g., pool allocated)
} kobject_t;

// convert a specific object type's struct ptr to an kobject_t ptr
#define KOBJ_RESOLVE(kobj) ((kobject_t *)(kobj))

// initialize kobject_t
#define KOBJ_INITIALIZER(kobj, _magic, _flag) \
    {                                         \
        .magic = (_magic),                    \
        .flag = (_flag),                      \
    }

#define KOBJ_INIT(kobj, _magic, _flag)          \
    do {                                        \
        (KOBJ_RESOLVE(kobj))->magic = (_magic); \
        (KOBJ_RESOLVE(kobj))->flag = (_flag);   \
    } while (0)

// macros for manipulating magic number of kobject_t.
#define KOBJ_MAGIC(kobj) ((KOBJ_RESOLVE(kobj))->magic)
#define KOBJ_MAGIC_SET(kobj, _magic) ((KOBJ_RESOLVE(kobj))->magic = (_magic))
#define KOBJ_MAGIC_CLEAR(kobj) ((KOBJ_RESOLVE(kobj))->magic = 0)
#define KOBJ_MAGIC_CHECK(kobj, _magic) ((kobj) && (KOBJ_RESOLVE(kobj))->magic == (_magic))

// macros for manipulating flags of kobject_t.
#define KOBJ_FLAG_SET(kobj, _flag) FLAG_SET((KOBJ_RESOLVE(kobj))->flag, _flag)
#define KOBJ_FLAG_CLEAR(kobj, _flag) FLAG_CLEAR((KOBJ_RESOLVE(kobj))->flag, _flag)
#define KOBJ_FLAG_CHECK(kobj, _flag) FLAG_CHECK((KOBJ_RESOLVE(kobj))->flag, _flag)

// offset for object type specific flags in kobject_t flag field
#define KOBJ_FLAG_TYPE_OFFSET 8

// Magic number for kernel object validation
#define MAKE_MAGIC(a, b, c, d) (((a) << 24) | ((b) << 16) | ((c) << 8) | (d))

// kobject_t flag definitions
#define KOBJ_FLAG_POOL_ALLOC (1 << 0)  // Object is allocated from a pool
#define KOBJ_FLAG_HEAP_ALLOC (1 << 1)  // Object is allocated from a heap

/*
********************************************************************************
*                           Critical Section Management
********************************************************************************
*/

/**
 * @brief Kernel and task critical section macros
 *
 * Macros for implementing kernel and task critical sections.
 * These macros provide a convenient way to disable and
 * enable interrupts or lock and unlock the scheduler, allowing for atomic
 * operations and preventing race conditions.
 */

// implement kernel critical section by disabling/enabling interrupt

/**
 * @def KERNEL_CRITICAL_ENTER()
 * @brief Enters the kernel critical section by disabling interrupts
 *
 * This macro disables interrupts by saving the current interrupt state and
 * storing it in the "__irqsr__" variable.
 */
#define KERNEL_CRITICAL_ENTER() int __irqsr__ = cpu_irq_save();

/**
 * @def KERNEL_CRITICAL_EXIT()
 * @brief Exits the kernel critical section by enabling interrupts
 *
 * This macro restores the interrupt state saved in the "__irqsr__" variable,
 * effectively enabling interrupts.
 */
#define KERNEL_CRITICAL_EXIT() cpu_irq_restore(__irqsr__);

/**
 * @def KERNEL_IN_CRITICAL()
 * @brief Checks if currently in the kernel critical section
 * @return 1 if in the kernel critical section, 0 otherwise
 *
 * This macro checks if interrupts are currently disabled, indicating that the
 * code is executing within the kernel critical section.
 */
#define KERNEL_IN_CRITICAL() (cpu_irq_disabled())

/**
 * @def WITHIN_CRITICAL()
 * @brief Executes code within an interrupt disabled critical section
 *
 * This macro provides a convenient way to execute code within a critical
 * section where interrupts are disabled. The code block should not contain
 * any return or break statements.
 * DO NOT call return or break in the code block!
 */
#define WITHIN_CRITICAL() for (               \
    int __irqsr = cpu_irq_save(), __exit = 0; \
    !__exit;                                  \
    cpu_irq_restore(__irqsr), __exit = 1)

// implement task critical section by disabling/enabling scheduler

/**
 * @def TASK_CRITICAL_ENTER()
 * @brief Enters the task critical section by locking the scheduler
 *
 * This macro locks the scheduler, preventing task switching and ensuring that
 * the current task can execute without interruption.
 */
#define TASK_CRITICAL_ENTER() sched_lock()

/**
 * @def TASK_CRITICAL_EXIT()
 * @brief Exits the task critical section by unlocking the scheduler
 *
 * This macro unlocks the scheduler, allowing task switching to resume.
 */
#define TASK_CRITICAL_EXIT() sched_unlock()

/**
 * @def TASK_IN_CRITICAL()
 * @brief Checks if currently in the task critical section
 * @return 1 if in the task critical section, 0 otherwise
 *
 * This macro checks if the scheduler is currently locked, indicating that the
 * code is executing within the task critical section.
 */
#define TASK_IN_CRITICAL() (sched_lock_level() > 0)

/*
********************************************************************************
*                            Schedule Management
********************************************************************************
*/
/**
 * This Sectin contains the declarations of functions related to the kernel scheduler.
 * The kernel scheduler is responsible for managing task scheduling and prioritization.
 */

// Task priority levels
#define PRIO_LOWEST 0                                   // Lowest priority level
#define PRIO_HIGHEST KCONF_PRIO_HIGHEST                 // Highest priority level
#define PRIO_NORMAL ((PRIO_LOWEST + PRIO_HIGHEST) / 2)  // Normal priority level

// Compare two priorities
#define PRIO_HIGHER(pri_a, pri_b) ((pri_a) > (pri_b))  // pri_a is higher than pri_b
#define PRIO_LOWER(pri_a, pri_b) ((pri_a) < (pri_b))   // pri_a is lower than pri_b

// Enumeration representing the possible states of a task.
typedef enum {
    TASK_DEAD = 0, /**< Task is dead. */
    TASK_READY,    /**< Task is ready to run. */
    TASK_RUNNING,  /**< Task is currently running. */
    TASK_BLOCKED   /**< Task is blocked and waiting for an event. */
} task_state_t;

// task queue type
typedef TAILQ_HEAD(_taskq_t, ktask) taskq_t;
// node type in a task queue
typedef TAILQ_ENTRY(ktask) taskq_node_t;

/**
 * @brief Structure representing a scheduling queue.
 *
 * This structure contains information about a scheduling queue, including the task queue,
 * the highest priority, and the count of tasks in the queue.
 */
typedef struct sched_queue {
    taskq_t taskq;    /**< Task queue */
    int prio_highest; /**< Highest priority */
    int count;        /**< Number of tasks in the queue */
} sched_queue_t;

#define SCHED_QUEUE_INITIALIZER(schedq)                  \
    {                                                    \
        .taskq = TAILQ_HEAD_INITIALIZER((schedq).taskq), \
        .prio_highest = PRIO_LOWEST,                     \
        .count = 0,                                      \
    }

typedef sched_queue_t ready_queue_t;  // ready queue type
typedef sched_queue_t wait_queue_t;   // wait queue type

/**
 * @brief Initializes the ready queue.
 */
void ready_queue_init(void);

/**
 * @brief Inserts a task at the tail of the ready queue.
 *
 * @param task The task to be inserted.
 */
void ready_queue_insert_tail(ktask_t *task);

/**
 * @brief Removes a task from the ready queue.
 *
 * @param task The task to be removed.
 */
void ready_queue_remove(ktask_t *task);

/**
 * @brief Retrieves the highest priority task from the ready queue.
 *
 * @return The highest priority task.
 */
ktask_t *ready_queue_get_highest(void);

/**
 * @brief Initializes a wait queue.
 *
 * @param wait_q The wait queue to be initialized.
 */
void wait_queue_init(wait_queue_t *wait_q);

/**
 * @brief Changes the priority of a task.
 *
 * @param task The task whose priority needs to be changed.
 * @param new_prio The new priority value.
 *
 * @return The status of the operation.
 */
kstatus_t sched_change_priority(ktask_t *task, int new_prio);

/**
 * @brief Changes the state of a task.
 *
 * @param t The task whose state needs to be changed.
 * @param newstate The new state value.
 *
 * @return The task with the new state.
 */
ktask_t *sched_change_state_to(ktask_t *t, task_state_t newstate);

/**
 * @brief Yields the CPU to another task.
 *
 * @param reason The reason for yielding.
 */
void sched_yield(const char *reason);

/**
 * @brief Preempts a task.
 *
 * @param task The task to be preempted.
 * @param reason The reason for preemption.
 */
void sched_preempt(ktask_t *task, const char *reason);

/**
 * @brief Locks the scheduler.
 *
 * @return The lock level.
 */
int sched_lock(void);

/**
 * @brief Unlocks the scheduler.
 *
 * @return The lock level.
 */
int sched_unlock(void);

/**
 * @brief Retrieves the current scheduler lock level.
 *
 * @return The current lock level.
 */
int sched_lock_level(void);

/**
 * @brief Enters the scheduler from an IRQ context.
 */
void sched_irq_enter(void);

/**
 * @brief Exits the scheduler from an IRQ context.
 */
void sched_irq_exit(void);

/**
 * @brief Calls an ISR (Interrupt Service Routine) within the scheduler context.
 *
 * @param isr The ISR function to be called.
 * @param arg The argument to be passed to the ISR.
 *
 * @return The status of the ISR call.
 */
int sched_call_isr(isr_func_t isr, void *arg);

/**
 * @brief Waits on a wait queue.
 *
 * @param wait_q The wait queue to wait on.
 * @param reason The reason for waiting.
 *
 * @return The status of the wait operation.
 */
kstatus_t sched_wait_on(wait_queue_t *wait_q, const char *reason);

/**
 * @brief Waits on a wait queue with a timeout.
 *
 * @param wait_q The wait queue to wait on.
 * @param timeout The timeout(ticks) value.
 * @param reason The reason for waiting.
 *
 * @return The status of the wait operation.
 */
kstatus_t sched_wait_timeout(wait_queue_t *wait_q, kticks_t timeout, kticks_t *remain, const char *reason);

/**
 * @brief Macro that waits for a condition to be met or a timeout to occur or error hanpped.
 *
 * This macro allows a task to wait on a condition or timeout in a scheduler wait queue.
 * It repeatedly checks the condition expression until it evaluates to K_OK or the timeout expires.
 * The macro is designed to be used within a critical section to ensure thread safety.
 *
 * @param wait_queue The scheduler wait queue to wait on.
 * @param timeout The maximum time to wait for the condition to be met, in ticks.
 * @param cond_expr The condition expression to be evaluated.
 * @return The status of the wait operation. Possible values are:
 *         - K_OK: The condition was met.
 *         - K_TIMEOUT: The timeout occurred.
 *         - K_WAIT_DELETED: The wait object was deleted.
 *         - K_WAIT_ABORTED: The wait operation was aborted.
 *         - K_WAIT_BLOCKING: The wait operation is blocking for result.
 *         - K_ERROR: An error occurred.
 */
#define SCHED_WAIT_CONDITION_OR_TIMEOUT(wait_queue, timeout, cond_expr)                             \
    ({                                                                                              \
        KERNEL_CRITICAL_ENTER();                                                                    \
        kstatus_t _wait_status = K_OK;                                                              \
        kticks_t _wait_timeout = timeout;                                                           \
        do {                                                                                        \
            if ((cond_expr) == K_OK) {                                                              \
                break;                                                                              \
            }                                                                                       \
            _wait_status = sched_wait_timeout(wait_queue, _wait_timeout, &_wait_timeout, __func__); \
        } while (_wait_status == K_OK);                                                             \
        KERNEL_CRITICAL_EXIT();                                                                     \
        _wait_status;                                                                               \
    })

/**
 * @brief Waits for an event.
 *
 * @return The status of the wait operation.
 */
kstatus_t sched_wait(void);

/**
 * @brief Wakes up a task waiting on a wait queue.
 *
 * @param wait_q The wait queue to wake up the task from.
 * @param wait_result The result of the wait operation.
 *
 * @return The status of the wakeup operation.
 */
kstatus_t sched_wakeup(wait_queue_t *wait_q, kstatus_t wait_result);

/**
 * @brief Wakes up all tasks waiting on a wait queue.
 *
 * @param wait_q The wait queue to wake up the tasks from.
 * @param wait_result The result of the wait operation.
 *
 * @return The status of the wakeup operation.
 */
kstatus_t sched_wakeup_all(wait_queue_t *wait_q, kstatus_t wait_result);

/**
 * @brief Wakes up a specific task.
 *
 * @param task The task to be woken up.
 * @param wait_result The result of the wait operation.
 *
 * @return The status of the wakeup operation.
 */
kstatus_t sched_wakeup_task(ktask_t *task, kstatus_t wait_result);

/**
 * @brief Starts the scheduler.
 */
void sched_start(void);

/**
 * @brief Initializes the kernel scheduler.
 */
void k_sched_init(void);

/*
********************************************************************************
*                            Task Management
********************************************************************************
*/
#define MAGIC_TASK MAKE_MAGIC('T', 'A', 'S', 'K')

// task control block
struct ktask {
    kobject_t kobj;                    // kernel object
    struct task_stack_frame *context;  // pointer to current top of stack

    // init config attributes
    char name[32];         // task name
    kstack_t *stack_base;  // stack base address
    kstack_t *stack_end;   // stack end address
    task_func_t entry;     // user function entry
    void *arg;             // user function argument
    int prio;              // task priority

    // run control data
    task_state_t state;        // my current state
    taskq_node_t stateq_node;  // node on queue: ready or waiting
    wait_queue_t *wait_on;     // which queue I'am waiting on
    kstatus_t wait_result;     // wait result
    wait_queue_t join_queue;   // my join queue, for who wait on me to exit

    // debug data
    taskq_node_t task_node;    // link to all task list
    task_state_t old_state;    // my old state
    const char *yield_reason;  // why the task yield to another
    const char *wait_reason;   // why the task is waiting

    // task statistics
    unsigned int stack_switch_count;
    unsigned int state_change_count;
};

/**
 * @brief Creates a new task. Task's TCB and stack dynamically allocated.
 *
 * This function creates a new task with the specified name, entry point function,
 * argument, priority, and stack size.
 *
 * @param name The name of the task.
 * @param entry The entry point function of the task.
 * @param arg The argument to be passed to the entry point function.
 * @param prio The priority of the task.
 * @param stack_bytes The size of the task's stack in bytes.
 * @return Pointer to the newly created task.
 */
ktask_t *task_new(const char *name, task_func_t entry, void *arg, int prio, size_t stack_bytes);

/**
 * @brief Creates a new task.
 *
 * This function creates a new task with the specified parameters.
 *
 * @param name Name of the task.
 * @param entry Function pointer to the task's entry point.
 * @param arg Pointer to the argument to be passed to the task's entry point.
 * @param prio Priority of the task.
 * @param stack_bytes Size of the task's stack in bytes.
 * @param stack_base Pointer to the base of the task's stack.
 * @param task Pointer to the task structure to be initialized.
 * @param flag Flags for the task creation operation.
 * @return Status of the task creation operation.
 */
kstatus_t task_create(const char *name, task_func_t entry, void *arg, int prio, size_t stack_bytes, kstack_t *stack_base, ktask_t *task, unsigned int flag);

/**
 * @brief Deletes a task.
 *
 * This function deletes the specified task.
 *
 * @param task Pointer to the task to be deleted.
 * @return Status of the task deletion operation.
 */
kstatus_t task_delete(ktask_t *task);

/**
 * @brief Exits the current task.
 *
 * This function causes the current task to exit.
 *
 * @return Status of the task exit operation.
 */
kstatus_t task_exit(void);

/**
 * @brief Yields the CPU to another task.
 *
 * This function yields the CPU to another task, allowing it to run.
 *
 * @return Status of the task yield operation.
 */
kstatus_t task_yield(void);

/**
 * @brief Returns a pointer to the current task.
 *
 * This function returns a pointer to the current task.
 *
 * @return Pointer to the current task.
 */
ktask_t *task_self(void);

/**
 * @brief Returns the name of a task.
 *
 * This function returns the name of the specified task.
 *
 * @param task Pointer to the task.
 * @return Name of the task.
 */
const char *task_name(ktask_t *task);

/**
 * @brief Returns the priority of a task.
 *
 * This function returns the priority of the specified task.
 *
 * @param task Pointer to the task.
 * @return Priority of the task.
 */
int task_priority(ktask_t *task);

/**
 * @brief Sets the priority of a task.
 *
 * This function sets the priority of the specified task.
 *
 * @param task Pointer to the task.
 * @param new_prio New priority for the task.
 * @return Status of the task priority set operation.
 */
kstatus_t task_set_priority(ktask_t *task, int new_prio);

/**
 * @brief Joins a task.
 *
 * This function joins the specified task, blocking the current task until the joined task completes.
 *
 * @param task Pointer to the task to be joined.
 * @return Status of the task join operation.
 */
kstatus_t task_join(ktask_t *task);

/**
 * @brief Suspends the current task.
 *
 * This function suspends the current task, allowing other tasks to run.
 *
 * @return Status of the task suspend operation.
 */
kstatus_t task_suspend(void);

/**
 * @brief Resumes a task.
 *
 * This function resumes the specified task.
 *
 * @param task Pointer to the task to be resumed.
 * @return Status of the task resume operation.
 */
kstatus_t task_resume(ktask_t *task);

/**
 * @brief Sleeps for a specified number of ticks.
 *
 * This function causes the current task to sleep for the specified number of ticks.
 *
 * @param ticks Number of ticks to sleep.
 * @return Status of the task sleep operation.
 */
kstatus_t task_sleep(kticks_t ticks);

/**
 * @brief Sleeps for a specified number of milliseconds.
 *
 * This function causes the current task to sleep for the specified number of milliseconds.
 *
 * @param ms Number of milliseconds to sleep.
 * @return Status of the task sleep operation.
 */
kstatus_t task_msleep(unsigned long ms);

/**
 * @brief Initializes the task subsystem.
 *
 * This function initializes the task subsystem.
 */
void k_task_init(void);

/*
********************************************************************************
*                            Time & Timer Management
********************************************************************************
*/

#define MAGIC_TIMER MAKE_MAGIC('T', 'I', 'M', 'R')

#define TIME_SUB(a, b) ((long)((a) - (b)))   // a - b
#define TIME_GT(a, b) (TIME_SUB(a, b) > 0)   //(b,a), a after b
#define TIME_LT(a, b) (TIME_SUB(a, b) < 0)   //(a,b), a before b
#define TIME_GE(a, b) (TIME_SUB(a, b) >= 0)  //[b,a), a same as or after b
#define TIME_LE(a, b) (TIME_SUB(a, b) <= 0)  //(a, b], a same as or before b
#define TIME_EQ(a, b) (TIME_SUB(a, b) == 0)  // a equal to b

#define TIME_NO_WAIT (kticks_t)(-1)      // no wait
#define TIME_FOREVER_WAIT (kticks_t)(0)  // forever wait

/**
 * @brief Structure representing a kernel timer.
 */
typedef struct ktimer_t {
    kobject_t kobj; /**< Kernel object. */

    TAILQ_ENTRY(ktimer_t)
    timer_node; /**< Linked list node for timer queue. */

    kticks_t expire_time; /**< Time at which the timer expires. */
    kticks_t period_time; /**< Periodic time interval for the timer. */

    timer_func_t callback; /**< Callback function to be executed when the timer expires. */
    void *arg;             /**< Argument to be passed to the callback function. */
} ktimer_t;

/**
 * @brief Structure representing a timer queue.
 */
typedef struct {
    TAILQ_HEAD(_timerq_t, ktimer_t)
    timerq;    /**< Linked list head for the timer queue. */
    int count; /**< Number of timers in the queue. */
} timer_queue_t;

#define TIMER_INITIALIZER(timer)                  \
    {                                             \
        .kobj = KOBJ_INITIALIZER(MAGIC_TIMER, 0), \
        .expire_time = 0,                         \
        .period_time = 0,                         \
        .callback = NULL,                         \
        .arg = NULL,                              \
        .timer_node = { NULL,                     \
                        NULL }                    \
    }

#define DEFINE_TIMER(timer) ktimer_t timer = TIMER_INITIALIZER(timer)

/**
 * @brief Creates a new timer.
 *
 * This function creates a new timer and returns a pointer to the newly created timer object.
 *
 * @return A pointer to the newly created timer object.
 */
ktimer_t *timer_new(void);

/**
 * @brief Initializes a timer.
 *
 * This function initializes the specified timer.
 *
 * @param timer Pointer to the timer structure.
 * @return The status of the timer initialization.
 */
kstatus_t timer_initialize(ktimer_t *timer);

/**
 * @brief Deletes a timer.
 *
 * This function deletes the specified timer.
 *
 * @param timer Pointer to the timer structure.
 * @return The status of the timer deletion.
 */
kstatus_t timer_delete(ktimer_t *timer);

/**
 * @brief Adds a timer with a specified delay and period.
 *
 * This function adds a timer with the specified delay and period.
 * When the timer expires, the specified callback function will be called with the provided argument.
 *
 * @param timer Pointer to the timer structure.
 * @param delay The delay before the timer expires, in ticks.
 * @param period The period at which the timer should repeat, in ticks.
 * @param callback The callback function to be called when the timer expires.
 * @param arg The argument to be passed to the callback function.
 * @return The status of the timer addition.
 */
kstatus_t timer_add(ktimer_t *timer, kticks_t delay, kticks_t period, timer_func_t callback, void *arg);

/**
 * @brief Adds a one-shot timer with a specified delay.
 *
 * This function adds a one-shot timer with the specified delay.
 * When the timer expires, the specified callback function will be called with the provided argument.
 *
 * @param timer Pointer to the timer structure.
 * @param delay The delay before the timer expires, in ticks.
 * @param callback The callback function to be called when the timer expires.
 * @param arg The argument to be passed to the callback function.
 * @return The status of the timer addition.
 */
kstatus_t timer_add_oneshot(ktimer_t *timer, kticks_t delay, timer_func_t callback, void *arg);

/**
 * @brief Adds a periodic timer with a specified period.
 *
 * This function adds a periodic timer with the specified period.
 * When the timer expires, the specified callback function will be called with the provided argument.
 *
 * @param timer Pointer to the timer structure.
 * @param period The period at which the timer should repeat, in ticks.
 * @param callback The callback function to be called when the timer expires.
 * @param arg The argument to be passed to the callback function.
 * @return The status of the timer addition.
 */
kstatus_t timer_add_periodic(ktimer_t *timer, kticks_t period, timer_func_t callback, void *arg);

/**
 * @brief Converts milliseconds to ticks.
 *
 * This function converts the specified number of milliseconds to ticks.
 *
 * @param ms The number of milliseconds to convert.
 * @return The equivalent number of ticks.
 */
kticks_t time_ms_to_ticks(unsigned long ms);

/**
 * @brief Converts ticks to milliseconds.
 *
 * This function converts the specified number of ticks to milliseconds.
 *
 * @param ticks The number of ticks to convert.
 * @return The equivalent number of milliseconds.
 */
unsigned long time_ticks_to_ms(kticks_t ticks);

/**
 * @brief Gets the current time in ticks.
 *
 * This function returns the current time in ticks.
 *
 * @return The current time in ticks.
 */
kticks_t time_now_ticks(void);

/**
 * @brief Gets the current time in milliseconds.
 *
 * This function returns the current time in milliseconds.
 *
 * @return The current time in milliseconds.
 */
unsigned long time_now_ms(void);

/**
 * @brief Returns the number of ticks before the nearest software timer expires.
 *
 * This helper is used by the tickless idle decision code. If no timer exists,
 * the return value is capped by KCONF_TICKLESS_MAX_IDLE_TICKS.
 */
kticks_t time_next_timeout_ticks(void);

/**
 * @brief Runs one tickless-idle decision when the idle task is scheduled.
 *
 * In this QEMU project the function records the idle window that could be
 * suppressed on hardware. The normal SysTick source is kept running so that
 * all Lab1-Lab9 regression tests remain deterministic.
 */
kticks_t tickless_idle_enter(void);

/**
 * @brief Reads tickless idle statistics.
 */
void tickless_get_stats(kticks_t *suppressed_ticks, unsigned int *enter_count, kticks_t *max_window);

/**
 * @brief Initializes the kernel time module.
 *
 * This function initializes the kernel time module.
 * It should be called before using any of the time-related functions.
 */
void k_time_init(void);

/*
********************************************************************************
*                            Software Watchdog Management
********************************************************************************
*/

#define MAGIC_WATCHDOG MAKE_MAGIC('W', 'D', 'O', 'G')

typedef struct kwatchdog {
    kobject_t kobj;              /**< Kernel object */
    struct ktask *owner;         /**< Task watched by this object */
    kticks_t timeout_ticks;      /**< Timeout threshold */
    kticks_t last_feed_tick;     /**< Last feed time */
    unsigned int expired_count;  /**< Number of recorded expirations */
    int active;                  /**< Whether the watchdog is currently active */
    int on_list;                 /**< Whether the watchdog is linked into scan list */
    TAILQ_ENTRY(kwatchdog)
    wd_node; /**< Watchdog scan list node */
} kwatchdog_t;

kwatchdog_t *watchdog_new(void);
kstatus_t watchdog_init(kwatchdog_t *wd, ktask_t *owner, kticks_t timeout_ticks);
kstatus_t watchdog_start(kwatchdog_t *wd);
kstatus_t watchdog_feed(kwatchdog_t *wd);
kstatus_t watchdog_stop(kwatchdog_t *wd);
kstatus_t watchdog_delete(kwatchdog_t *wd);
void watchdog_scan(void);
unsigned int watchdog_expired_count(kwatchdog_t *wd);

/*
********************************************************************************
*                            Semaphore Management
********************************************************************************
*/

#define MAGIC_SEMAPHORE MAKE_MAGIC('S', 'E', 'M', 'A')

/**
 * @struct semaphore
 * @brief Structure representing a semaphore.
 *
 * This structure defines a semaphore, which is used for synchronization
 * and mutual exclusion in concurrent programming. It contains a magic
 * number, a count, and a wait queue.
 */
typedef struct semaphore {
    kobject_t kobj;          /**< Kernel object */
    int count;               /**< Current count of the semaphore */
    wait_queue_t wait_queue; /**< Wait queue for blocked tasks */
} ksem_t;

// static initializer
#define SEM_INITIALIZER(sem, val)                                \
    {                                                            \
        .kobj = KOBJ_INITIALIZER(MAGIC_SEMAPHORE, 0),            \
        .count = (int)(val),                                     \
        .wait_queue = SCHED_QUEUE_INITIALIZER((sem).wait_queue), \
    }
#define DEFINE_SEM(sem, val) ksem_t sem = SEM_INITIALIZER(sem, val)

/**
 * @brief Creates a new semaphore with the specified initial count.
 *
 * This function creates a new semaphore with the specified initial count.
 * The initial count determines the number of resources available for the semaphore.
 *
 * @param count The initial count for the semaphore.
 * @return A pointer to the newly created semaphore.
 */
ksem_t *sem_new(int count);

/**
 * @brief Initializes a semaphore.
 *
 * This function initializes a semaphore with the specified initial count.
 *
 * @param sem Pointer to the semaphore structure.
 * @param count The initial count for the semaphore.
 * @return The status of the semaphore initialization.
 */
kstatus_t sem_init(ksem_t *sem, int count);

/**
 * @brief Deletes a semaphore.
 *
 * This function deletes a semaphore and frees any associated resources.
 *
 * @param sem Pointer to the semaphore structure.
 * @return The status of the semaphore deletion.
 */
kstatus_t sem_delete(ksem_t *sem);

/**
 * @brief Attempts to take a semaphore without blocking. Can be called from an ISR.
 *
 * @param sem Pointer to the semaphore to be taken.
 * @return The status of the operation.
 */
kstatus_t sem_try_take(ksem_t *sem);

/**
 * @brief Takes a semaphore.
 *
 * This function tries to take a semaphore. If the semaphore is not available,
 * it will wait for the specified timeout duration.
 *
 * @param sem Pointer to the semaphore structure.
 * @param timeout The maximum time to wait for the semaphore.
 * @return The status of the semaphore take operation.
 */
kstatus_t sem_take(ksem_t *sem, kticks_t timeout);

/**
 * @brief Gives a semaphore.
 *
 * This function gives a semaphore, releasing it for other tasks to take.
 *
 * @param sem Pointer to the semaphore structure.
 * @return The status of the semaphore give operation.
 */
kstatus_t sem_give(ksem_t *sem);

/*
********************************************************************************
*                            Mutex Management
********************************************************************************
*/
#define MAGIC_MUTEX MAKE_MAGIC('M', 'U', 'T', 'X')

/**
 * @brief Structure representing a mutex.
 *
 * This structure defines a mutex, which is used for mutual exclusion in multi-threaded environments.
 * It contains information about the mutex's owner, take count, original priority, and wait queue.
 */
typedef struct mutex {
    kobject_t kobj;          /**< Kernel object */
    struct ktask *owner;     /**< Pointer to the task that currently owns the mutex */
    int take_count;          /**< Number of times the mutex has been taken */
    int original_prio;       /**< Original priority of the task that owns the mutex */
    wait_queue_t wait_queue; /**< Wait queue for tasks waiting to acquire the mutex */
} kmutex_t;

// static initializer
#define MUTEX_INITIALIZER(mtx)                                   \
    {                                                            \
        .kobj = KOBJ_INITIALIZER(MAGIC_MUTEX, 0),                \
        .owner = NULL,                                           \
        .take_count = 0,                                         \
        .original_prio = PRIO_LOWEST,                            \
        .wait_queue = SCHED_QUEUE_INITIALIZER((mtx).wait_queue), \
    }

#define DEFINE_MUTEX(mtx) kmutex_t mtx = MUTEX_INITIALIZER(mtx)

/**
 * @brief Creates a new mutex.
 *
 * This function creates and initializes a new mutex object.
 *
 * @return A pointer to the newly created mutex object.
 */
kmutex_t *mutex_new(void);

/**
 * @brief Initializes a mutex.
 *
 * This function initializes a mutex object.
 *
 * @param mutex Pointer to the mutex object to be initialized.
 * @return The status of the operation.
 */
kstatus_t mutex_init(kmutex_t *mutex);

/**
 * @brief Deletes a mutex.
 *
 * This function deletes a mutex object.
 *
 * @param mutex Pointer to the mutex object to be deleted.
 * @return The status of the operation.
 */
kstatus_t mutex_delete(kmutex_t *sem);

/**
 * Tries to take a mutex without blocking. Can be called from an ISR.
 *
 * @param mutex Pointer to the mutex to be taken.
 * @return The status of the operation.
 */
kstatus_t mutex_try_take(kmutex_t *mutex);

/**
 * @brief Takes a mutex.
 *
 * This function takes a mutex object. If the mutex is not available, the function will block until it becomes available.
 *
 * @param mutex Pointer to the mutex object to be taken.
 * @return The status of the operation.
 */
kstatus_t mutex_take(kmutex_t *mutex);

/**
 * @brief Gives a mutex.
 *
 * This function gives a mutex object, allowing other tasks to take it.
 *
 * @param mutex Pointer to the mutex object to be given.
 * @return The status of the operation.
 */
kstatus_t mutex_give(kmutex_t *mutex);

/*
********************************************************************************
*                            Message Queue Management
********************************************************************************
*/
#define MAGIC_MSGQ MAKE_MAGIC('M', 'S', 'G', 'Q')

/**
 * @brief Structure representing a message queue.
 */
typedef struct msgq {
    kobject_t kobj; /**< Kernel object. */

    int msg_size;         /**< Size of each message in the queue. */
    unsigned int max_num; /**< Maximum number of messages in the queue. */

    // msgq buffer
    unsigned int free_num; /**< Number of free slots in the queue. */
    char *buffer_start;    /**< Pointer to the start of the message buffer. */
    char *buffer_end;      /**< Pointer to the end of the message buffer. */
    char *read_ptr;        /**< Pointer to the next message to be read. */
    char *write_ptr;       /**< Pointer to the next message to be written. */

    wait_queue_t wait_queue; /**< Wait queue for blocking operations on the queue. */
} kmsgq_t;

// static initializer
#define MSGQ_INITIALIZER(q_name, q_buffer, q_msg_size, q_max_msgs)        \
    {                                                                     \
        .kobj = KOBJ_INITIALIZER(MAGIC_MSGQ, 0),                          \
        .msg_size = q_msg_size,                                           \
        .max_num = q_max_msgs,                                            \
        .free_num = q_max_msgs,                                           \
        .buffer_start = (char *)(q_buffer),                               \
        .buffer_end = (char *)(q_buffer) + ((q_max_msgs) * (q_msg_size)), \
        .read_ptr = (char *)(q_buffer),                                   \
        .write_ptr = (char *)(q_buffer),                                  \
        .wait_queue = SCHED_QUEUE_INITIALIZER((q_name).wait_queue),       \
    }

#define DEFINE_MSGQ(q_name, q_msg_size, q_max_msgs)                   \
    static char __msgq_ringbuf_##q_name[(q_msg_size) * (q_max_msgs)]; \
    kmsgq_t q_name = MSGQ_INITIALIZER(q_name, __msgq_ringbuf_##q_name, (q_msg_size), (q_max_msgs))

/**
 * @brief Creates a new message queue.
 *
 * This function creates a new message queue with the specified parameters.
 *
 * @param buffer The buffer to be used for storing the messages in the queue.
 * @param msg_size The size of each message in the queue.
 * @param max_num The maximum number of messages that the queue can hold.
 *
 * @return A pointer to the newly created message queue, or NULL if an error occurred.
 */
kmsgq_t *msgq_new(void *buffer, int msg_size, unsigned int max_num);

/**
 * Initializes a message queue.
 *
 * @param msgq The message queue to initialize.
 * @param buffer The buffer to be used for storing messages.
 * @param msg_size The size of each message in bytes.
 * @param max_num The maximum number of messages that the queue can hold.
 * @return The status of the operation.
 */
kstatus_t msgq_init(kmsgq_t *msgq, void *buffer, int msg_size, unsigned int max_num);

/**
 * Deletes a message queue.
 *
 * @param msgq The message queue to delete.
 * @return The status of the operation.
 */
kstatus_t msgq_delete(kmsgq_t *msgq);

/**
 * @brief Tries to take a message from the specified message queue without blocking. Can be called from an ISR.
 *
 * @param msgq The message queue from which to take a message.
 * @param message A pointer to the buffer where the taken message will be copied.
 * @return The status of the operation. Possible values are:
 *         - `K_OK` if a message was successfully taken.
 *         - `K_EBUSY` if the message queue is empty and no message was taken.
 *         - Other error codes indicating a failure to take a message.
 */
kstatus_t msgq_try_take(kmsgq_t *msgq, void *message);

/**
 * Retrieves a message from the message queue.
 *
 * @param msgq The message queue to retrieve the message from.
 * @param message The buffer to store the retrieved message.
 * @param timeout The maximum time to wait for a message.
 * @return The status of the operation.
 */
kstatus_t msgq_take(kmsgq_t *msgq, void *message, kticks_t timeout);

/**
 * Adds a message to the message queue.
 *
 * @param msgq The message queue to add the message to.
 * @param message The message to be added.
 * @return The status of the operation.
 */
kstatus_t msgq_give(kmsgq_t *msgq, const void *message);

/**
 * Gets the number of free slots in the message queue.
 *
 * @param msgq The message queue to get the number of free slots from.
 * @return The number of free slots in the message queue.
 */
int msgq_get_free(kmsgq_t *msgq);

/*
********************************************************************************
*                            Pool Management
********************************************************************************
*/
#include "k_pool.h"

/*
********************************************************************************
*                            Kernel State
********************************************************************************
*/

typedef struct kernel_state_t {
    // sched state
    int running;           // kernel running flag
    int sched_pending;     // scheduler pending flag
    int sched_lock_level;  // scheduler lock level
    int irq_nested_count;  // nested Interrupt Request count
    int irq_lock_level;    // Interrupt Request lock level

    ready_queue_t ready_queue;  // scheduler ready queue
    wait_queue_t wait_queue;    // scheduler wait queue for task_join and task_sleep

    // task state
    taskq_t task_list;      // all tasks in system
    ktask_t *current_task;  // who is running now
    ktask_t *idle_task;     // idle task
    ktask_t *timer_task;    // timer task
    ktask_t *zombie_task;   // zombie task (deleted task waiting for cleanup)

    // time state
    kticks_t ticks;  // current tick count
    timer_queue_t timer_queue;

    // tickless idle statistics
    kticks_t tickless_expected_idle;     // last predicted idle window
    kticks_t tickless_suppressed_ticks;  // accumulated suppressible ticks
    unsigned int tickless_enter_count;   // successful tickless decisions
    unsigned int tickless_abort_count;   // decisions rejected because system was not idle
    kticks_t tickless_max_window;        // maximum predicted idle window
} kernel_state_t;

extern kernel_state_t kernel_state;

/**
 * @brief Starts the kernel.
 *
 * This function is responsible for starting the kernel and initializing all necessary components.
 * It should be called once at the beginning of the program.
 */
void kern_start(void);

/*
********************************************************************************
*                            Kernel Statistics
********************************************************************************
*/

#ifdef KCONF_KERN_STATS_ENABLED

typedef struct kern_stats {
    // scheduler statistics
    unsigned int sched_request;       // number of task switch request
    unsigned int sched_irq_delayed;   // number of delayed task switch because of in interrut
    unsigned int sched_lock_delayed;  // number of delayed task switch because of scheduler locked
    unsigned int sched_acked;         // number of task switch response
    unsigned int sched_switch;        // number of task context switch
    unsigned int sched_preempt;       // number of task preempted request
    unsigned int sched_lock;          // number of scheduler locked request
    unsigned int sched_unlock;        // number of scheduler unlocked request
    unsigned int sched_irq_enter;     // number of enter irq
    unsigned int sched_irq_exit;      // number of exit irq

    // task statistics
    unsigned int task_create;  // number of task created
    unsigned int task_delete;  // number of task deleted
} kern_stats_t;

// global kernel statistics variable
extern kern_stats_t kernel_stats;

// kernel statistics counter operations
#define KSTAT_INC(x) (kernel_stats.x++)
#define KSTAT_DEC(x) (kernel_stats.x--)
#define KSTAT_ADD(x, n) (kernel_stats.x += (n))
#define KSTAT_SUB(x, n) (kernel_stats.x -= (n))
#define KSTAT_SET(x, n) (kernel_stats.x = (n))
#define KSTAT_GET(x) (kernel_stats.x)
#define KSTAT_CLEAR() memset(&kernel_stats, 0, sizeof(kern_stats_t))

#else

#define KSTAT_INC(x)
#define KSTAT_DEC(x)
#define KSTAT_ADD(x, n)
#define KSTAT_SUB(x, n)
#define KSTAT_SET(x, n)
#define KSTAT_GET(x)
#define KSTAT_CLEAR()

#endif

/*
********************************************************************************
*                            User Functions
********************************************************************************
*/

/**
 * @brief Registers a hook function to be called when a task is created.
 *
 * @param task Pointer to the task being created.
 */
void task_create_hook(ktask_t *task);

/**
 * @brief Registers a hook function to be called when a task is deleted.
 *
 * @param task Pointer to the task being deleted.
 */
void task_delete_hook(ktask_t *task);

/**
 * @brief Registers a hook function to be called when the state of a task changes.
 *
 * @param task Pointer to the task whose state has changed.
 */
void task_state_change_hook(ktask_t *task);

/**
 * @brief Registers a hook function to be called when a task switch occurs.
 *
 * @param from Pointer to the task being switched from.
 * @param to Pointer to the task being switched to.
 */
void task_switch_hook(ktask_t *from, ktask_t *to);

/**
 * @brief Registers a hook function to be called when the system is idle.
 */
void task_idle_hook(void);

/**
 * @brief Registers a hook function to be called during kernel initialization.
 */
void kern_init_hook(void);

/**
 * @brief Registers a hook function to be called during user initialization.
 */
void usr_init_hook(void);

#endif  // _K_KERN_H_
