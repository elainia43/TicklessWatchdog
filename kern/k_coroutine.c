#include <stdint.h>
#include <stdio.h>

#include "k_port.h"

#ifdef KCONF_COROUTINE_ENABLED

// a very simple coroutine implementation

typedef struct circq {
    struct circq *next;  // next element
    struct circq *prev;  // previous element
} circq_t;

// Circular queue definitions.
#define CIRCQ_INIT(elem)       \
    do {                       \
        (elem)->next = (elem); \
        (elem)->prev = (elem); \
    } while (0)

#define CIRCQ_INSERT(elem, list)     \
    do {                             \
        (elem)->prev = (list)->prev; \
        (elem)->next = (list);       \
        (list)->prev->next = (elem); \
        (list)->prev = (elem);       \
    } while (0)

#define CIRCQ_REMOVE(elem)                 \
    do {                                   \
        (elem)->next->prev = (elem)->prev; \
        (elem)->prev->next = (elem)->next; \
        ((elem)->prev = 0);                \
        ((elem)->next = 0);                \
    } while (0)

#define CIRCQ_FIRST(elem) ((elem)->next)

#define CIRCQ_EMPTY(elem) (CIRCQ_FIRST(elem) == (elem))

typedef int (*co_func_t)(void *);

typedef struct coroutine {
    struct circq link;                              // link to other coroutines
    const char *name;                               // coroutine name
    struct task_stack_frame *context;               // coroutine's cpu context on switch
    kstack_t stack[KCONF_DEFAULT_TASK_STACK_SIZE];  // coroutine's stack
} co_t;

static co_t main_co;      // main coroutine
static co_t *current_co;  // current coroutine

#define CQ(co) (&(co)->link)

// get the coroutine from the circq
static inline co_t *co_from_circq(struct circq *p) {
    return ((co_t *)(p));
}

// external functions
void cpu_sched_request(void);
kstack_t *sched_switch_coroutine_stack(kstack_t *sp);

// create a coroutine and add it to the circular queue for scheduling
co_t *co_create(co_t *co, const char *name, co_func_t func, void *arg) {
    co->name = name;
    kstack_t *stack_top = co->stack + sizeof(co->stack) / sizeof(kstack_t);
    co->context = cpu_task_stack_prepair(stack_top, func, arg);
    CIRCQ_INSERT(CQ(co), CQ(&main_co));
    return co;
}

// call pendsv to yield/switch to next coroutine
void co_yield (void) {
    cpu_sched_request();
}

// delete the coroutine and switch to the next coroutine
void co_delete(co_t *co) {
    CIRCQ_REMOVE(CQ(co));
    co_yield ();
}

// init coroutine library
void co_init(void) {
    // create and initialize the main coroutine
    CIRCQ_INIT(CQ(&main_co));
    current_co = &main_co;  // set the main coroutine as the current coroutine, main_co.context = main stack pointer
}

// the main loop to switch between coroutines
void co_loop(void) {
    while (!CIRCQ_EMPTY(CQ(current_co))) {
        co_yield ();
    }
}

// switch coroutine stack pointer, called in the pendsv handler
kstack_t *sched_switch_coroutine_stack(kstack_t *sp) {
    current_co->context = (struct task_stack_frame *)sp;      // save the current stack pointer
    current_co = co_from_circq(CIRCQ_FIRST(CQ(current_co)));  // switch to the next coroutine
    if (current_co == NULL) {
        current_co = &main_co;
    }
    return (kstack_t *)current_co->context;  // return the new stack pointer
}

/*******************************************************************/

// test coroutine
int co_func(void *arg) {
    int *value = (int *)arg;
    for (int i = 0; i < 5; i++) {
        printf("Coroutine %s: %d\n", current_co->name, *value);
        (*value)++;
        co_yield ();
    }

    // should delete itself
    co_delete(current_co);
    return 0;
}

// coroutine test
void co_test(void) {
    static co_t co1, co2, co3;
    int value1 = 100, value2 = 200, value3 = 300;

    co_init();
    co_create(&co1, "co1", co_func, (void *)&value1);
    co_create(&co2, "co2", co_func, (void *)&value2);
    co_create(&co3, "co3", co_func, (void *)&value3);
    co_loop();

    while (1)
        ;
}

#endif  // KCONF_COROUTINE_ENABLED