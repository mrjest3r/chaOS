#include "task.h"
#include "kheap.h"
#include "pipe.h"
#include "uaccess.h"
#include "frame_user.h"
#include "../cpu/arch.h"
#include "../libc/mem.h"

#define MAX_TASKS   16
#define KSTACK_SIZE 8192

static task_t tasks[MAX_TASKS];
static int current_idx = 0;
static task_t *current = 0;
volatile int multitasking_on = 0;

/* Mirrors the CR3 currently loaded, so we only reload (and flush the TLB) when
 * actually switching between different address spaces. */
static uint32_t active_cr3 = 0;

/* Active input listener (SYS_EVENT_WAIT). A short stack lets a nested
 * listener (a client that also waits) restore the compositor on exit.
 * Only the top of the stack receives events (still one listener at a time). */
#define EV_LISTEN_STACK 4
static int fg_slot = -1;
static volatile int ev_listener = -1;
static int ev_prev[EV_LISTEN_STACK];
static int ev_prev_n = 0;

static void ev_clear_stack(void) {
    ev_listener = -1;
    ev_prev_n = 0;
}

static void ev_drop_task(int idx) {
    int w = 0;
    for (int i = 0; i < ev_prev_n; i++)
        if (ev_prev[i] != idx) ev_prev[w++] = ev_prev[i];
    ev_prev_n = w;
    if (ev_listener == idx)
        ev_listener = (ev_prev_n > 0) ? ev_prev[--ev_prev_n] : -1;
}

static void reset_events(task_t *t) {
    t->ev_head = t->ev_tail = 0;
}

static void reset_io(task_t *t) {
    t->stdin_pipe = t->stdout_pipe = -1;
    t->io_owner = 0;
    t->frame_ptr = 0;
    t->frame_w = t->frame_h = 0;
    t->frame_x = t->frame_y = 0;
}

static int key_owner = -1;

void task_set_keys(int id) { key_owner = id; }

int task_raw_keys(void) {
    if (key_owner < 0) return 1;
    return current && current->id == key_owner;
}

static int live_state(task_state_t s) {
    return s != TASK_UNUSED && s != TASK_DEAD;
}

static void name_set(task_t *t, const char *s) {
    int i = 0;
    if (!s) s = "";
    while (s[i] && i < TASK_NAME_LEN - 1) {
        t->name[i] = s[i];
        i++;
    }
    t->name[i] = '\0';
}

/* Tear down a user task that is not on the CPU. Slot stays DEAD until reap. */
static void drop_user_task(task_t *t) {
    uint32_t dir = t->page_dir;
    if (dir && dir != (uint32_t) kernel_directory) {
        vmm_destroy_addrspace(dir);
        t->page_dir = (uint32_t) kernel_directory;
    }
    for (int s = 0; s < TASK_MAX_SURF; s++)
        t->surf[s].vaddr = t->surf[s].bytes = 0;
    ev_drop_task(t->id);
    reset_events(t);
    if (t->io_owner && t->stdout_pipe >= 0)
        pipe_close(t->stdout_pipe);
    reset_io(t);
    if (fg_slot == t->id) fg_slot = -1;
    t->state = TASK_DEAD;
}

static void orphan_children(int id) {
    for (int i = 1; i < MAX_TASKS; i++)
        if (tasks[i].parent == id) tasks[i].parent = -1;
}

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
    tasks[0].parent = -1;
    tasks[0].entry = 0;
    tasks[0].is_user = 0;
    name_set(&tasks[0], "kernel");
    tasks[0].state = TASK_RUNNING;
    tasks[0].counter = 0;
    tasks[0].sleep_until = 0;
    tasks[0].kstack = 0;
    tasks[0].kstack_top = 0xC0090000; /* boot stack top (higher-half alias) */
    tasks[0].page_dir = (uint32_t) kernel_directory;
    tasks[0].user_stack_top = 0;
    reset_events(&tasks[0]);
    reset_io(&tasks[0]);
    ev_clear_stack();

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
    t->parent = current ? current->id : -1;
    t->entry = entry;
    t->is_user = 0;
    name_set(t, "kthread");
    t->counter = 0;
    t->sleep_until = 0;
    t->kstack = kstack;
    t->kstack_top = kstack + KSTACK_SIZE;
    t->page_dir = (uint32_t) kernel_directory;
    t->user_stack_top = 0;
    t->brk_base = t->brk = 0;
    for (int s = 0; s < TASK_MAX_SURF; s++) t->surf[s].vaddr = t->surf[s].bytes = 0;
    reset_events(t);
    reset_io(t);

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
    t->parent = current ? current->id : -1;
    t->entry = (void (*)()) entry;
    t->is_user = 1;
    name_set(t, "");
    t->counter = 0;
    t->sleep_until = 0;
    t->kstack = kstack;
    t->kstack_top = kstack + KSTACK_SIZE;
    t->page_dir = page_dir;
    t->user_stack_top = user_stack_top;
    t->brk_base = t->brk = 0;
    for (int s = 0; s < TASK_MAX_SURF; s++) t->surf[s].vaddr = t->surf[s].bytes = 0;
    reset_events(t);
    /* A windowed program's children talk to the same window. */
    reset_io(t);
    if (current && current->is_user) {
        t->stdin_pipe = current->stdin_pipe;
        t->stdout_pipe = current->stdout_pipe;
    }

    prime_kstack(t, user_bootstrap);
    t->state = TASK_READY;
    return t->id;
}

/* Promote sleeping tasks whose deadline has passed back to READY, and event
 * waiters whose timeout (sleep_until != 0) has expired. */
static void wake_sleepers() {
    uint32_t now = timer_ticks();
    for (int i = 0; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_SLEEPING && now >= tasks[i].sleep_until)
            tasks[i].state = TASK_READY;
        else if (tasks[i].state == TASK_BLOCKED && tasks[i].sleep_until &&
                 now >= tasks[i].sleep_until)
            tasks[i].state = TASK_READY;
    }
}

void schedule() {
    if (!multitasking_on || current == 0) return;

    /* Reclaim exited tasks whose stacks we are not currently running on. */
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD && &tasks[i] != current) {
            if (tasks[i].kstack) {
                free((void *) tasks[i].kstack);
                tasks[i].kstack = 0;
            }
            tasks[i].state = TASK_UNUSED;
            tasks[i].page_dir = 0;
            tasks[i].entry = 0;
            tasks[i].parent = -1;
            tasks[i].name[0] = '\0';
        }
    }

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

void task_reap() {
    asm volatile("cli");
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_DEAD && &tasks[i] != current) {
            if (tasks[i].kstack) {
                free((void *) tasks[i].kstack);
                tasks[i].kstack = 0;
            }
            tasks[i].state = TASK_UNUSED;
            tasks[i].page_dir = 0;
            tasks[i].entry = 0;
            tasks[i].parent = -1;
            tasks[i].name[0] = '\0';
        }
    }
    asm volatile("sti");
}

void task_prepare_poweroff(void) {
    asm volatile("cli");
    multitasking_on = 0;
    /* Kill any leftover user/background tasks so nothing runs during halt. */
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) continue;
        uint32_t dir = tasks[i].page_dir;
        if (dir && dir != (uint32_t) kernel_directory)
            vmm_destroy_addrspace(dir);
        if (tasks[i].kstack) {
            free((void *) tasks[i].kstack);
            tasks[i].kstack = 0;
        }
        tasks[i].state = TASK_UNUSED;
        tasks[i].page_dir = 0;
        tasks[i].entry = 0;
        tasks[i].parent = -1;
        tasks[i].name[0] = '\0';
    }
    load_page_directory(kernel_directory);
    active_cr3 = (uint32_t) kernel_directory;
    current_idx = 0;
    current = &tasks[0];
    current->state = TASK_RUNNING;
    ev_clear_stack();
    tss_set_stack(current->kstack_top);
    asm volatile("sti");
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

    /* Surface frames went with the address space above; just forget them. */
    for (int s = 0; s < TASK_MAX_SURF; s++) current->surf[s].vaddr = current->surf[s].bytes = 0;

    ev_drop_task(current_idx);
    reset_events(current);

    /* Hang up on the compositor so it sees EOF and can reclaim the window. */
    if (current->io_owner && current->stdout_pipe >= 0)
        pipe_close(current->stdout_pipe);
    reset_io(current);

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
        if (tasks[i].state != TASK_UNUSED && tasks[i].state != TASK_DEAD) n++;
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
    return live_state(tasks[id].state);
}

void task_set_name(int id, const char *name) {
    if (id < 0 || id >= MAX_TASKS) return;
    name_set(&tasks[id], name);
}

int task_set_parent(int id, int parent) {
    asm volatile("cli");
    if (id <= 0 || id >= MAX_TASKS || !live_state(tasks[id].state) ||
        parent < -1 || parent >= MAX_TASKS || parent == id) {
        asm volatile("sti");
        return -1;
    }
    tasks[id].parent = parent;
    asm volatile("sti");
    return 0;
}

int task_list(uproc_t *out, int max) {
    if (!out || max < 0) return -1;
    if (max > MAX_TASKS) max = MAX_TASKS;
    task_reap();
    asm volatile("cli");
    int n = 0;
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        if (!live_state(tasks[i].state)) continue;
        out[n].id = tasks[i].id;
        out[n].state = (uint32_t) tasks[i].state;
        out[n].user = tasks[i].is_user ? 1u : 0u;
        {
            int k = 0;
            while (tasks[i].name[k] && k < PROC_NAME_LEN - 1) {
                out[n].name[k] = tasks[i].name[k];
                k++;
            }
            out[n].name[k] = '\0';
        }
        n++;
    }
    asm volatile("sti");
    return n;
}

static int user_target(int id) {
    if (id <= 0 || id >= MAX_TASKS || id == current_idx) return 0;
    task_t *t = &tasks[id];
    return live_state(t->state) && t->is_user;
}

int task_kill(int id) {
    asm volatile("cli");
    if (!user_target(id)) {
        asm volatile("sti");
        return -1;
    }
    orphan_children(id);
    drop_user_task(&tasks[id]);
    asm volatile("sti");
    return 0;
}

int task_obliterate(int id) {
    asm volatile("cli");
    if (!user_target(id)) {
        asm volatile("sti");
        return -1;
    }
    uint8_t mark[MAX_TASKS];
    for (int i = 0; i < MAX_TASKS; i++) mark[i] = 0;
    mark[id] = 1;
    int grew = 1;
    while (grew) {
        grew = 0;
        for (int i = 1; i < MAX_TASKS; i++) {
            if (mark[i] || i == current_idx) continue;
            if (!live_state(tasks[i].state) || !tasks[i].is_user) continue;
            int p = tasks[i].parent;
            if (p > 0 && p < MAX_TASKS && mark[p]) {
                mark[i] = 1;
                grew = 1;
            }
        }
    }
    mark[current_idx] = 0;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (!mark[i]) continue;
        orphan_children(i);
        drop_user_task(&tasks[i]);
    }
    asm volatile("sti");
    return 0;
}

int task_freeze(int id) {
    asm volatile("cli");
    if (!user_target(id) || tasks[id].state == TASK_FROZEN) {
        asm volatile("sti");
        return -1;
    }
    tasks[id].state = TASK_FROZEN;
    asm volatile("sti");
    return 0;
}

int task_awaken(int id) {
    asm volatile("cli");
    if (id <= 0 || id >= MAX_TASKS || tasks[id].state != TASK_FROZEN) {
        asm volatile("sti");
        return -1;
    }
    tasks[id].state = TASK_READY;
    asm volatile("sti");
    return 0;
}

/* ---- surfaces (GUI G2) --------------------------------------------------- */

uint32_t task_surface_alloc(uint32_t w, uint32_t h) {
    if (!current || !current->is_user) return 0;
    if (w == 0 || h == 0 || w > 4096 || h > 4096) return 0;
    uint32_t logical = w * h * 4u;
    if (logical > SURF_SLOT_BYTES) return 0;
    uint32_t bytes = (logical + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1u);

    int slot = -1;
    for (int s = 0; s < TASK_MAX_SURF; s++) {
        if (current->surf[s].vaddr == 0) { slot = s; break; }
    }
    if (slot < 0) return 0;

    uint32_t base = SURF_BASE + (uint32_t) slot * SURF_SLOT_BYTES;
    uint32_t pd = current->page_dir;
    uint32_t off = 0;
    for (; off < bytes; off += PAGE_SIZE) {
        uint32_t f = frame_alloc();
        if (!f || vmm_map(pd, base + off, f, PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
            if (f) frame_free(f);
            vmm_unmap_pde(pd, base);
            load_page_directory((uint32_t *) pd);
            return 0;
        }
        memory_set((uint8_t *) f, 0, PAGE_SIZE); /* no stale pixels leak */
    }
    /* New PDE/PTEs; the TLB may hold stale non-present entries for this range. */
    load_page_directory((uint32_t *) pd);

    current->surf[slot].vaddr = base;
    current->surf[slot].bytes = logical; /* validate against w*h*4, not pages */
    return base;
}

int task_surface_free(uint32_t vaddr) {
    if (!current || !current->is_user) return -1;
    for (int s = 0; s < TASK_MAX_SURF; s++) {
        if (current->surf[s].vaddr && current->surf[s].vaddr == vaddr) {
            vmm_unmap_pde(current->page_dir, vaddr);
            load_page_directory((uint32_t *) current->page_dir);
            current->surf[s].vaddr = current->surf[s].bytes = 0;
            return 0;
        }
    }
    return -1;
}

int task_surface_range_ok(uint32_t p, uint32_t len) {
    if (!current) return 0;
    for (int s = 0; s < TASK_MAX_SURF; s++) {
        uint32_t b = current->surf[s].vaddr, n = current->surf[s].bytes;
        if (!b) continue;
        if (p >= b && p < b + n && len <= (b + n) - p) return 1;
    }
    return 0;
}

/* ---- console redirection (GUI G4) ---------------------------------------- */

void task_set_io(int id, int stdin_pipe, int stdout_pipe) {
    if (id < 0 || id >= MAX_TASKS) return;
    task_t *t = &tasks[id];
    if (stdin_pipe >= 0) t->stdin_pipe = stdin_pipe;
    if (stdout_pipe >= 0) t->stdout_pipe = stdout_pipe;
    t->io_owner = 1;
}

int task_stdin(void) {
    return current ? current->stdin_pipe : -1;
}

int task_stdout(void) {
    return current ? current->stdout_pipe : -1;
}

static task_t *live_id(int id) {
    if (id < 0 || id >= MAX_TASKS) return 0;
    task_t *t = &tasks[id];
    if (t->id != id || t->state == TASK_UNUSED || t->state == TASK_DEAD) return 0;
    return t;
}

static uint32_t va_phys(uint32_t cr3, uint32_t va) {
    uint32_t *pd = (uint32_t *) cr3;
    uint32_t pde = pd[va >> 22];
    if (!(pde & 1u)) return 0;
    uint32_t *pt = (uint32_t *) (pde & ~0xFFFu);
    uint32_t pte = pt[(va >> 12) & 0x3FFu];
    if (!(pte & 1u)) return 0;
    return (pte & ~0xFFFu) | (va & 0xFFFu);
}

int task_frame(frame_req_t *req) {
    if (!req || !current) return -1;
    if (req->op == FRAME_PUBLISH) {
        if (req->w < 1 || req->h < 1 || req->w > 1024 || req->h > 1024) return -1;
        uint32_t bytes = (uint32_t) req->w * (uint32_t) req->h * 4u;
        if (!user_range_ok((const void *) req->ptr, bytes)) return -1;
        current->frame_ptr = req->ptr;
        current->frame_w = (uint16_t) req->w;
        current->frame_h = (uint16_t) req->h;
        return 0;
    }
    if (req->op == FRAME_QUERY) {
        req->x = current->frame_x;
        req->y = current->frame_y;
        req->w = current->frame_w;
        req->h = current->frame_h;
        req->flags = 0;
        req->ptr = current->frame_ptr;
        return current->frame_ptr ? 0 : -1;
    }
    if (req->op == FRAME_KEYS) {
        task_set_keys(req->pid);
        return 0;
    }
    task_t *src = live_id(req->pid);
    if (!src) return -1;
    if (req->op == FRAME_PLACE) {
        src->frame_x = (int16_t) req->x;
        src->frame_y = (int16_t) req->y;
        return 0;
    }
    /* Alive but not published yet: the window stays blank until the
     * first frame. A dead task already returned -1 above. */
    if (req->op != FRAME_BLIT) return -1;
    if (!src->frame_ptr) return 0;
    int sw = src->frame_w, sh = src->frame_h;
    int pitch = req->w, dh = req->h;
    if (pitch < 1 || dh < 1) return -1;
    if (!user_range_ok((const void *) req->ptr, (uint32_t) pitch * (uint32_t) dh * 4u))
        return -1;
    uint32_t *dst = (uint32_t *) req->ptr;
    for (int row = 0; row < sh; row++) {
        int dy = req->y + row;
        if (dy < 0 || dy >= dh) continue;
        uint32_t row_va = src->frame_ptr + (uint32_t) row * (uint32_t) sw * 4u;
        int col = 0;
        while (col < sw) {
            int dx = req->x + col;
            uint32_t phys = va_phys(src->page_dir, row_va + (uint32_t) col * 4u);
            if (!phys) { col++; continue; }
            uint32_t room = (4096u - (phys & 0xFFFu)) / 4u;
            int n = sw - col;
            if ((uint32_t) n > room) n = (int) room;
            for (int i = 0; i < n; i++) {
                int x = dx + i;
                if (x >= 0 && x < pitch)
                    dst[dy * pitch + x] = ((uint32_t *) phys)[i];
            }
            col += n;
        }
    }
    return 0;
}

/* ---- input events (GUI G3) ----------------------------------------------- */

void task_set_foreground(int id) { fg_slot = id; }
int task_foreground(void) { return fg_slot; }

/* A listener that is not the foreground task (desk under F1 / nested exec)
 * must not eat keys or mouse packets meant for the shell or the child. */
static int listener_gets_input(task_t *t) {
    if (fg_slot >= 0 && t->id != fg_slot) return 0;
    return 1;
}

void task_event_listen(void) {
    if (!current || !current->is_user) return;
    if (ev_listener == current_idx) return;
    if (ev_listener >= 0 && ev_prev_n < EV_LISTEN_STACK)
        ev_prev[ev_prev_n++] = ev_listener;
    ev_listener = current_idx;
}

int task_event_has_listener(void) {
    return ev_listener >= 0;
}

/* Returns the listener task if it can still receive events, else 0. */
static task_t *listener(void) {
    if (ev_listener < 0) return 0;
    task_t *t = &tasks[ev_listener];
    if (t->state == TASK_UNUSED || t->state == TASK_DEAD) {
        ev_drop_task(ev_listener);
        return listener();
    }
    return t;
}

/* Append 'ev' to t's queue (drop when full) and wake t if it is blocked.
 * Called with interrupts disabled (IRQ handler or cli section). */
static int evq_full(task_t *t) {
    return t->ev_head - t->ev_tail >= TASK_EVQ_SIZE;
}

static void evq_push(task_t *t, const uevent_t *ev) {
    if (!evq_full(t)) {
        t->evq[t->ev_head % TASK_EVQ_SIZE] = *ev;
        t->ev_head++;
    }
    if (t->state == TASK_BLOCKED) t->state = TASK_READY;
}

int task_event_room(void) {
    task_t *t = listener();
    if (!t) return 0;
    int used = (int) (t->ev_head - t->ev_tail);
    return TASK_EVQ_SIZE - used;
}

int task_event_key(char c) {
    task_t *t = listener();
    if (!t || !listener_gets_input(t) || evq_full(t)) return 0;
    uevent_t ev;
    ev.type = UEV_KEY;
    ev.x = ev.y = 0;
    ev.key = (uint32_t) (uint8_t) c;
    ev.buttons = 0;
    evq_push(t, &ev);
    return 1;
}

int task_event_mouse(int32_t x, int32_t y, uint32_t buttons) {
    task_t *t = listener();
    if (!t || !listener_gets_input(t)) return 0;
    /* Coalesce pure motion: update the newest unread mouse event in place
     * when its button state matches, so motion never floods the queue. */
    if (t->ev_head != t->ev_tail) {
        uevent_t *last = &t->evq[(t->ev_head - 1) % TASK_EVQ_SIZE];
        if (last->type == UEV_MOUSE && last->buttons == buttons) {
            last->x = x;
            last->y = y;
            if (t->state == TASK_BLOCKED) t->state = TASK_READY;
            return 1;
        }
    }
    uevent_t ev;
    ev.type = UEV_MOUSE;
    ev.x = x;
    ev.y = y;
    ev.key = 0;
    ev.buttons = buttons;
    evq_push(t, &ev);
    return 1;
}

int task_event_wait(uevent_t *out, uint32_t timeout) {
    if (!current || !current->is_user) return -1;
    ev_listener = current_idx;

    int forever = (timeout == UEV_FOREVER);
    uint32_t deadline = 0;
    if (!forever && timeout) {
        deadline = timer_ticks() + timeout;
        if (deadline == 0) deadline = 1; /* 0 means "no timeout" below */
    }

    for (;;) {
        /* The check-then-block below must be atomic against the IRQ
         * producers, otherwise a push between them would be lost. */
        asm volatile("cli");
        if (current->ev_head != current->ev_tail) {
            *out = current->evq[current->ev_tail % TASK_EVQ_SIZE];
            current->ev_tail++;
            asm volatile("sti");
            return 1;
        }
        if (!forever && (timeout == 0 || (int32_t) (timer_ticks() - deadline) >= 0)) {
            asm volatile("sti");
            return 0;
        }
        if (!multitasking_on) {
            /* No scheduler to hand the CPU to: idle until the next IRQ. */
            asm volatile("sti; hlt");
            continue;
        }
        current->sleep_until = forever ? 0 : deadline;
        current->state = TASK_BLOCKED;
        /* Interrupts stay off across the switch; the task we resume restores
         * its own IF, and we come back here (IF=0) once an event or the
         * timer made us READY again. */
        schedule();
    }
}

void task_set_break(int id, uint32_t brk) {
    if (id <= 0 || id >= MAX_TASKS) return;
    tasks[id].brk_base = brk;
    tasks[id].brk = brk;
}

uint32_t task_brk(uint32_t want) {
    if (!current || !current->is_user || current->brk_base == 0)
        return (uint32_t) -1;
    if (want == 0) return current->brk;
    if (want < current->brk_base) return (uint32_t) -1;
    if (want - current->brk_base > USER_HEAP_MAX) return (uint32_t) -1;
    if (want > USER_PROG_LIMIT) return (uint32_t) -1;

    uint32_t from = (current->brk + 0xFFFu) & ~0xFFFu;
    uint32_t to = (want + 0xFFFu) & ~0xFFFu;
    if (want > current->brk) {
        for (uint32_t a = from; a < to; a += PAGE_SIZE) {
            uint32_t f = frame_alloc();
            if (!f) return (uint32_t) -1;
            memory_set((uint8_t *) f, 0, PAGE_SIZE);
            if (vmm_map(current->page_dir, a, f,
                        PAGE_PRESENT | PAGE_RW | PAGE_USER) != 0) {
                frame_free(f);
                return (uint32_t) -1;
            }
        }
        load_page_directory((uint32_t *) current->page_dir);
    }
    current->brk = want;
    return current->brk;
}
