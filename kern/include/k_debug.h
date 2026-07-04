#ifndef _K_DEBUG_H_
#define _K_DEBUG_H_

#include <stdio.h>

#include "k_config.h"

// Debug print macro
#if KCONF_DEBUG_ENABLED
extern void assert_fail(const char *assertion, const char *file, unsigned int line, const char *function);

#define K_ASSERT(expr)                                        \
    do {                                                      \
        if (!(expr)) {                                        \
            assert_fail(#expr, __FILE__, __LINE__, __func__); \
        }                                                     \
    } while (0)

#define K_PRINTF(...)        \
    do {                     \
        printf(__VA_ARGS__); \
    } while (0)

#else  // KCONF_DEBUG_ENABLED

#define K_PRINTF(...) ((void)0)

#endif  // KCONF_DEBUG_ENABLED

// Local debug print macro
#ifdef LOCAL_DEBUG

extern uint32_t HAL_GetTick(void);
#define _K_TRACE(fmt, ...)                                                                                                                            \
    do {                                                                                                                                              \
        sched_lock();                                                                                                                                 \
        printf("[%lu %s/%d] %s/%d: " fmt "\n", time_now_ticks(), task_name(NULL), task_priority(NULL), __PRETTY_FUNCTION__, __LINE__, ##__VA_ARGS__); \
        sched_unlock();                                                                                                                               \
    } while (0)

#define K_TRACE(...) _K_TRACE(__VA_ARGS__)
#else

#define K_TRACE(...) ((void)0)

#endif  // LOCAL_DEBUG

#define PANIC(...)                                          \
    do {                                                    \
        printf(__VA_ARGS__);                                \
        assert_fail("panic", __FILE__, __LINE__, __func__); \
    } while (0)

#endif  // _K_DEBUG_H_