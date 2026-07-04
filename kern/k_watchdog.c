// #define LOCAL_DEBUG

#include "k_kern.h"

#if KCONF_WATCHDOG_ENABLED

#define WATCHDOG_ON_LIST_FLAG (1 << KOBJ_FLAG_TYPE_OFFSET)
#define WATCHDOG_IS_ON_LIST(wd) KOBJ_FLAG_CHECK((wd), WATCHDOG_ON_LIST_FLAG)
#define WATCHDOG_ON_LIST(wd) KOBJ_FLAG_SET((wd), WATCHDOG_ON_LIST_FLAG)
#define WATCHDOG_OFF_LIST(wd) KOBJ_FLAG_CLEAR((wd), WATCHDOG_ON_LIST_FLAG)

typedef TAILQ_HEAD(_watchdogq_t, kwatchdog) watchdogq_t;

static watchdogq_t watchdog_list = TAILQ_HEAD_INITIALIZER(watchdog_list);
static int watchdog_list_initialized = 0;

DEFINE_POOL(watchdog_pool, kwatchdog_t, KCONF_WATCHDOG_MAX_COUNT);

static void watchdog_ensure_init(void) {
    if (!watchdog_list_initialized) {
        TAILQ_INIT(&watchdog_list);
        watchdog_list_initialized = 1;
    }
}

static void watchdog_insert(kwatchdog_t *wd) {
    if (!WATCHDOG_IS_ON_LIST(wd)) {
        TAILQ_INSERT_TAIL(&watchdog_list, wd, wd_node);
        WATCHDOG_ON_LIST(wd);
        wd->on_list = 1;
    }
}

static void watchdog_remove(kwatchdog_t *wd) {
    if (WATCHDOG_IS_ON_LIST(wd)) {
        TAILQ_REMOVE(&watchdog_list, wd, wd_node);
        WATCHDOG_OFF_LIST(wd);
        wd->on_list = 0;
    }
}

kwatchdog_t *watchdog_new(void) {
    kwatchdog_t *wd = pool_alloc(&watchdog_pool);
    if (wd == NULL) {
        return NULL;
    }

    if (watchdog_init(wd, NULL, 0) != K_OK) {
        pool_free(&watchdog_pool, wd);
        return NULL;
    }

    KOBJ_FLAG_SET(wd, KOBJ_FLAG_POOL_ALLOC);
    return wd;
}

kstatus_t watchdog_init(kwatchdog_t *wd, ktask_t *owner, kticks_t timeout_ticks) {
    if (wd == NULL) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        watchdog_ensure_init();
        memset(wd, 0, sizeof(kwatchdog_t));
        KOBJ_INIT(wd, MAGIC_WATCHDOG, 0);
        wd->owner = owner;
        wd->timeout_ticks = timeout_ticks;
        wd->last_feed_tick = time_now_ticks();
        wd->expired_count = 0;
        wd->active = 0;
        wd->on_list = 0;
        watchdog_insert(wd);
    }

    return K_OK;
}

kstatus_t watchdog_start(kwatchdog_t *wd) {
    if (!KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG) || wd->timeout_ticks == 0) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        watchdog_ensure_init();
        wd->last_feed_tick = time_now_ticks();
        wd->active = 1;
        watchdog_insert(wd);
    }

    return K_OK;
}

kstatus_t watchdog_feed(kwatchdog_t *wd) {
    if (!KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG)) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        wd->last_feed_tick = time_now_ticks();
    }

    return K_OK;
}

kstatus_t watchdog_stop(kwatchdog_t *wd) {
    if (!KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG)) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        wd->active = 0;
    }

    return K_OK;
}

kstatus_t watchdog_delete(kwatchdog_t *wd) {
    if (!KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG)) {
        return K_EINVAL;
    }

    WITHIN_CRITICAL() {
        watchdog_remove(wd);
        wd->active = 0;
        KOBJ_MAGIC_CLEAR(wd);

        if (KOBJ_FLAG_CHECK(wd, KOBJ_FLAG_POOL_ALLOC)) {
            pool_free(&watchdog_pool, wd);
        }
    }

    return K_OK;
}

void watchdog_scan(void) {
    kwatchdog_t *wd;
    kticks_t now = time_now_ticks();

    watchdog_ensure_init();

    TAILQ_FOREACH(wd, &watchdog_list, wd_node) {
        if (KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG) && wd->active && wd->timeout_ticks > 0) {
            if (TIME_GE(now, wd->last_feed_tick + wd->timeout_ticks)) {
                wd->expired_count++;
                wd->last_feed_tick = now;
                K_TRACE("watchdog timeout: task=%s expired=%u", wd->owner ? task_name(wd->owner) : "unknown", wd->expired_count);
            }
        }
    }
}

unsigned int watchdog_expired_count(kwatchdog_t *wd) {
    unsigned int count = 0;

    if (!KOBJ_MAGIC_CHECK(wd, MAGIC_WATCHDOG)) {
        return 0;
    }

    WITHIN_CRITICAL() {
        count = wd->expired_count;
    }

    return count;
}

#else

kwatchdog_t *watchdog_new(void) {
    return NULL;
}

kstatus_t watchdog_init(kwatchdog_t *wd, ktask_t *owner, kticks_t timeout_ticks) {
    (void)wd;
    (void)owner;
    (void)timeout_ticks;
    return K_ENOTSUP;
}

kstatus_t watchdog_start(kwatchdog_t *wd) {
    (void)wd;
    return K_ENOTSUP;
}

kstatus_t watchdog_feed(kwatchdog_t *wd) {
    (void)wd;
    return K_ENOTSUP;
}

kstatus_t watchdog_stop(kwatchdog_t *wd) {
    (void)wd;
    return K_ENOTSUP;
}

kstatus_t watchdog_delete(kwatchdog_t *wd) {
    (void)wd;
    return K_ENOTSUP;
}

void watchdog_scan(void) {
}

unsigned int watchdog_expired_count(kwatchdog_t *wd) {
    (void)wd;
    return 0;
}

#endif
