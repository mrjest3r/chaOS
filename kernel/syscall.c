#include "syscall.h"
#include "task.h"
#include "fs.h"
#include "elf.h"
#include "pipe.h"
#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/idle.h"
#include "../cpu/power.h"
#include "../drivers/rtc.h"
#include "proc_user.h"
#include "uaccess.h"
#include "../libc/string.h"
#include <stdint.h>

#define EFAULT ((uint32_t) -1)

/* ---- console of the calling task ------------------------------------------
 * Either the kernel console + keyboard ring, or (GUI G4, SYS_SPAWNIO) a pair
 * of pipes owned by a compositor window. */

static void con_print(const char *s) {
    int out = task_stdout();
    if (out >= 0) pipe_write(out, (const uint8_t *) s, strlen((char *) s));
    else kprint((char *) s);
}

static void con_backspace(void) {
    if (task_stdout() >= 0) con_print("\b");
    else kprint_backspace();
}

/* Window consoles understand a few cursor sequences. The kernel console
 * does not, so these are pipe-only. */
static void con_csi(char cmd, int n) {
    if (task_stdout() < 0 || n <= 0) return;
    if (n > 99) n = 99;
    char b[8];
    int i = 0;
    b[i++] = 27;
    b[i++] = '[';
    if (n >= 10) b[i++] = (char) ('0' + n / 10);
    b[i++] = (char) ('0' + n % 10);
    b[i++] = cmd;
    b[i] = '\0';
    con_print(b);
}

static void con_span(const char *s, int n) {
    for (int i = 0; i < n; i++) {
        char b[2];
        b[0] = s[i];
        b[1] = '\0';
        con_print(b);
    }
}

/* Blocking single-character read. Waits with interrupts enabled so the timer
 * keeps scheduling other tasks while this one sits on the keyboard. A
 * redirected task whose stdin pipe hits EOF (window closed) is terminated. */
static char sys_getchar() {
    int in = task_stdin();
    if (in >= 0) {
        uint8_t c;
        if (pipe_read(in, &c, 1) <= 0) task_exit();
        return (char) c;
    }
    char c;
    while ((c = kbd_getchar()) == 0) {
        asm volatile("sti; hlt");
    }
    return c;
}

static char sys_trychar() {
    int in = task_stdin();
    if (in >= 0) {
        int n = pipe_avail(in);
        if (n < 0) task_exit();
        if (n == 0) return 0;
        uint8_t c;
        if (pipe_read(in, &c, 1) <= 0) task_exit();
        return (char) c;
    }
    return kbd_getchar();
}

/* Blocking line read with echo and Backspace editing. Returns the line length
 * (without the newline); the buffer is always NUL-terminated. */
static int sys_readline(char *buf, int max) {
    int len = 0;
    int caret = 0;
    if (max <= 0) return 0;

    for (;;) {
        char c = sys_getchar();

        if (c == 0x11) { /* left */
            if (caret > 0) {
                caret--;
                con_csi('D', 1);
            }
            continue;
        }
        if (c == 0x12) { /* right */
            if (caret < len) {
                caret++;
                con_csi('C', 1);
            }
            continue;
        }
        if (c == 0x13) { /* home */
            con_csi('D', caret);
            caret = 0;
            continue;
        }
        if (c == 0x14) { /* end */
            con_csi('C', len - caret);
            caret = len;
            continue;
        }
        /* Up / Down scroll the window's history. A client editor reads
         * these itself and never reaches this line editor. */
        if (c == 0x15) { con_csi('S', 3); continue; }
        if (c == 0x16) { con_csi('T', 3); continue; }

        if (c == '\b') {
            if (caret > 0) {
                for (int i = caret - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                caret--;
                buf[len] = '\0';
                if (task_stdout() >= 0) {
                    con_csi('D', 1);
                    con_span(buf + caret, len - caret);
                    con_print(" ");
                    con_csi('D', len - caret + 1);
                } else if (caret == len) {
                    con_backspace();
                }
            }
            continue;
        }
        if (c == '\n') {
            con_print("\n");
            break;
        }
        if (len < max - 1 && c >= 32 && c <= 126) {
            for (int i = len; i > caret; i--) buf[i] = buf[i - 1];
            buf[caret] = c;
            len++;
            caret++;
            buf[len] = '\0';
            if (task_stdout() >= 0) {
                con_span(buf + caret - 1, len - (caret - 1));
                con_csi('D', len - caret);
            } else if (caret == len) {
                char echo[2] = { c, '\0' };
                con_print(echo);
            }
        }
    }

    buf[len] = '\0';
    return len;
}

/* Block until the given task exits. Waiting on yourself would never return,
 * so that is rejected. Waiting keeps interrupts enabled: the timer goes on
 * scheduling everyone else (including the task being waited for). */
static int sys_wait(int pid) {
    if (pid < 0 || pid == task_current_id()) return -1;
    while (task_alive(pid)) {
        asm volatile("sti; hlt");
    }
    return 0;
}

static void syscall_handler(registers_t *r) {
    /* Argument pointers (ebx/ecx) are caller addresses. Ring 0 can reach every
     * mapping, so each one is checked against the user window (uaccess.h)
     * before it is dereferenced; anything outside fails with -1.
     * SYS_FB lives in syscall_fb.c so this switch stays the ABI table. */
    if (r->eax > SYS_BRK) {
        r->eax = (uint32_t) -1;
        return;
    }
    switch (r->eax) {
        case SYS_PRINT:
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0) {
                r->eax = EFAULT;
                break;
            }
            con_print((const char *) r->ebx);
            r->eax = 0;
            break;

        case SYS_GETPID:
            r->eax = (uint32_t) task_current_id();
            break;

        case SYS_WRITEFILE:
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0 ||
                !user_range_ok((const void *) r->ecx, r->edx)) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) fs_write_file((char *) r->ebx,
                                              (uint8_t *) r->ecx, r->edx);
            break;

        case SYS_READFILE:
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0 ||
                !user_range_ok((const void *) r->ecx, r->edx)) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) fs_read_file((char *) r->ebx,
                                             (uint8_t *) r->ecx, r->edx);
            break;

        case SYS_LISTFILES:
            if ((int) r->ecx < 0 ||
                !user_range_ok((const void *) r->ebx,
                               r->ecx * (uint32_t) sizeof(fs_fileinfo_t))) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) fs_list((fs_fileinfo_t *) r->ebx, (int) r->ecx);
            break;

        case SYS_UPTIME:
            r->eax = timer_ticks();
            break;

        case SYS_YIELD:
            task_yield();
            r->eax = 0;
            break;

        case SYS_SLEEP:
            task_sleep(r->ebx);
            r->eax = 0;
            break;

        case SYS_GETCHAR:
            r->eax = (uint32_t) sys_getchar();
            break;

        case SYS_READLINE:
            if ((int) r->ecx < 0 || !user_range_ok((const void *) r->ebx, r->ecx)) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) sys_readline((char *) r->ebx, (int) r->ecx);
            break;

        case SYS_SPAWN:
            /* Loads the ELF into a brand-new isolated address space and
             * schedules it; the caller keeps running concurrently. */
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) elf_spawn((const char *) r->ebx);
            break;

        case SYS_WAIT:
            r->eax = (uint32_t) sys_wait((int) r->ebx);
            break;

        case SYS_PIPE:
            r->eax = (uint32_t) pipe_create();
            break;

        case SYS_PIPEREAD:
            if ((int) r->edx < 0 || !user_range_ok((const void *) r->ecx, r->edx)) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) pipe_read((int) r->ebx,
                                          (uint8_t *) r->ecx, (int) r->edx);
            break;

        case SYS_PIPEWRITE:
            if ((int) r->edx < 0 || !user_range_ok((const void *) r->ecx, r->edx)) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) pipe_write((int) r->ebx,
                                           (const uint8_t *) r->ecx,
                                           (int) r->edx);
            break;

        case SYS_PIPECLOSE:
            r->eax = (uint32_t) pipe_close((int) r->ebx);
            break;

        case SYS_MOUSEGET:
            if (!user_range_ok((const void *) r->ebx, sizeof(mouse_state_t))) {
                r->eax = EFAULT;
            } else {
                mouse_get((mouse_state_t *) r->ebx);
                r->eax = mouse_available() ? 0 : (uint32_t) -1;
            }
            break;

        case SYS_TRYCHAR:
            r->eax = (uint32_t) (uint8_t) sys_trychar();
            break;

        case SYS_SPAWNIO:
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) elf_spawn_io((const char *) r->ebx,
                                             (int) r->ecx, (int) r->edx);
            break;

        case SYS_PIPEPOLL:
            r->eax = (uint32_t) pipe_avail((int) r->ebx);
            break;

        case SYS_PROCS: {
            int max = (int) r->ecx;
            if (max < 0) {
                r->eax = EFAULT;
                break;
            }
            if (max > 16) max = 16;
            if (max > 0 &&
                !user_range_ok((const void *) r->ebx,
                               (uint32_t) max * (uint32_t) sizeof(uproc_t))) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) task_list((uproc_t *) r->ebx, max);
            break;
        }

        case SYS_PSCTL:
            if (r->ecx == PSOP_SLAY) r->eax = (uint32_t) task_kill((int) r->ebx);
            else if (r->ecx == PSOP_OBLITERATE)
                r->eax = (uint32_t) task_obliterate((int) r->ebx);
            else if (r->ecx == PSOP_FREEZE)
                r->eax = (uint32_t) task_freeze((int) r->ebx);
            else if (r->ecx == PSOP_AWAKEN)
                r->eax = (uint32_t) task_awaken((int) r->ebx);
            else r->eax = EFAULT;
            break;

        case SYS_POWER:
            if (r->ebx != PWOP_REBOOT && r->ebx != PWOP_SHUTDOWN &&
                r->ebx != PWOP_OFF) {
                r->eax = EFAULT;
                break;
            }
            task_prepare_poweroff();
            if (r->ebx == PWOP_REBOOT) power_reboot();
            power_off();
            r->eax = EFAULT;
            break;

        case SYS_CLOCK:
            if (!user_range_ok((const void *) r->ebx, sizeof(uclock_t))) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) rtc_read((uclock_t *) r->ebx);
            break;

        case SYS_BRK:
            r->eax = task_brk(r->ebx);
            break;

        case SYS_DELETEFILE:
            if (user_str_ok((const char *) r->ebx, USER_STR_MAX) < 0) {
                r->eax = EFAULT;
                break;
            }
            r->eax = (uint32_t) fs_delete((char *) r->ebx);
            break;

        case SYS_DOZE:
            idle_doze();
            r->eax = 0;
            break;

        case SYS_SURFACE:
            r->eax = task_surface_alloc(r->ebx, r->ecx);
            break;

        case SYS_SURFFREE:
            r->eax = (uint32_t) task_surface_free(r->ebx);
            break;

        case SYS_EVENT_WAIT: {
            if (!user_range_ok((const void *) r->ebx, sizeof(uevent_t))) {
                r->eax = EFAULT;
                break;
            }
            /* Become the listener first, then move any keys that were typed
             * (or injected) before we listened out of the ring into the
             * queue, so nothing is stranded between the two paths. */
            task_event_listen();
            /* Leave overflow in the ring. A burst longer than the queue
             * (kbd_inject before listen) must survive until the next wait. */
            while (task_event_room() > 0) {
                char c = kbd_getchar();
                if (!c) break;
                task_event_key(c);
            }
            r->eax = (uint32_t) task_event_wait((uevent_t *) r->ebx, r->ecx);
            break;
        }

        case SYS_FB:
            syscall_fb(r);
            break;

        case SYS_EXIT:
            task_exit(); /* terminates the task and reschedules; never returns */
            break;

        default:
            r->eax = (uint32_t) -1;
            break;
    }
}

void syscall_init() {
    register_interrupt_handler(0x80, syscall_handler);
}
