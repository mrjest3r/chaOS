#ifndef TASK_H
#define TASK_H

#include <stdint.h>

typedef enum {
    TASK_UNUSED = 0, /* free slot                                   */
    TASK_READY,      /* runnable, waiting for the CPU               */
    TASK_RUNNING,    /* currently on the CPU                        */
    TASK_SLEEPING,   /* blocked until 'sleep_until' ticks           */
    TASK_DEAD        /* finished; slot reclaimable                  */
} task_state_t;

typedef struct task {
    uint32_t esp;              /* saved kernel stack pointer (first field)   */
    uint32_t kstack;           /* base of kernel-stack allocation (to free)  */
    uint32_t kstack_top;       /* top of kernel stack -> loaded into TSS.esp0*/
    uint32_t ustack;           /* base of user-stack allocation (0 if kernel)*/
    void (*entry)();           /* task entry point                           */
    int id;
    int is_user;               /* 1 = runs in ring 3                         */
    task_state_t state;
    volatile uint32_t counter; /* activity counter (demo/inspection)         */
    volatile uint32_t sleep_until;
} task_t;

/* Registers the currently-running kernel context as task 0 (the idle/shell). */
void tasking_init();

/* Spawns a ring-0 kernel thread. Returns its id, or -1 on failure. */
int task_create(void (*entry)());

/* Spawns a ring-3 user task with its own kernel + user stacks. */
int task_create_user(void (*entry)());

/* Round-robin switch to the next runnable task. Called from the timer IRQ and
 * from the blocking syscalls below. */
void schedule();

/* Voluntarily give up the CPU. */
void task_yield();

/* Block the current task for at least 'ticks' timer ticks. */
void task_sleep(uint32_t ticks);

/* Terminate the current task and switch away. Never returns. */
void task_exit();

/* Increments the current task's activity counter (used by demo workers). */
void task_inc();

int task_count();                 /* number of live (non-UNUSED) slots        */
int task_max();                   /* size of the task table                   */
uint32_t task_get_counter(int id);
int task_current_id();
task_state_t task_get_state(int id);
int task_alive(int id);           /* state is neither UNUSED nor DEAD          */

extern volatile int multitasking_on;

#endif
