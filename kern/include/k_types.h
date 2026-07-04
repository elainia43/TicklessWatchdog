#ifndef _K_TYPES_H_
#define _K_TYPES_H_

#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "k_config.h"

/*
********************************************************************************
*                           Kernel Types
********************************************************************************
*/

typedef int kbool_t;
#define K_TRUE 1
#define K_FALSE 0

// typedef int kstatus_t;

// kernel function return status
typedef enum {
    K_OK = 0u,       // success
    K_ERROR = (-1),  // unspecified general error

    K_EINVAL = (-2),     // invalid argument
    K_EAGAIN = (-3),     // try again
    K_EBUSY = (-4),      // resource busy or not available
    K_EDEADLOCK = (-5),  // deadlock
    K_ENOMEM = (-6),     // out of memory
    K_EOVERFLOW = (-7),  // overflow
    K_EISRCTX = (-8),    // not allowed in ISR context
    K_ENOTSUP = (-9),    // operation not supported

    K_WAIT_BLOCKING = (-10),  // wait blocking
    K_WAIT_TIMEOUT = (-11),   // wait timeout
    K_WAIT_DELETED = (-12),   // wait object deleted
    K_WAIT_ABORTED = (-13),   // wait aborted

} kstatus_t;


// kernel time in ticks
typedef unsigned long kticks_t;
typedef unsigned long long kticks64_t;

// function pointer type
typedef int (*function_ptr_t)(void* arg);

// Task control block: Structure representing a kernel task.
typedef struct ktask ktask_t;
// Function pointer type for a task function.
typedef function_ptr_t task_func_t;
// Function pointer type for an ISR (Interrupt Service Routine) function.
typedef function_ptr_t isr_func_t;
// Function pointer type for a timer callback function.
typedef function_ptr_t timer_func_t;

#endif  // _K_TYPES_H_