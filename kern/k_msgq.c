#include "k_kern.h"

DEFINE_POOL(msgq_pool, kmsgq_t, KCONF_MSGQ_MAX_COUNT);

// create a new message queue
kmsgq_t *msgq_new(void *buffer, int msg_size, unsigned int max_num) {
    kmsgq_t *msgq = pool_alloc(&msgq_pool);
    if (msgq == NULL) {
        return NULL;
    }
    if (msgq_init(msgq, buffer, msg_size, max_num) != K_OK) {
        pool_free(&msgq_pool, msgq);
        return NULL;
    } else {
        KOBJ_FLAG_SET(msgq, KOBJ_FLAG_POOL_ALLOC);
    }
    return msgq;
}

// initialize message queue
kstatus_t msgq_init(kmsgq_t *msgq, void *buffer, int msg_size, unsigned int max_num) {
    KOBJ_INIT(msgq, MAGIC_MSGQ, 0);
    msgq->msg_size = msg_size;
    msgq->max_num = max_num;
    msgq->free_num = max_num;
    msgq->buffer_start = buffer;
    msgq->buffer_end = (char *)(buffer) + (msg_size * max_num);
    msgq->read_ptr = (char *)(buffer);
    msgq->write_ptr = (char *)(buffer);
    wait_queue_init(&msgq->wait_queue);
    return K_OK;
}

// delete message queue
kstatus_t msgq_delete(kmsgq_t *msgq) {
    K_ASSERT(KOBJ_MAGIC_CHECK(msgq, MAGIC_MSGQ));

    kstatus_t status = K_OK;

    WITHIN_CRITICAL() {
        KOBJ_MAGIC_CLEAR(msgq);
        status = sched_wakeup_all(&msgq->wait_queue, K_WAIT_DELETED);

        if (KOBJ_FLAG_CHECK(msgq, KOBJ_FLAG_POOL_ALLOC)) {
            pool_free(&msgq_pool, msgq);
        }
    }

    return status;
}

// get number of free messages slot in message queue
int msgq_get_free(kmsgq_t *msgq) {
    return msgq->free_num;
}

// try to take a message from the message queue. Return K_OK if success, K_ERROR if failed.
kstatus_t msgq_try_take(kmsgq_t *msgq, void *message) {
    K_ASSERT(KOBJ_MAGIC_CHECK(msgq, MAGIC_MSGQ));
    if (msgq->free_num < msgq->max_num && message != NULL) {
        // take first available message from queue
        if (message) {
            memcpy(message, msgq->read_ptr, msgq->msg_size);
        }
        msgq->read_ptr += msgq->msg_size;
        if (msgq->read_ptr == msgq->buffer_end) {
            msgq->read_ptr = msgq->buffer_start;
        }
        ++msgq->free_num;
        return K_OK;
    }
    return K_EBUSY;
}

// get a message from message queue, wait 'timeout' ticks if message queue is empty
kstatus_t msgq_take(kmsgq_t *msgq, void *message, kticks_t timeout) {
    K_ASSERT(KOBJ_MAGIC_CHECK(msgq, MAGIC_MSGQ));
    K_ASSERT(!((timeout != TIME_NO_WAIT) && cpu_in_irq()));  // can't wait in irq

    // take message from queue or wait until message available or timeout or msgq deleted
    kstatus_t status = SCHED_WAIT_CONDITION_OR_TIMEOUT(&msgq->wait_queue, timeout, msgq_try_take(msgq, message));

    return status;
}

// put a message into message queue, wait 'timeout' ticks if message queue is full
kstatus_t msgq_give(kmsgq_t *msgq, const void *message) {
    K_ASSERT(message != NULL);
    K_ASSERT(KOBJ_MAGIC_CHECK(msgq, MAGIC_MSGQ));

    kstatus_t status = K_OK;

    // try to put a message until success or timeout
    WITHIN_CRITICAL() {
        if (msgq->free_num > 0) {  // have space, put it
            // put message in queue
            if (message) {
                memcpy(msgq->write_ptr, message, msgq->msg_size);
            }
            msgq->write_ptr += msgq->msg_size;
            if (msgq->write_ptr == msgq->buffer_end) {
                msgq->write_ptr = msgq->buffer_start;
            }
            --msgq->free_num;
        }
        // wake up task waiting for message
        status = sched_wakeup(&msgq->wait_queue, K_OK);
    }

    return status;
}
