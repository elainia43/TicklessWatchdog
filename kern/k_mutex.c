#include "k_kern.h"

DEFINE_POOL(mutex_pool, kmutex_t, KCONF_MUTEX_MAX_COUNT);

// create a new mutex
kmutex_t *mutex_new(void) {
    kmutex_t *mutex = pool_alloc(&mutex_pool);
    if (mutex == NULL) {
        return NULL;
    }
    if (mutex_init(mutex) != K_OK) {
        pool_free(&mutex_pool, mutex);
        return NULL;
    } else {
        KOBJ_FLAG_SET(mutex, KOBJ_FLAG_POOL_ALLOC);
    }
    return mutex;
}

// initialize mutex
kstatus_t mutex_init(kmutex_t *mutex) {
    KOBJ_INIT(mutex, MAGIC_MUTEX, 0);
    mutex->owner = NULL;
    mutex->take_count = 0;
    mutex->original_prio = 0;
    wait_queue_init(&mutex->wait_queue);
    return K_OK;
}

// delete mutex
kstatus_t mutex_delete(kmutex_t *mutex) {
    K_ASSERT(KOBJ_MAGIC_CHECK(mutex, MAGIC_MUTEX));

    kstatus_t status = K_OK;

    WITHIN_CRITICAL() {
        KOBJ_MAGIC_CLEAR(mutex);
        status = sched_wakeup_all(&mutex->wait_queue, K_WAIT_DELETED);  // wake up all tasks waiting for this mutex

        if (KOBJ_FLAG_CHECK(mutex, KOBJ_FLAG_POOL_ALLOC)) {
            pool_free(&mutex_pool, mutex);
        }
    }

    return status;
}

// try to take mutex without waiting (return K_OK if success, K_ERROR if failed)
kstatus_t mutex_try_take(kmutex_t *mutex) {
    K_ASSERT(KOBJ_MAGIC_CHECK(mutex, MAGIC_MUTEX));
    ktask_t *self = task_self();

    if (mutex->owner == NULL) {                              // case 1. mutex available
        mutex->owner = self;                                 // mark mutex taken by me
        mutex->original_prio = task_priority(mutex->owner);  // save the original priority of the current task
        K_ASSERT(mutex->take_count == 0);
        mutex->take_count = 1;
        return K_OK;
    } else if (mutex->owner == self) {  // case 2. mutex already taken by me
        K_ASSERT(mutex->take_count > 0);
        mutex->take_count++;
        return K_OK;
    } else {  // case 3. mutex unavailable
        int myprio = task_priority(self);
        int ownerprio = task_priority(mutex->owner);
        if (PRIO_LOWER(ownerprio, myprio)) {          // priority inversion hanppens
            task_set_priority(mutex->owner, myprio);  // inherit the priority of the current high-priority task
        }
        return K_EBUSY;
    }
}

// take mutex
kstatus_t mutex_take(kmutex_t *mutex) {
    K_ASSERT(KOBJ_MAGIC_CHECK(mutex, MAGIC_MUTEX));
    K_ASSERT(!cpu_in_irq());

    // take mutex or wait until mutex available or deleted
    kstatus_t status = SCHED_WAIT_CONDITION_OR_TIMEOUT(&mutex->wait_queue, TIME_FOREVER_WAIT, mutex_try_take(mutex));

    return status;
}

// give mutex
kstatus_t mutex_give(kmutex_t *mutex) {
    K_ASSERT(KOBJ_MAGIC_CHECK(mutex, MAGIC_MUTEX));
    K_ASSERT(mutex->take_count > 0);
    K_ASSERT(mutex->owner == task_self());

    kstatus_t status = K_OK;

    WITHIN_CRITICAL() {
        mutex->take_count--;
        if (mutex->take_count == 0) {
            mutex->owner = NULL;                                    // mark mutex available
            task_set_priority(mutex->owner, mutex->original_prio);  // restore the original priority of the current task
            mutex->original_prio = 0;
            status = sched_wakeup(&mutex->wait_queue, K_OK);  // wake up the waiting task (if any)
        }
    }
    return status;
}
