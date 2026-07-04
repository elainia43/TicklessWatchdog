#include "k_kern.h"

kernel_state_t kernel_state;

typedef struct task_pool_block {
    ktask_t task;
    kstack_t stack[KCONF_DEFAULT_TASK_STACK_SIZE];
} task_pool_block_t;

DEFINE_POOL(task_pool, task_pool_block_t, KCONF_TASK_MAX_COUNT);

// free a task control block and its stack memory
static void free_task_mem(ktask_t *task) {
    if (KOBJ_FLAG_CHECK(task, KOBJ_FLAG_POOL_ALLOC)) {
        K_ASSERT(task != task_self());
        pool_free(&task_pool, task);
    } else if (KOBJ_FLAG_CHECK(task, KOBJ_FLAG_HEAP_ALLOC)) {
        K_ASSERT(task != task_self());
        k_free(task);
    }
}

// create a new task with it's TCB & stack memory dynamically allocated
ktask_t *task_new(const char *name, task_func_t entry, void *arg, int prio, size_t stack_bytes) {
    task_pool_block_t *tb;
    unsigned int flag;

    if (stack_bytes > sizeof(tb->stack)) {
        // stack size too large for pool, allocate from heap for both TCB and stack
        tb = k_malloc(sizeof(ktask_t) + stack_bytes);
        flag = KOBJ_FLAG_HEAP_ALLOC;
    } else {
        tb = pool_alloc(&task_pool);
        stack_bytes = sizeof(tb->stack);
        flag = KOBJ_FLAG_POOL_ALLOC;
    }

    if (tb == NULL) {
        return NULL;
    }

    if (task_create(name, entry, arg, prio, stack_bytes, tb->stack, &tb->task, flag) != K_OK) {
        pool_free(&task_pool, tb);
        return NULL;
    } else {
        return &tb->task;
    }
}

// ensure the task is deleted
static void task_guard(ktask_t *task) {
    if (task == NULL) {
        return;
    }

    // enable interrupt for each task and exit kernel critical section
    cpu_irq_enable();

    if (task->entry) {
        (*task->entry)(task->arg);
    }
    task_exit();
}

// create a new task
kstatus_t task_create(const char *name, task_func_t entry, void *arg, int prio, size_t stack_bytes, kstack_t *stack_base, ktask_t *task, unsigned int flag) {
    if (task == NULL || stack_base == NULL || stack_bytes < sizeof(task_stack_frame_t) || entry == NULL || prio < PRIO_LOWEST || prio > PRIO_HIGHEST || name == NULL) {
        return K_EINVAL;
    }

    size_t stack_size = stack_bytes / sizeof(kstack_t);
    // Initialize the task control block
    memset(task, 0, sizeof(ktask_t));

    KOBJ_INIT(task, MAGIC_TASK, flag);

    task->stack_base = stack_base;
    task->stack_end = stack_base + stack_size;
    strlcpy(task->name, name, sizeof(task->name));
    // snprintf(task->name, sizeof(task->name), "%s#%p", name, task);  // unique name
    task->entry = entry;
    task->arg = arg;
    task->prio = prio;

    task->state = TASK_DEAD;
    task->wait_on = NULL;
    task->wait_result = K_OK;
    wait_queue_init(&task->join_queue);

    TAILQ_INSERT_TAIL(&kernel_state.task_list, task, task_node);

    // create an init stack frame to simulate the context of the task
    task->context = cpu_task_stack_prepair(task->stack_end, (function_ptr_t)task_guard, task);

    task_create_hook(task);

    WITHIN_CRITICAL() {
        KSTAT_INC(task_create);
        sched_change_state_to(task, TASK_READY);
        sched_preempt(task, __func__);
    }

    return K_OK;
}

static void handle_zombie_task(ktask_t *task) {
    if (kernel_state.zombie_task) {  // delete last zombie task
        free_task_mem(kernel_state.zombie_task);
        kernel_state.zombie_task = NULL;
    }
    if (task == task_self()) {  // save current task as zombie
        kernel_state.zombie_task = task;
    } else {  // cleanup task immediately
        free_task_mem(task);
        kernel_state.zombie_task = NULL;
    }
}

// delete a task
kstatus_t task_delete(ktask_t *task) {
    if (NULL == task) {
        task = task_self();  // delete self
    }

    if (kernel_state.idle_task == task || !KOBJ_MAGIC_CHECK(task, MAGIC_TASK)) {  // idle task can't be deleted
        return K_EINVAL;
    }

    task_delete_hook(task);

    WITHIN_CRITICAL() {
        sched_change_state_to(task, TASK_DEAD);

        TAILQ_REMOVE(&kernel_state.task_list, task, task_node);
        KOBJ_MAGIC_CLEAR(task);
        KSTAT_INC(task_delete);

        handle_zombie_task(task);

        sched_wakeup_all(&task->join_queue, K_WAIT_DELETED);

        sched_yield(__func__);
    }

    return K_OK;
}

// exit current task
kstatus_t task_exit(void) {
    task_delete(kernel_state.current_task);
    K_ASSERT(0 && "Never reach here: task should be deleted!");
    return K_OK;
}

// yield current task
kstatus_t task_yield(void) {
    sched_yield(__func__);
    return K_OK;
}

// get current task control block
ktask_t *task_self(void) {
    return kernel_state.current_task;
}

// get task name
const char *task_name(ktask_t *task) {
    if (task == NULL) {
        task = task_self();
    }
    if (task == NULL /* || task->name == NULL*/) {
        return "null";
    }
    return task->name;
}

// get task priority
int task_priority(ktask_t *task) {
    if (task == NULL) {
        task = task_self();
    }
    return task->prio;
}

// set task priority
kstatus_t task_set_priority(ktask_t *task, int new_prio) {
    if (task == NULL) {
        task = task_self();
    }
    return sched_change_priority(task, new_prio);
}

// join a task: current task will wait for another task to complete its execution before proceeding further.
kstatus_t task_join(ktask_t *task) {
    if (!KOBJ_MAGIC_CHECK(task, MAGIC_TASK)) {
        return K_EINVAL;
    }
    if (task == kernel_state.current_task || task->wait_on == &task->join_queue) {
        return K_EDEADLOCK;
    }
    if (task->state == TASK_DEAD) {
        return K_OK;
    }

    return sched_wait_on(&task->join_queue, __func__);
}

// suspend current task
kstatus_t task_suspend(void) {
    return sched_wait();
}

// resume a task to run
kstatus_t task_resume(ktask_t *task) {
    if (!KOBJ_MAGIC_CHECK(task, MAGIC_TASK)) {
        return K_EINVAL;
    }

    kstatus_t status = K_OK;
    WITHIN_CRITICAL() {
        if (task->wait_on) {
            K_ASSERT(task->state == TASK_BLOCKED);
            status = sched_wakeup_task(task, K_OK);
        }
    }
    return status;
}

// sleep/delay current task for 'ticks' ticks
kstatus_t task_sleep(kticks_t ticks) {
    return sched_wait_timeout(&kernel_state.wait_queue, ticks, NULL, __func__);
}

// sleep/delay current task for 'ms' milliseconds
kstatus_t task_msleep(unsigned long ms) {
    kticks_t tick;

    tick = time_ms_to_ticks(ms);

    return task_sleep(tick);
}

// body of idle task
static void idle_task(void) {
    while (1) {
        tickless_idle_enter();
        task_idle_hook();
        task_yield();
    }
}

// convert boot task to idle task
static void boot_task_become_idle(void) {
    kernel_state.idle_task = task_self();
    task_set_priority(kernel_state.idle_task, PRIO_LOWEST);
    idle_task();
}

// bootstrap task: used to initialize user applications in task context
static void bootstrap_task(void) {
    // call user init functions
    usr_init_hook();

    // convert boot task to idle task after kernel init
    boot_task_become_idle();
}

#define IDLE_TASK_STACK_SIZE KCONF_DEFAULT_TASK_STACK_SIZE
static ktask_t boot_task_cb;
static kstack_t boot_task_stack[IDLE_TASK_STACK_SIZE];

// initialize task library
void k_task_init(void) {
    TAILQ_INIT(&kernel_state.task_list);
    task_create("bootstrap_task", (task_func_t)bootstrap_task, NULL, PRIO_HIGHEST, sizeof(boot_task_stack), boot_task_stack, &boot_task_cb, 0);
}
