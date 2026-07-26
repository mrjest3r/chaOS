#include "task.h"
#include "kheap.h"
#include "../cpu/gdt.h"
#include "../cpu/timer.h"
#include "../cpu/paging.h"
#include "../cpu/vmm.h"

/* Implemented in cpu/context.asm */
extern void context_switch(uint32_t *old_esp, uint32_t new_esp);
/* Implemented in cpu/usermode.asm */
extern void enter_user_mode(uint32_t entry, uint32_t user_stack);

#define MAX_TASKS   8
#define KSTACK_SIZE 8192

static task_t tasks[MAX_TASKS];
static int current_idx = 0;
static task_t *current = 0;
volatile int multitasking_on = 0;

/* Mirrors the CR3 currently loaded, so we only reload (and flush the TLB) when
 * actually switching between different address spaces. */
static uint32_t active_cr3 = 0;

/* Trampoline for kernel threads. context_switch "returns" here the first time a
 * task is scheduled. Interrupts were disabled during the switch, so re-enable
 * them before running. A kernel thread that returns is terminated cleanly. */
static void kthread_bootstrap() {
    asm volatile("sti");
    current->entry();
    task_exit();
}

/* Trampoline for user tasks. Runs in ring 0 on the task's kernel stack, then
 * drops to ring 3. By the time we get here the scheduler has already loaded the
 * task's page directory, so the user stack/entry virtual addresses are valid.
 * enter_user_mode sets IF in the ring-3 EFLAGS, so we do not enable interrupts
 * here (they must stay off until the iret). */
static void user_bootstrap() {
    enter_user_mode((uint32_t) current->entry, current->user_stack_top);
}

void tasking_init() {
    for (int i = 0; i < MAX_TASKS; i++) tasks[i].state = TASK_UNUSED;

    /* Task 0 is the already-running kernel context (the shell / idle task). It
     * always stays runnable, so the scheduler never runs dry. */
    tasks[0].id = 0;
    tasks[0].entry = 0;
    tasks[0].is_user = 0;
    tasks[0].state = TASK_RUNNING;
    tasks[0].counter = 0;
    tasks[0].sleep_until = 0;
    tasks[0].kstack = 0;
    tasks[0].kstack_top = 0x90000; /* protected-mode boot stack top */
    tasks[0].page_dir = (uint32_t) kernel_directory;
    tasks[0].user_stack_top = 0;

    current = &tasks[0];
    current_idx = 0;

    /* Paging is already on with the kernel directory loaded. */
    active_cr3 = (uint32_t) kernel_directory;
}

/* Finds a reusable slot (never slot 0). Frees the stacks of a DEAD slot before
 * handing it out. Returns -1 if the table is full. */
static int alloc_slot() {
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED || tasks[i].state == TASK_DEAD) {
            if (tasks[i].state == TASK_DEAD && tasks[i].kstack) {
                free((void *) tasks[i].kstack);
                tasks[i].kstack = 0;
            }
            return i;
        }
    }
    return -1;
}

/* Builds the initial kernel-stack frame that context_switch can "return" into:
 * it pops edi, esi, ebx, ebp, then rets to 'bootstrap'. */
static void prime_kstack(task_t *t, void (*bootstrap)()) {
    uint32_t *sp = (uint32_t *) (t->kstack + KSTACK_SIZE);
    *(--sp) = (uint32_t) bootstrap; /* return address for context_switch */
    *(--sp) = 0;                    /* ebp */
    *(--sp) = 0;                    /* ebx */
    *(--sp) = 0;                    /* esi */
    *(--sp) = 0;                    /* edi */
    t->esp = (uint32_t) sp;
}

int task_create(void (*entry)()) {
    int i = alloc_slot();
    if (i < 0) return -1;

    uint32_t kstack = (uint32_t) malloc(KSTACK_SIZE);
    if (!kstack) return -1;

    task_t *t = &tasks[i];
    t->id = i;
    t->entry = entry;
    t->is_user = 0;
    t->counter = 0;
    t->sleep_until = 0;
    t->kstack = kstack;
    t->kstack_top = kstack + KSTACK_SIZE;
    t->page_dir = (uint32_t) kernel_directory;
    t->user_stack_top = 0;

    prime_kstack(t, kthread_bootstrap);
    t->state = TASK_READY;
    return t->id;
}

int task_create_user(uint32_t entry, uint32_t page_dir, uint32_t user_stack_top) {
    int i = alloc_slot();
    if (i < 0) return -1;

    uint32_t kstack = (uint32_t) malloc(KSTACK_SIZE);
    if (!kstack) return -1;

    task_t *t = &tasks[i];
    t->id = i;
    t->entry = (void (*)()) entry;
    t->is_user = 1;
    t->counter = 0;
    t->sleep_until = 0;
    t->kstack = kstack;
    t->kstack_top = kstack + KSTACK_SIZE;
    t->page_dir = page_dir;
    t->user_stack_top = user_stack_top;

    prime_kstack(t, user_bootstrap);
    t->state = TASK_READY;
    return t->id;
}

/* Promote any sleeping tasks whose deadline has passed back to READY. */
static void wake_sleepers() {
    uint32_t now = timer_ticks();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && now >= tasks[i].sleep_until)
            tasks[i].state = TASK_READY;
    }
}

void schedule() {
    if (!multitasking_on || current == 0) return;

    wake_sleepers();

    /* Round-robin: find the next READY/RUNNING slot after the current one. */
    int next = -1;
    for (int off = 1; off <= MAX_TASKS; off++) {
        int idx = (current_idx + off) % MAX_TASKS;
        task_state_t st = tasks[idx].state;
        if (st == TASK_READY || st == TASK_RUNNING) { next = idx; break; }
    }

    if (next < 0 || next == current_idx) {
        /* Nobody else is runnable. If the current task is no longer runnable
         * (it just slept or exited) we must not fall through to it - task 0 is
         * always runnable, so this branch only happens when current is fine. */
        return;
    }

    task_t *prev = current;
    if (prev->state == TASK_RUNNING) prev->state = TASK_READY;

    current_idx = next;
    current = &tasks[next];
    current->state = TASK_RUNNING;

    /* The CPU uses this stack when the next interrupt/syscall re-enters ring 0
     * from ring 3. Must point at the task we are about to run. */
    tss_set_stack(current->kstack_top);

    /* Switch address spaces if the next task lives in a different one. The
     * kernel is mapped identically in every directory, so the code and stacks
     * we are running on remain valid across the CR3 load. */
    if (current->page_dir && current->page_dir != active_cr3) {
        load_page_directory((uint32_t *) current->page_dir);
        active_cr3 = current->page_dir;
    }

    context_switch(&prev->esp, current->esp);
}

void task_yield() {
    if (multitasking_on) schedule();
}

void task_sleep(uint32_t ticks) {
    if (!multitasking_on || current == 0) {
        uint32_t start = timer_ticks();
        while (timer_ticks() - start < ticks) asm volatile("hlt");
        return;
    }
    current->sleep_until = timer_ticks() + ticks;
    current->state = TASK_SLEEPING;
    schedule();
}

void task_exit() {
    asm volatile("cli");

    if (current == 0 || current == &tasks[0]) {
        /* Task 0 must never exit; just idle if something asks it to. */
        for (;;) asm volatile("hlt");
    }

    /* If this was a user task with its own address space, return to the kernel
     * directory first (where the frame pool is reachable) and then free the
     * process's page directory, page tables and user frames. The kernel stack
     * we are running on lives in the shared kernel mapping, so it stays valid. */
    uint32_t dir = current->page_dir;
    if (dir && dir != (uint32_t) kernel_directory) {
        load_page_directory(kernel_directory);
        active_cr3 = (uint32_t) kernel_directory;
        vmm_destroy_addrspace(dir);
        current->page_dir = (uint32_t) kernel_directory;
    }

    current->state = TASK_DEAD;

    /* Switch away permanently. schedule() will pick another runnable task
     * (task 0 at worst) and never switch back to this DEAD slot. The kernel
     * stack is reclaimed later by alloc_slot() when the slot is reused. */
    schedule();

    for (;;) asm volatile("hlt"); /* unreachable */
}

void task_inc() {
    if (current) current->counter++;
}

int task_count() {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (tasks[i].state != TASK_UNUSED) n++;
    return n;
}

int task_max() {
    return MAX_TASKS;
}

uint32_t task_get_counter(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    return tasks[id].counter;
}

int task_current_id() {
    return current ? current->id : -1;
}

task_state_t task_get_state(int id) {
    if (id < 0 || id >= MAX_TASKS) return TASK_UNUSED;
    return tasks[id].state;
}

int task_alive(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    task_state_t s = tasks[id].state;
    return s != TASK_UNUSED && s != TASK_DEAD;
}
