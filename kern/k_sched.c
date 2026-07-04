#include "k_kern.h"

// Initializes a scheduler queue.
void sched_queue_init(sched_queue_t *queue) {
    TAILQ_INIT(&queue->taskq);
    queue->count = 0;
}

// Returns the highest priority task in the scheduler queue.
ktask_t *sched_queue_get_highest(sched_queue_t *queue) {
    return TAILQ_FIRST(&queue->taskq);
}

// Inserts a task into the scheduler queue.
void sched_queue_insert_tail(sched_queue_t *queue, ktask_t *task) {
    // Add task by priority. Keep queue sorted in descending priority order
    ktask_t *t;

    queue->count++;
    TAILQ_FOREACH(t, &queue->taskq, stateq_node) {
        if (PRIO_LOWER(t->prio, task->prio)) {
            TAILQ_INSERT_BEFORE(t, task, stateq_node);
            return;
        }
    }

    TAILQ_INSERT_TAIL(&queue->taskq, task, stateq_node);
}

// Removes a task from the scheduler queue.
void sched_queue_remove(sched_queue_t *queue, ktask_t *task) {
    TAILQ_REMOVE(&queue->taskq, task, stateq_node);
    queue->count--;
}

/**
 * ready queue api
 */
void ready_queue_init(void) {
    sched_queue_init(&kernel_state.ready_queue);
}

ktask_t *ready_queue_get_highest(void) {
    return sched_queue_get_highest(&kernel_state.ready_queue);
}

void ready_queue_insert_tail(ktask_t *task) {
    sched_queue_insert_tail(&kernel_state.ready_queue, task);
}

void ready_queue_remove(ktask_t *task) {
    sched_queue_remove(&kernel_state.ready_queue, task);
}

/**
 * wait queue api
 */
void wait_queue_init(wait_queue_t *wait_q) {
    K_ASSERT(wait_q != NULL);
    if (wait_q != NULL) {
        sched_queue_init(wait_q);
    }
}

ktask_t *wait_queue_get_highest(wait_queue_t *wait_q) {
    K_ASSERT(wait_q != NULL);
    if (wait_q != NULL) {
        return sched_queue_get_highest(wait_q);
    }
    return NULL;
}

void wait_queue_insert_tail(wait_queue_t *wait_q, ktask_t *task) {
    K_ASSERT(wait_q != NULL);
    if (wait_q != NULL) {
        sched_queue_insert_tail(wait_q, task);
    }
}

void wait_queue_remove(wait_queue_t *wait_q, ktask_t *task) {
    K_ASSERT(wait_q != NULL);
    if (wait_q != NULL) {
        sched_queue_remove(wait_q, task);
    }
}

// change task priority and do a preempt if necessary
kstatus_t sched_change_priority(ktask_t *task, int new_prio) {
    if (task == NULL || new_prio < PRIO_LOWEST || new_prio > PRIO_HIGHEST) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        if (task->state == TASK_READY) {
            ready_queue_remove(task);
            task->prio = new_prio;
            ready_queue_insert_tail(task);
            sched_preempt(task, __func__);
        } else if (task->state == TASK_RUNNING && PRIO_LOWER(new_prio, task->prio)) {
            task->prio = new_prio;
            sched_yield(__func__);
        } else {
            task->prio = new_prio;
        }
    }

    return K_OK;
}

// change task state and do schedule queue operations, preempt is not done here but in the caller
ktask_t *sched_change_state_to(ktask_t *t, task_state_t newstate) {
    K_ASSERT(KERNEL_IN_CRITICAL());  // must run in irq disabled context
    t->state_change_count++;

    // wait_queue_t *wait_q = t->wait_on;
    switch (t->state) {
        case TASK_DEAD:
            if (newstate == TASK_READY) {  // created
                // add task to ready list
                ready_queue_insert_tail(t);
            } else {
                K_ASSERT(0 && "TASK_DEAD state error");
                return t;
            }
            break;
        case TASK_READY:
            if (newstate == TASK_RUNNING) {  // run
                ready_queue_remove(t);
            } else if (newstate == TASK_BLOCKED) {  // suspended
                ready_queue_remove(t);
                wait_queue_insert_tail(t->wait_on, t);
            } else if (newstate == TASK_DEAD) {  // deleted
                ready_queue_remove(t);
            } else if (newstate == TASK_READY) {
                // do nothing
                // ready_queue_remove(t);
                // ready_queue_insert_tail(t);
            } else {
                K_ASSERT(0 && "TASK_READY state error");
                return t;
            }
            break;
        case TASK_RUNNING:
            if (newstate == TASK_READY) {  // preempted
                ready_queue_insert_tail(t);
            } else if (newstate == TASK_BLOCKED) {  // blocked
                wait_queue_insert_tail(t->wait_on, t);
            } else if (newstate == TASK_DEAD) {  // suicide
                // do nothing
            } else if (newstate == TASK_RUNNING) {
                // do nothing
            } else {
                K_ASSERT(0 && "TASK_RUNNING state error");
                return t;
            }
            break;
        case TASK_BLOCKED:
            if (newstate == TASK_READY) {  // unblocked
                wait_queue_remove(t->wait_on, t);
                t->wait_on = NULL;
                ready_queue_insert_tail(t);
            } else if (newstate == TASK_DEAD) {  // deledted
                wait_queue_remove(t->wait_on, t);
                t->wait_on = NULL;
            } else {
                K_ASSERT(0 && "TASK_BLOCKED state error");
                return t;
            }
            break;
        default:
            K_ASSERT(0 && "task unknown state error");
            return t;
            // break;
    }

    t->old_state = t->state;
    t->state = newstate;

    task_state_change_hook(t);

    return t;
}

// Picks the next task to be executed from the ready queue.
static ktask_t *pick_next_task(void) {
    ktask_t *highest = ready_queue_get_highest();
    return highest;
}

// Switches the current task stack pointer to the next task stack.
kstack_t *sched_switch_task_stack(kstack_t *sp) {
    K_ASSERT(KERNEL_IN_CRITICAL());
    KSTAT_INC(sched_acked);

    ktask_t *current_task = kernel_state.current_task;
    if (current_task) {
        current_task->context = (void *)sp;
    }

    ktask_t *next_task = pick_next_task();
    if (next_task == NULL) {
        return sp;
    }

    // Corner case of current running task: current_task maybe NULL or in any state.
    if (current_task != NULL && current_task->state == TASK_RUNNING) {
        // if current running task is higher than next_task, no need to change stack
        if (PRIO_HIGHER(current_task->prio, next_task->prio)) {
            return sp;
        }
        // if current running task is not equal to next_task, change state to ready
        K_ASSERT(current_task != next_task);
        sched_change_state_to(current_task, TASK_READY);
    }
    sched_change_state_to(next_task, TASK_RUNNING);

    next_task->stack_switch_count++;
    KSTAT_INC(sched_switch);

    task_switch_hook(current_task, next_task);

    kernel_state.current_task = next_task;

    return (kstack_t *)next_task->context;
}

// request to do a task reschedule
void sched_yield(const char *reason) {
    WITHIN_CRITICAL() {
        if (kernel_state.current_task != NULL) {
            kernel_state.current_task->yield_reason = reason;
        }

        if (kernel_state.running) {
            KSTAT_INC(sched_request);
            // request to do a schedule
            if (kernel_state.irq_nested_count > 0) {  // check if in irq context
                KSTAT_INC(sched_irq_delayed);
                kernel_state.sched_pending++;                // set pending flag and do NOT schedule
            } else if (kernel_state.sched_lock_level > 0) {  // check if task schedule is locked
                KSTAT_INC(sched_lock_delayed);
                kernel_state.sched_pending++;  // set pending flag and do NOT schedule
            } else {                           // not in irq context, do schedule immediately
                cpu_sched_request();
            }
        }
    }
}

// preempt the current task if the new task has higher priority
void sched_preempt(ktask_t *task, const char *reason) {
    WITHIN_CRITICAL() {
        KSTAT_INC(sched_preempt);
        if (kernel_state.current_task != NULL && PRIO_HIGHER(task->prio, kernel_state.current_task->prio)) {
            sched_yield(reason);
        }
    }
}

// get the shceduler lock level, 0 means no lock else shceduler is locked
int sched_lock_level(void) {
    return kernel_state.sched_lock_level;
}

// disable task schedule
int sched_lock(void) {
    K_ASSERT(kernel_state.sched_lock_level >= 0);

    int state = 0;
    WITHIN_CRITICAL() {
        KSTAT_INC(sched_lock);
        state = kernel_state.sched_lock_level;
        kernel_state.sched_lock_level++;
    }

    return state;
}

// enable task schedule without doing a preempt
int sched_unlock(void) {
    K_ASSERT(kernel_state.sched_lock_level > 0);

    int state = 0;
    WITHIN_CRITICAL() {
        KSTAT_INC(sched_unlock);
        state = kernel_state.sched_lock_level;
        kernel_state.sched_lock_level--;
    }

    return state;
}

// enable task schedule and do a preempt as the shcedule state may has changed in sched_lock
int sched_unlock_preempt(void) {
    K_ASSERT(kernel_state.sched_lock_level > 0);

    int state = 0;

    WITHIN_CRITICAL() {
        KSTAT_INC(sched_unlock);
        kernel_state.sched_lock_level--;
        // schedule state may has changed
        if (kernel_state.sched_lock_level == 0) {
            sched_preempt(task_self(), __func__);
        }
    }

    return state;
}

// irq enter: increase irq count
void sched_irq_enter(void) {
    int primask = cpu_irq_save();
    KSTAT_INC(sched_irq_enter);
    kernel_state.irq_nested_count++;  // increase irq count
    cpu_irq_restore(primask);
}

// irq exit: decrease irq count and check if need to do a schedule
void sched_irq_exit(void) {
    int primask = cpu_irq_save();
    KSTAT_INC(sched_irq_exit);
    kernel_state.irq_nested_count--;           // decrease irq count
    if (kernel_state.irq_nested_count == 0) {  // check if it's the outermost irq exit
        if (kernel_state.sched_pending > 0) {  // check if need to do a schedule
            kernel_state.sched_pending = 0;    // clear request
            sched_yield(__func__);
        }
    }
    cpu_irq_restore(primask);
}

// wrapper for isr function, the actual isr function is surrounded by sched_irq_enter and sched_irq_exit
int sched_call_isr(isr_func_t isr, void *arg) {
    sched_irq_enter();   // mark irq enter
    int ret = isr(arg);  // Call actual user ISR code, scheduler is disabled in user ISR
    sched_irq_exit();    // mark irq exit, may do a schedule
    return ret;
}

// schedule the current task to block on the wait_q (wait for a condition to be met).
kstatus_t sched_wait_on(wait_queue_t *wait_q, const char *reason) {
    kstatus_t wait_result;

    WITHIN_CRITICAL() {
        ktask_t *self = kernel_state.current_task;

        self->yield_reason = reason;
        self->wait_on = wait_q;
        self->wait_result = K_WAIT_BLOCKING;
        sched_change_state_to(self, TASK_BLOCKED);

        sched_yield(__func__);

        K_ASSERT(self->wait_result != K_WAIT_BLOCKING && self->wait_on == NULL);
        wait_result = self->wait_result;  // return wait result
    }

    return wait_result;
}

// schedule the current task to block on kernel wait_queue.
kstatus_t sched_wait(void) {
    return sched_wait_on(&kernel_state.wait_queue, __func__);
}

// timeout handler for wait_timeout
static int wait_timeout_handler(void *task) {
    sched_wakeup_task(task, K_WAIT_TIMEOUT);
    return 0;
}

// schedule current task to wait for a certain time until the condition is met
kstatus_t sched_wait_timeout(wait_queue_t *wait_q, kticks_t timeout, kticks_t *remain, const char *reason) {
    kstatus_t result = K_OK;
    kticks_t remain_time = 0;

    if (timeout == TIME_NO_WAIT) {  // no wait
        remain_time = TIME_NO_WAIT;
        result = K_WAIT_TIMEOUT;
    } else if (timeout == TIME_FOREVER_WAIT) {  // wait forever
        remain_time = TIME_FOREVER_WAIT;
        result = sched_wait_on(wait_q, reason);
    } else {  // wait for a certain time
        kticks_t start_time = time_now_ticks();

        ktimer_t timer;
        timer_initialize(&timer);
        timer_add_oneshot(&timer, timeout, wait_timeout_handler, (void *)task_self());
        result = sched_wait_on(wait_q, reason);  // wait for condition to be met or timeout
        timer_delete(&timer);

        // calculate remain time
        kticks_t diff_time = time_now_ticks() - start_time;
        if (diff_time < timeout) {
            remain_time = timeout - diff_time;
        } else {
            remain_time = 0;
        }
    }

    if (remain) {
        *remain = remain_time;
    }
    return result;
}

// wakeup the highest priority task in the wait queue
kstatus_t sched_wakeup(wait_queue_t *wait_q, kstatus_t wait_result) {
    if (wait_q == NULL) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        ktask_t *task = wait_queue_get_highest(wait_q);
        if (task != NULL) {
            task->wait_result = wait_result;
            sched_change_state_to(task, TASK_READY);
            sched_preempt(task, __func__);
        }
    }
    return K_OK;
}

// wakeup all tasks in the wait queue
kstatus_t sched_wakeup_all(wait_queue_t *wait_q, kstatus_t wait_result) {
    if (wait_q == NULL) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        ktask_t *task;
        int need_reschedule = 0;
        while ((task = wait_queue_get_highest(wait_q)) != NULL) {
            task->wait_result = wait_result;
            sched_change_state_to(task, TASK_READY);
            need_reschedule++;
        }
        if (need_reschedule) {
            sched_yield(__func__);
        }
    }
    return K_OK;
}

// wakeup a specific task
kstatus_t sched_wakeup_task(ktask_t *task, kstatus_t wait_result) {
    if (task == NULL) {
        return K_EINVAL;
    }

    kstatus_t status = K_OK;
    WITHIN_CRITICAL() {
        task->wait_result = wait_result;
        sched_change_state_to(task, TASK_READY);
        sched_preempt(task, __func__);
    }
    return status;
}

// start rtos's scheduler
void sched_start(void) {
    // mark kernel as running
    kernel_state.running = 1;
    // enable interrupts to start the scheduler
    cpu_irq_enable();
    // start the first task
    task_yield();
}

// initialize schedule library
void k_sched_init(void) {
    memset(&kernel_state, 0, sizeof(kernel_state));
    ready_queue_init();
    wait_queue_init(&kernel_state.wait_queue);
}
