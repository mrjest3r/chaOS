#include "syscall.h"
#include "task.h"
#include "fs.h"
#include "elf.h"
#include "pipe.h"
#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/fb.h"
#include "../drivers/idle.h"
#include "fb_user.h"
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
            if (caret > 0) caret--;
            continue;
        }
        if (c == 0x12) { /* right */
            if (caret < len) caret++;
            continue;
        }
        if (c == 0x13) { caret = 0; continue; } /* home */
        if (c == 0x14) { caret = len; continue; } /* end */

        if (c == '\b') {
            if (caret > 0) {
                for (int i = caret - 1; i < len - 1; i++) buf[i] = buf[i + 1];
                len--;
                caret--;
                buf[len] = '\0';
                /* Simple echo: redraw not available in VGA path easily —
                 * backspace visually once when caret was at end. */
                if (caret == len) con_backspace();
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
            if (caret == len) {
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
     * before it is dereferenced; anything outside fails with -1. */
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
            char c;
            while ((c = kbd_getchar()) != 0) task_event_key(c);
            r->eax = (uint32_t) task_event_wait((uevent_t *) r->ebx, r->ecx);
            break;
        }

        case SYS_FB: {
            fb_req_t *req = (fb_req_t *) r->ebx;
            if (!user_range_ok(req, sizeof(fb_req_t))) {
                r->eax = EFAULT;
                break;
            }
            if (!fb_available()) {
                r->eax = (uint32_t) -1;
                break;
            }
            /* F1 console fallback: leave INFO/RESTORE alone so the compositor
             * can still query the screen and we can restore after F1 again. */
            if (kbd_console_override() &&
                req->op != FBOP_INFO && req->op != FBOP_RESTORE) {
                r->eax = 0;
                break;
            }
            switch (req->op) {
                case FBOP_INFO:
                    req->width = fb_width();
                    req->height = fb_height();
                    req->pitch = fb_pitch();
                    req->ok = 1;
                    r->eax = 0;
                    break;
                case FBOP_FILL:
                    fb_fill_rect(req->x, req->y, req->w, req->h, req->color);
                    r->eax = 0;
                    break;
                case FBOP_TEXT:
                    req->text[sizeof(req->text) - 1] = '\0';
                    fb_draw_string(req->x, req->y, req->text,
                                   req->color, req->color2);
                    r->eax = 0;
                    break;
                case FBOP_CLEAR:
                    fb_clear(req->color);
                    r->eax = 0;
                    break;
                case FBOP_RESTORE:
                    fb_console_full_redraw();
                    r->eax = 0;
                    break;
                case FBOP_BLIT: {
                    int w = req->w, h = req->h, pitch = req->src_pitch;
                    if (w <= 0 || h <= 0 || w > FB_BLIT_MAX || h > FB_BLIT_MAX ||
                        pitch < w || pitch > FB_BLIT_MAX) {
                        r->eax = (uint32_t) -1;
                        break;
                    }
                    /* Bounded above by 4096*4096*4 — no overflow. */
                    uint32_t bytes = (uint32_t) h * (uint32_t) pitch * 4u;
                    if (!user_range_ok(req->src, bytes)) {
                        r->eax = EFAULT;
                        break;
                    }
                    fb_blit(req->x, req->y, w, h, req->src, pitch);
                    r->eax = 0;
                    break;
                }
                default:
                    r->eax = (uint32_t) -1;
                    break;
            }
            break;
        }

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
