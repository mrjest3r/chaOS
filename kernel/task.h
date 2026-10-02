#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include "event_user.h"
#include "proc_user.h"

typedef enum {
    TASK_UNUSED = 0, /* free slot                                   */
    TASK_READY,      /* runnable, waiting for the CPU               */
    TASK_RUNNING,    /* currently on the CPU                        */
    TASK_SLEEPING,   /* blocked until 'sleep_until' ticks           */
    TASK_DEAD,       /* finished; slot reclaimable                  */
    TASK_BLOCKED,    /* in SYS_EVENT_WAIT; woken by an event push or
                        by 'sleep_until' (0 = no timeout)           */
    TASK_FROZEN      /* suspended; the scheduler will not run it    */
} task_state_t;

#define TASK_NAME_LEN 24

/* Per-task input event queue (GUI G3). Producer is the keyboard / mouse IRQ,
 * consumer is task_event_wait; count = ev_head - ev_tail. */
#define TASK_EVQ_SIZE 64

/* One SYS_SURFACE pixel buffer mapped into a user task (GUI G2). */
typedef struct {
    uint32_t vaddr;            /* user address; 0 = slot free                */
    uint32_t bytes;            /* logical size w*h*4 (mapping is page-rounded)*/
} task_surface_t;

#define TASK_MAX_SURF 4        /* == SURF_SLOTS in cpu/vmm.h                 */

typedef struct task {
    uint32_t esp;              /* saved kernel stack pointer (first field)   */
    uint32_t kstack;           /* base of kernel-stack allocation (to free)  */
    uint32_t kstack_top;       /* top of kernel stack -> loaded into TSS.esp0*/
    uint32_t page_dir;         /* CR3 for this task (kernel_directory if k.) */
    uint32_t user_stack_top;   /* ring-3 stack top (user vaddr; 0 if kernel) */
    void (*entry)();           /* task entry point                           */
    int id;
    int parent;                /* spawning slot, or -1                       */
    int is_user;               /* 1 = runs in ring 3                         */
    task_state_t state;
    char name[TASK_NAME_LEN];
    volatile uint32_t counter; /* activity counter (demo/inspection)         */
    volatile uint32_t sleep_until;
    task_surface_t surf[TASK_MAX_SURF];
    uevent_t evq[TASK_EVQ_SIZE];
    volatile uint32_t ev_head, ev_tail;
    /* Console redirection (GUI G4): -1 = kernel console / keyboard ring,
     * else a pipe id used by SYS_PRINT / GETCHAR / READLINE / TRYCHAR.
     * Inherited by children; io_owner marks the task that was handed the
     * pipes by SYS_SPAWNIO and therefore closes stdout at exit (EOF for the
     * compositor). */
    int stdin_pipe, stdout_pipe;
    int io_owner;
    uint32_t brk_base;         /* first byte past the loaded image, page aligned */
    uint32_t brk;              /* current heap end (SYS_BRK)                     */
} task_t;

/* Registers the currently-running kernel context as task 0 (the idle/shell). */
void tasking_init();

/* Spawns a ring-0 kernel thread. Returns its id, or -1 on failure. */
int task_create(void (*entry)());

/* Spawns a ring-3 user task that runs in the given address space ('page_dir',
 * a CR3 value) starting at 'entry' on the user stack top 'user_stack_top'. The
 * caller is responsible for having mapped the program and stack into page_dir.
 * Returns the task id, or -1 on failure. */
int task_create_user(uint32_t entry, uint32_t page_dir, uint32_t user_stack_top);

/* Round-robin switch to the next runnable task. Called from the timer IRQ and
 * from the blocking syscalls below. */
void schedule();

/* Voluntarily give up the CPU. */
void task_yield();

/* Block the current task for at least 'ticks' timer ticks. */
void task_sleep(uint32_t ticks);

/* Free DEAD task slots (except the current task). Safe to call from the shell. */
void task_reap();

/* Stop preemption, reap user tasks, force kernel CR3 — call before power_off. */
void task_prepare_poweroff(void);

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

/* Snapshot of live tasks (kernel threads included). Returns the count
 * written, at most 'max'. Reaps DEAD slots first. */
int task_list(uproc_t *out, int max);
void task_set_name(int id, const char *name);
/* Point a live task at another slot. parent -1 clears it. */
int task_set_parent(int id, int parent);

/* User tasks only. Refuse pid 0, kernel threads, and the caller.
 * Kill leaves the slot DEAD for the next reap. Freeze sticks until awaken. */
int task_kill(int id);
int task_obliterate(int id);
int task_freeze(int id);
int task_awaken(int id);

/* ---- surfaces (GUI G2) ---------------------------------------------------
 * Allocate a w*h*4 pixel buffer, map it PAGE_USER into the current task and
 * return its user address (0 on failure: not a user task, no slot, no frames,
 * or larger than one slot). Freed by task_surface_free or at task exit. */
uint32_t task_surface_alloc(uint32_t w, uint32_t h);
int      task_surface_free(uint32_t vaddr);   /* 0 / -1                        */
/* 1 if [p, p+len) lies inside one of the current task's live surfaces. */
int      task_surface_range_ok(uint32_t p, uint32_t len);

/* ---- console redirection (GUI G4) ----------------------------------------
 * Give task 'id' its own stdin/stdout pipes (-1 keeps what it inherited).
 * Call right after creating it, before it can run. */
void task_set_io(int id, int stdin_pipe, int stdout_pipe);
int  task_stdin(void);    /* current task's stdin pipe or -1               */
int  task_stdout(void);   /* current task's stdout pipe or -1              */

/* ---- input events (GUI G3) -----------------------------------------------
 * Make the current (user) task the input listener. A previous listener is
 * stacked and restored when this task exits. Only the top receives events. */
/* Slot that should receive input. A background listener (the desk, under
 * F1 or a nested exec) must not steal keys meant for the foreground task. */
void task_set_foreground(int id);
int  task_foreground(void);

void task_event_listen(void);
int  task_event_has_listener(void);
/* IRQ-side producers: queue an event for the listener and make it READY.
 * Return 1 if a listener took it, 0 if there is none (caller falls back to
 * the legacy ring / console behaviour). Never schedule — IRQ context. */
int  task_event_key(char c);
/* Free slots in the listener's queue (0 if there is no listener). */
int  task_event_room(void);
int  task_event_mouse(int32_t x, int32_t y, uint32_t buttons);
/* Pop the next event for the current task, blocking (TASK_BLOCKED) up to
 * 'timeout' ticks (0 = poll, UEV_FOREVER = no timeout).
 * Returns 1 = event stored, 0 = timeout, -1 = not a user task. */
int  task_event_wait(uevent_t *out, uint32_t timeout);

/* Heap end for a user task. task_brk(0) returns it; any other value tries to
 * move it, mapping pages up to brk_base + USER_HEAP_MAX. Returns the new
 * break, or (uint32_t)-1. */
void task_set_break(int id, uint32_t brk);
uint32_t task_brk(uint32_t want);

extern volatile int multitasking_on;

#endif
