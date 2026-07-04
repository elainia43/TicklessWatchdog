#include <errno.h>
#include <sys/stat.h>

#include "stm32f4xx_hal.h"

/* File descriptor is always stdout (1) */
int _close_r(struct _reent *r, int file) {
    return 0;
}

/* Seek always fails with ESPIPE (illegal seek) */
off_t _lseek_r(struct _reent *r, int file, off_t ptr, int dir) {
    return -1;
}

/* Read from file descriptor (stdin), returns EOF */
int _read_r(struct _reent *r, int file, char *ptr, int len) {
    return -1; /* Return EOF */
}

extern UART_HandleTypeDef huart2;
/* Write to file descriptor (stdout), sends characters out */
int _write_r(struct _reent *r, int file, const char *ptr, int len) {
    // Send character pointed to by 'ptr' to standard output
    HAL_UART_Transmit(&huart2, (uint8_t *)ptr, len, HAL_MAX_DELAY);
    return len; /* Return number of bytes written */
}

/* Provide minimum implementation of isatty */
int _isatty_r(struct _reent *r, int file) {
    return 1; /* Always a terminal */
}

/* Provide minimum implementation of fstat */
int _fstat_r(struct _reent *r, int file, struct stat *st) {
    st->st_mode = S_IFCHR;
    return 0;
}

/* Provide minimum implementation of sbrk (used by malloc) */
caddr_t _sbrk_r(struct _reent *r, int incr) {
    extern char _heap_start; /* Defined by the linker */
    extern char _heap_end;   /* Defined by the linker */
    static char *heap_end;
    char *prev_heap_end;

    if (heap_end == 0) {
        heap_end = &_heap_start;
    }
    prev_heap_end = heap_end;

    if (heap_end + incr > &_heap_end) {
        errno = ENOMEM; /* Out of memory */
        return (caddr_t)-1;
    }

    heap_end += incr;

    return (caddr_t)prev_heap_end;
}

extern int cpu_irq_save(void);
extern void cpu_irq_restore(int state);

static int irqstate = 0;
// Locks the malloc function.
void __malloc_lock(struct _reent *_r) {
    irqstate = cpu_irq_save();
}

// Unlocks the malloc function.
void __malloc_unlock(struct _reent *_r) {
    cpu_irq_restore(irqstate);
}
