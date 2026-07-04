// #define LOCAL_DEBUG

#include "k_kern.h"

#define TIMER_ON_QUEUE_FLAG (1 << KOBJ_FLAG_TYPE_OFFSET)
#define TIMER_IS_ON_QUEUE(tm) KOBJ_FLAG_CHECK(tm, TIMER_ON_QUEUE_FLAG)
#define TIMER_ON_QUEUE(tm) KOBJ_FLAG_SET(tm, TIMER_ON_QUEUE_FLAG)
#define TIMER_OFF_QUEUE(tm) KOBJ_FLAG_CLEAR(tm, TIMER_ON_QUEUE_FLAG)

static void timer_queue_init(void) {
    TAILQ_INIT(&kernel_state.timer_queue.timerq);
    kernel_state.timer_queue.count = 0;
}

static ktimer_t *timer_queue_first(void) {
    return TAILQ_FIRST(&kernel_state.timer_queue.timerq);
}

static void timer_queue_remove(ktimer_t *tm) {
    TIMER_OFF_QUEUE(tm);

    TAILQ_REMOVE(&kernel_state.timer_queue.timerq, tm, timer_node);
    kernel_state.timer_queue.count--;
}

static void timer_queue_insert(ktimer_t *new_tm) {
    ktimer_t *tm;

    TIMER_ON_QUEUE(new_tm);
    kernel_state.timer_queue.count++;

    TAILQ_FOREACH(tm, &kernel_state.timer_queue.timerq, timer_node) {
        if (TIME_GT(tm->expire_time, new_tm->expire_time)) {
            TAILQ_INSERT_BEFORE(tm, new_tm, timer_node);
            return;
        }
    }

    TAILQ_INSERT_TAIL(&kernel_state.timer_queue.timerq, new_tm, timer_node);
}

static inline int timer_in_queue(ktimer_t *tm) {
    return TIMER_IS_ON_QUEUE(tm);
}

DEFINE_POOL(timer_pool, ktimer_t, KCONF_TIMER_MAX_COUNT);

// create a new timer
ktimer_t *timer_new(void) {
    ktimer_t *timer = pool_alloc(&timer_pool);
    if (timer == NULL) {
        return NULL;
    }
    if (timer_initialize(timer) != K_OK) {
        pool_free(&timer_pool, timer);
        return NULL;
    } else {
        KOBJ_FLAG_SET(timer, KOBJ_FLAG_POOL_ALLOC);
    }
    return timer;
}

// initialize a timer
kstatus_t timer_initialize(ktimer_t *tm) {
    memset(tm, 0, sizeof(ktimer_t));
    KOBJ_INIT(tm, MAGIC_TIMER, 0);
    return K_OK;
}

// delete a timer
kstatus_t timer_delete(ktimer_t *timer) {
    if (!KOBJ_MAGIC_CHECK(timer, MAGIC_TIMER)) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        if (timer_in_queue(timer)) {
            timer_queue_remove(timer);
        }

        timer->period_time = 0;
        timer->callback = NULL;
        timer->arg = NULL;

        if (KOBJ_FLAG_CHECK(timer, KOBJ_FLAG_POOL_ALLOC)) {
            pool_free(&timer_pool, timer);
        }
    }
    return K_OK;
}

// add a timer with delay and period ticks
kstatus_t timer_add(ktimer_t *timer, kticks_t delay, kticks_t period, timer_func_t callback, void *arg) {
    kticks_t now;

    K_TRACE("add timer %p delay %lu period %lu callback %p arg %p", timer, delay, period, callback, arg);
    // check arguments
    if (!KOBJ_MAGIC_CHECK(timer, MAGIC_TIMER) || callback == NULL) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        if (timer_in_queue(timer)) {
            timer_queue_remove(timer);
        }

        now = time_now_ticks();
        timer->expire_time = now + delay;
        timer->period_time = period;
        timer->callback = callback;
        timer->arg = arg;
        timer_queue_insert(timer);
    }

    // K_TRACE("scheduled time %u", timer->expire_time);

    return K_OK;
}

// add a one-shot timer with delay ticks
kstatus_t timer_add_oneshot(ktimer_t *timer, kticks_t delay, timer_func_t callback, void *arg) {
    return timer_add(timer, delay, 0, callback, arg);
}

// add a periodic timer with period ticks
kstatus_t timer_add_periodic(ktimer_t *timer, kticks_t period, timer_func_t callback, void *arg) {
    return timer_add(timer, period, period, callback, arg);
}

// process soft timers. called in task
static void soft_timer_update(void) {
    ktimer_t *timer;

    int __irqsr = cpu_irq_save();

    while ((timer = timer_queue_first()) != NULL) {
        K_ASSERT(KOBJ_MAGIC_CHECK(timer, MAGIC_TIMER));

        // if timer hasn't expired, break
        if (TIME_LT(kernel_state.ticks, timer->expire_time))
            break;

        timer_queue_remove(timer);
        cpu_irq_restore(__irqsr);  // restore interrupts before calling the callback

        // call the timer callback function
        if (timer->callback) {
            K_TRACE("timer %p firing, now %lu expire %lu period %lu callback %p",
                    timer, kernel_state.ticks, timer->expire_time, timer->period_time, timer->callback);
            timer->callback(timer->arg);
        }

        __irqsr = cpu_irq_save();  // re-disable interrupts
        // if timer is periodic and hasn't been requeued by the callback, reinsert it back into the queue
        if (timer->period_time > 0 && !timer_in_queue(timer)) {
            timer->expire_time = kernel_state.ticks + timer->period_time;
            timer_queue_insert(timer);
        }
    }

    cpu_irq_restore(__irqsr);
}

// timer task to handle soft timers
static void timer_task(void) {
    kernel_state.timer_task = task_self();

    while (1) {
        // wait for hardware timer tick
        task_suspend();
        // process soft timers
        soft_timer_update();
    }
}

static void time_slice_schedule(void) {
    // this will put current task to the end of the ready queue
    sched_yield(__func__);
}

// called in hardware systick interrupt
int time_tick_ISR(void *arg) {
    (void)arg;  // unused

    int expired = 0;

    if (!kernel_state.running) {
        return 0;
    }

    // update kernel tick
    kernel_state.ticks++;

    // scan software watchdogs on each system tick
    watchdog_scan();

    // round-robin schedule
    time_slice_schedule();

    // handle soft timers
    WITHIN_CRITICAL() {
        ktimer_t *timer = timer_queue_first();
        if ((timer != 0) && (TIME_GE(kernel_state.ticks, timer->expire_time))) {
            expired = 1;
        }
    }

    if (expired) {
        if (kernel_state.timer_task != NULL) {
            task_resume(kernel_state.timer_task);
        }
    }
    return expired;
}

void time_tick_ISR_wrapper(void) {
    sched_call_isr(time_tick_ISR, NULL);
}

// convert time in ms to ticks
kticks_t time_ms_to_ticks(unsigned long ms) {
    kticks_t padding;
    kticks_t ticks;

    if (ms == 0) {
        return TIME_NO_WAIT;
    }

    padding = 1000 / KCONF_TICKS_PER_SECOND;
    padding = (padding > 0) ? (padding - 1) : 0;

    ticks = ((ms + padding) * KCONF_TICKS_PER_SECOND) / 1000;

    return ticks;
}

// convert time in ticks to ms
unsigned long time_ticks_to_ms(kticks_t ticks) {
    unsigned long padding;
    unsigned long time;

    if (ticks == TIME_NO_WAIT) {
        return 0;
    }

    padding = KCONF_TICKS_PER_SECOND / 1000;
    padding = (padding > 0) ? (padding - 1) : 0;

    time = ((ticks + padding) * 1000) / KCONF_TICKS_PER_SECOND;

    return time;
}

// get current time in ticks
kticks_t time_now_ticks(void) {
    return kernel_state.ticks;
}

// get current time in ms
unsigned long time_now_ms(void) {
    return time_now_ticks() * 1000ULL / KCONF_TICKS_PER_SECOND;
}

kticks_t time_next_timeout_ticks(void) {
    kticks_t remain = KCONF_TICKLESS_MAX_IDLE_TICKS;

#if KCONF_TICKLESS_ENABLED
    WITHIN_CRITICAL() {
        ktimer_t *timer = timer_queue_first();
        kticks_t now = kernel_state.ticks;

        if (timer != NULL) {
            if (TIME_GE(now, timer->expire_time)) {
                remain = 0;
            } else {
                remain = timer->expire_time - now;
            }
        }

        if (remain > KCONF_TICKLESS_MAX_IDLE_TICKS) {
            remain = KCONF_TICKLESS_MAX_IDLE_TICKS;
        }
    }
#endif

    return remain;
}

kticks_t tickless_idle_enter(void) {
    kticks_t idle_ticks = 0;

#if KCONF_TICKLESS_ENABLED
    WITHIN_CRITICAL() {
        if (!kernel_state.running || kernel_state.current_task != kernel_state.idle_task || kernel_state.ready_queue.count != 0) {
            kernel_state.tickless_abort_count++;
        } else {
            idle_ticks = time_next_timeout_ticks();
            kernel_state.tickless_expected_idle = idle_ticks;

            if (idle_ticks > 0) {
                kernel_state.tickless_enter_count++;
                kernel_state.tickless_suppressed_ticks += idle_ticks;
                if (idle_ticks > kernel_state.tickless_max_window) {
                    kernel_state.tickless_max_window = idle_ticks;
                }
            } else {
                kernel_state.tickless_abort_count++;
            }
        }
    }
#endif

    return idle_ticks;
}

void tickless_get_stats(kticks_t *suppressed_ticks, unsigned int *enter_count, kticks_t *max_window) {
    WITHIN_CRITICAL() {
        if (suppressed_ticks != NULL) {
            *suppressed_ticks = kernel_state.tickless_suppressed_ticks;
        }
        if (enter_count != NULL) {
            *enter_count = kernel_state.tickless_enter_count;
        }
        if (max_window != NULL) {
            *max_window = kernel_state.tickless_max_window;
        }
    }
}

#define TIMER_TASK_STACK_SIZE KCONF_DEFAULT_TASK_STACK_SIZE
static ktask_t timer_task_cb;
static kstack_t timer_task_stack[TIMER_TASK_STACK_SIZE];

// initialize time module
void k_time_init(void) {
    timer_queue_init();
    task_create("timer_task", (task_func_t)timer_task, NULL, PRIO_HIGHEST, sizeof(timer_task_stack), timer_task_stack, &timer_task_cb, 0);
}
