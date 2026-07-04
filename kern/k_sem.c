#define LOCAL_DEBUG

#include "k_kern.h"

DEFINE_POOL(sem_pool, ksem_t, KCONF_SEM_MAX_COUNT);

// create a new semaphore
ksem_t *sem_new(int count) {
    ksem_t *sem = pool_alloc(&sem_pool);
    if (sem == NULL) {
        return NULL;
    }
    if (sem_init(sem, count) != K_OK) {
        pool_free(&sem_pool, sem);
        return NULL;
    } else {
        KOBJ_FLAG_SET(sem, KOBJ_FLAG_POOL_ALLOC);
    }
    return sem;
}

// initialize semaphore
kstatus_t sem_init(ksem_t *sem, int count) {
    KOBJ_INIT(sem, MAGIC_SEMAPHORE, 0);

    sem->count = count;
    wait_queue_init(&sem->wait_queue);
    return K_OK;
}

// delete semaphore
kstatus_t sem_delete(ksem_t *sem) {
    K_ASSERT(KOBJ_MAGIC_CHECK(sem, MAGIC_SEMAPHORE));

    kstatus_t status = K_OK;

    WITHIN_CRITICAL() {
        KOBJ_MAGIC_CLEAR(sem);
        status = sched_wakeup_all(&sem->wait_queue, K_WAIT_DELETED);

        if (KOBJ_FLAG_CHECK(sem, KOBJ_FLAG_POOL_ALLOC)) {
            pool_free(&sem_pool, sem);
        }
    }

    return status;
}

// try to take semaphore without waiting (return K_OK if success, K_ERROR if failed)
kstatus_t sem_try_take(ksem_t *sem) {
    K_ASSERT(KOBJ_MAGIC_CHECK(sem, MAGIC_SEMAPHORE));

    if (sem->count > 0) {
        sem->count--;
        return K_OK;
    }
    return K_EBUSY;
}

// take semaphore
// timeout: 0:try take, -1:wait forever, else: wait 'timeout' ticks
kstatus_t sem_take(ksem_t *sem, kticks_t timeout) {
    K_ASSERT(KOBJ_MAGIC_CHECK(sem, MAGIC_SEMAPHORE));
    K_ASSERT(sem->count >= 0);
    K_ASSERT(!((timeout != TIME_NO_WAIT) && cpu_in_irq()));  // can't wait in irq

    // take semaphore or wait until semaphore available or timeout or sem deleted
    kstatus_t status = SCHED_WAIT_CONDITION_OR_TIMEOUT(&sem->wait_queue, timeout, sem_try_take(sem));
    K_ASSERT(sem->count >= 0);

    return status;
}

// give semaphore
kstatus_t sem_give(ksem_t *sem) {
    K_ASSERT(KOBJ_MAGIC_CHECK(sem, MAGIC_SEMAPHORE));
    K_ASSERT(sem->count >= 0);

    kstatus_t status = K_OK;

    WITHIN_CRITICAL() {
        sem->count++;  // increase resource count
        if (sem->count > 0) {
            status = sched_wakeup(&sem->wait_queue, K_OK);  // may no task is waiting...
        }
    }

    return status;
}
