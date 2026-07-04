#ifndef _K_CONFIG_H_
#define _K_CONFIG_H_

// Define your configuration options here

// #define KCONF_COROUTINE_ENABLED 1  // Enable coroutine support

#define KCONF_DEBUG_ENABLED 1  // Enable debug output

#define KCONF_KERN_STATS_ENABLED 1  // Enable kernel statistics collection

#define KCONF_PRIO_HIGHEST 31  // Highest priority level (0-31)

#define KCONF_DEFAULT_TASK_STACK_SIZE 512  // Default task stack size in words

#define KCONF_TICKS_PER_SECOND 1000  // Kernel heartbeat HZ (ticks per second)

#define KCONF_TICKLESS_ENABLED 1          // Enable tickless idle decision/statistics framework
#define KCONF_TICKLESS_MAX_IDLE_TICKS 1000 // Maximum predicted idle window in one decision

#define KCONF_WATCHDOG_ENABLED 1       // Enable software watchdog module
#define KCONF_WATCHDOG_MAX_COUNT 8     // Maximum number of watchdog objects

#define KCONF_POOL_MAX_OBJECTS 128  // Maximum number of objects in one pool
#define KCONF_TASK_MAX_COUNT 8      // Maximum number of tasks in the system
#define KCONF_TIMER_MAX_COUNT 16    // Maximum number of timers in the system
#define KCONF_SEM_MAX_COUNT 16      // Maximum number of semaphores in the system
#define KCONF_MUTEX_MAX_COUNT 16    // Maximum number of mutexes in the system
#define KCONF_MSGQ_MAX_COUNT 16     // Maximum number of message queues in the system

#endif  // _K_CONFIG_H_
