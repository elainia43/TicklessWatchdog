#define LOCAL_DEBUG

#include "k_kern.h"

#define TEST_TASKS 3

// master task tcb and stack for test
static ktask_t test_master_tcb;
static kstack_t test_master_stack[KCONF_DEFAULT_TASK_STACK_SIZE];
static const char *test_master_name = "M0";

// slave task tcb and stack for test
static ktask_t test_slave_tcbs[TEST_TASKS];
static kstack_t test_slave_stacks[TEST_TASKS][KCONF_DEFAULT_TASK_STACK_SIZE];
static const char *test_slave_names[TEST_TASKS] = {"S0", "S1", "S2"};

// test loop count
static int loops = 5;

#define TEST_TASK_PROLOGUE(tid) \
    int tid = (int)arg;         \
    K_TRACE("task %d enter", tid);

#define TEST_TASK_EPILOGUE(tid)   \
    K_TRACE("task %d exit", tid); \
    return 0;

#define DEFINE_TEST_TASK(task_entry, test_body) \
    static int task_entry(void *arg) {     \
        TEST_TASK_PROLOGUE(tid);           \
        test_body;                              \
        TEST_TASK_EPILOGUE(tid);           \
    }

/**
 * schedule test
 */
DEFINE_TEST_TASK(co_sched_task, {
    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        task_yield();
    }
})

DEFINE_TEST_TASK(preempt_sched_task, {
    int myprio = task_priority(task_self());
    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        if (ctr & 1) {
            task_set_priority(task_self(), myprio + tid);
        } else {
            task_set_priority(task_self(), myprio - tid);
        }
    }
})

DEFINE_TEST_TASK(rr_sched_task, {
    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        for (int i = 0; i < 1000000; i++)
            ;
    }
})

DEFINE_TEST_TASK(lock_sched_task, {
    sched_lock();
    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        task_yield();
    }
    sched_unlock();
})

/**
 * task state change test
 */
DEFINE_TEST_TASK(task_state_tast, {
    if (tid < TEST_TASKS - 1) {
        task_suspend();
    }

    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        task_yield();
    }

    if (tid != 0) {
        task_resume(&test_slave_tcbs[tid - 1]);
    }
})

/**
 * Timer test
 */
static ktimer_t test_timers[TEST_TASKS];

static int timer_callback(void *arg) {
    int tid = (int)arg;
    K_TRACE("timer %d fired", tid);
    task_resume(&test_slave_tcbs[tid]);
    task_resume(&test_slave_tcbs[tid + 1]);
    return 0;
}

DEFINE_TEST_TASK(timer_test_task, {
    timer_initialize(&test_timers[tid]);

    if (tid > 0) {
        K_TRACE("task %d suspend", tid);
        task_suspend();
    }
    if (tid < TEST_TASKS - 1) {
        timer_add_oneshot(&test_timers[tid], 500, (timer_func_t)timer_callback, (void *)tid);
    }

    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);
        task_sleep(10);
    }

    if (tid < TEST_TASKS - 1) {
        K_TRACE("task %d suspend", tid);
        task_suspend();  // wait timer
    }

    timer_delete(&test_timers[tid]);
})

/**
 * Tickless idle test
 */
DEFINE_TEST_TASK(tickless_test_task, {
    kticks_t suppressed_before = 0;
    kticks_t suppressed_after = 0;
    kticks_t max_window = 0;
    unsigned int enter_before = 0;
    unsigned int enter_after = 0;

    if (tid == 0) {
        tickless_get_stats(&suppressed_before, &enter_before, &max_window);
    }

    task_sleep(80 + tid * 20);

    if (tid == 0) {
        tickless_get_stats(&suppressed_after, &enter_after, &max_window);
        K_TRACE("tickless stats: enter %u -> %u, suppressed %lu -> %lu, max_window %lu",
                enter_before, enter_after, suppressed_before, suppressed_after, max_window);
        K_ASSERT(enter_after > enter_before);
        K_ASSERT(suppressed_after > suppressed_before);
        K_ASSERT(max_window > 0);
    }
})

/**
 * Software watchdog test
 */
static kwatchdog_t test_watchdogs[TEST_TASKS];

DEFINE_TEST_TASK(watchdog_test_task, {
    if (tid == 0) {
        watchdog_init(&test_watchdogs[tid], task_self(), 50);
        watchdog_start(&test_watchdogs[tid]);
        for (int ctr = 0; ctr < 3; ctr++) {
            task_sleep(10);
            watchdog_feed(&test_watchdogs[tid]);
        }
        K_TRACE("watchdog feed path expired=%u", watchdog_expired_count(&test_watchdogs[tid]));
        K_ASSERT(watchdog_expired_count(&test_watchdogs[tid]) == 0);
        watchdog_stop(&test_watchdogs[tid]);
        watchdog_delete(&test_watchdogs[tid]);
    } else if (tid == 1) {
        watchdog_init(&test_watchdogs[tid], task_self(), 30);
        watchdog_start(&test_watchdogs[tid]);
        task_sleep(80);
        K_TRACE("watchdog timeout path expired=%u", watchdog_expired_count(&test_watchdogs[tid]));
        K_ASSERT(watchdog_expired_count(&test_watchdogs[tid]) > 0);
        watchdog_delete(&test_watchdogs[tid]);
    } else {
        watchdog_init(&test_watchdogs[tid], task_self(), 40);
        watchdog_start(&test_watchdogs[tid]);
        watchdog_feed(&test_watchdogs[tid]);
        watchdog_stop(&test_watchdogs[tid]);
        task_sleep(60);
        K_TRACE("watchdog stop path expired=%u", watchdog_expired_count(&test_watchdogs[tid]));
        K_ASSERT(watchdog_expired_count(&test_watchdogs[tid]) == 0);
        watchdog_delete(&test_watchdogs[tid]);
    }
})

/**
 * semaphore test
 */
static ksem_t sem_sync;
static ksem_t sem_excl;
static int add_times = 100000;
static int global_val = 0;

static int sem_test_setup(void) {
    sem_init(&sem_sync, 0);
    sem_init(&sem_excl, 1);
    global_val = 0;
    return 0;
}

static int sem_test_teardown(void) {
    sem_delete(&sem_sync);
    sem_delete(&sem_excl);
    return 0;
}

DEFINE_TEST_TASK(sem_test_task, {
    int prio = task_priority(task_self());
    task_set_priority(task_self(), prio + tid);

    if (tid > 0) {
        sem_take(&sem_sync, TIME_FOREVER_WAIT);
    } else {
        task_sleep(100);
        for (int i = 0; i < TEST_TASKS - 1; i++) {
            sem_give(&sem_sync);
        }
    }

    for (int ctr = 0; ctr < loops; ctr++) {
        sem_take(&sem_excl, TIME_FOREVER_WAIT);
        for (int i = 0; i < add_times; i++) {
            global_val++;
        }
        K_TRACE("task %d %d global_val %d", tid, ctr, global_val);
        K_ASSERT(global_val % add_times == 0);
        sem_give(&sem_excl);
    }

    // for (int ctr = 0; ctr < loops; ctr++) {
    //     sem_take(&sem_excl, TIME_NO_WAIT);
    //     for (int i = 0; i < add_times; i++) {
    //         global_val++;
    //     }
    //     K_TRACE("task %d %d global_val %d", tid, ctr, global_val);
    //     sem_give(&sem_excl);
    // }
})

/**
 * mutex test
 */
static kmutex_t test_mutex;

static int mutex_test_setup(void) {
    mutex_init(&test_mutex);
    global_val = 0;
    return 0;
}

static int mutex_test_teardown(void) {
    mutex_delete(&test_mutex);
    return 0;
}

DEFINE_TEST_TASK(mutex_test_task, {
    int prio = task_priority(task_self());
    task_set_priority(task_self(), prio + tid);

    for (int ctr = 0; ctr < loops; ctr++) {
        mutex_take(&test_mutex);
        for (int i = 0; i < add_times; i++) {
            global_val++;
        }
        K_TRACE("task %d %d global_val %d", tid, ctr, global_val);
        K_ASSERT(global_val % add_times == 0);
        mutex_give(&test_mutex);
    }
})

/**
 * message queue test
 */

static int msgq_buf[TEST_TASKS][4];
static kmsgq_t test_msgq[TEST_TASKS];

static int msgq_test_setup(void) {
    for (int i = 0; i < TEST_TASKS; i++) {
        msgq_init(&test_msgq[i], msgq_buf[i], sizeof(int), sizeof(msgq_buf[i]) / sizeof(int));
    }
    return 0;
}

static int msgq_test_teardown(void) {
    for (int i = 0; i < TEST_TASKS; i++) {
        msgq_delete(&test_msgq[i]);
    }
    return 0;
}

DEFINE_TEST_TASK(msgq_test_task, {
    int recv_msgs = 0;

    int prio = task_priority(task_self());
    task_set_priority(task_self(), prio + tid);

    for (int ctr = 0; ctr < loops; ctr++) {
        K_TRACE("task %d %d", tid, ctr);

        int to_tid = (tid + 1) % TEST_TASKS;
        msgq_give(&test_msgq[to_tid], &tid);

        int from_tid;
        msgq_take(&test_msgq[tid], &from_tid, TIME_FOREVER_WAIT);
        recv_msgs++;
        K_TRACE("task %d received message %d from task %d", tid, recv_msgs, from_tid);
    }
})

/**
 * pool test
 */

static int suspended_task(void *arg) {
    task_suspend();
    return 0;
}

static int suicided_task(void *arg) {
    task_exit();
    return 0;
}

DEFINE_TEST_TASK(pool_test_task, {
    int myprio = task_priority(task_self());
    ktask_t *t1 = task_new("Tp1", (task_func_t)suspended_task, (void *)NULL, myprio + 1, 0);
    ktask_t *t2 = task_new("Tp1", (task_func_t)suicided_task, (void *)NULL, myprio + 1, 0);
    task_delete(t1);
    task_delete(t2);
    t1 = task_new("Th1", (task_func_t)suspended_task, (void *)NULL, myprio + 1, KCONF_DEFAULT_TASK_STACK_SIZE + 1);
    t2 = task_new("Th2", (task_func_t)suicided_task, (void *)NULL, myprio + 1, KCONF_DEFAULT_TASK_STACK_SIZE + 1);
    task_delete(t1);
    task_delete(t2);
    K_TRACE("task %d done task_new test", tid);

    ktimer_t *timer = timer_new();
    timer_add_oneshot(timer, 100, (timer_func_t)task_exit, NULL);
    timer_delete(timer);
    K_TRACE("task %d done timer_new test", tid);

    ksem_t *sem = sem_new(1);
    sem_take(sem, TIME_FOREVER_WAIT);
    sem_give(sem);
    sem_delete(sem);
    K_TRACE("task %d done sem_new test", tid);

    kmutex_t *mutex = mutex_new();
    mutex_take(mutex);
    mutex_give(mutex);
    mutex_delete(mutex);
    K_TRACE("task %d done mutex_new test", tid);

    kmsgq_t *msgq = msgq_new(msgq_buf[tid], sizeof(int), sizeof(msgq_buf[tid]) / sizeof(int));
    msgq_give(msgq, &tid);
    msgq_take(msgq, &tid, TIME_FOREVER_WAIT);
    msgq_delete(msgq);
    K_TRACE("task %d done msgq_new test", tid);
})

// run test with setup and teardown functions if needed for each test case
static void run_test(const char *prompt, int (*test_setup)(void), task_func_t test_body, int (*test_teardown)(void)) {
    K_TRACE("%s", prompt);
    if (test_setup != NULL) {
        if (test_setup() != 0) {
            K_TRACE("[ FAILED ]  %s setup", prompt);
            return;
        }
    }
    for (size_t i = 0; i < TEST_TASKS; i++) {
        task_create(test_slave_names[i], test_body, (void *)i, PRIO_NORMAL, sizeof(test_slave_stacks[i]), test_slave_stacks[i], &test_slave_tcbs[i], 0);
    }
    for (size_t i = 0; i < TEST_TASKS; i++) {
        task_join(&test_slave_tcbs[i]);
    }
    if (test_teardown != NULL) {
        if (test_teardown() != 0) {
            K_TRACE("[ FAILED ]  %s teardown", prompt);
            return;
        }
    }
}

// master task to run all tests
static void test_master_task(void) {
    K_TRACE("\n\n === test start === \n");
    run_test("\n\n === start cooperative schedule test === \n", NULL, co_sched_task, NULL);
    run_test("\n\n === start preemptive schedule test === \n", NULL, preempt_sched_task, NULL);
    run_test("\n\n === start round-robin schedule test === \n", NULL, rr_sched_task, NULL);
    run_test("\n\n === start lock schedule test === \n", NULL, lock_sched_task, NULL);
    run_test("\n\n === start task state change test === \n", NULL, task_state_tast, NULL);
    run_test("\n\n === start timer test === \n", NULL, timer_test_task, NULL);
    run_test("\n\n === start tickless idle test === \n", NULL, tickless_test_task, NULL);
    run_test("\n\n === start software watchdog test === \n", NULL, watchdog_test_task, NULL);
    run_test("\n\n === start semaphore test === \n", sem_test_setup, sem_test_task, sem_test_teardown);
    run_test("\n\n === start mutex test === \n", mutex_test_setup, mutex_test_task, mutex_test_teardown);
    run_test("\n\n === start message queue test === \n", msgq_test_setup, msgq_test_task, msgq_test_teardown);
    run_test("\n\n === start pool test === \n", NULL, pool_test_task, NULL);
    K_TRACE("\n\n === all test done === \n");
}

// create master task to run all tests
void usr_init_hook(void) {
    task_create(test_master_name, (task_func_t)test_master_task, NULL, PRIO_NORMAL, sizeof(test_master_stack), test_master_stack, &test_master_tcb, 0);
}

// test entry
void rtos_test(void) {
    kern_start();
}
