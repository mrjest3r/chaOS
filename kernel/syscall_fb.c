#include "syscall.h"
#include "uaccess.h"
#include "fb_user.h"
#include "../cpu/arch.h"
#include "../drivers/fb.h"
#include "../drivers/keyboard.h"
#include "../drivers/serial.h"

#define EFAULT ((uint32_t) -1)

/* SYS_FB (18) — split out of the syscall switch. */
void syscall_fb(void *regs) {
    registers_t *r = (registers_t *) regs;
    fb_req_t *req = (fb_req_t *) r->ebx;
    if (!user_range_ok(req, sizeof(fb_req_t))) {
        r->eax = EFAULT;
        return;
    }
    if (!fb_available()) {
        r->eax = (uint32_t) -1;
        return;
    }
    if (kbd_console_override() &&
        req->op != FBOP_INFO && req->op != FBOP_RESTORE) {
        r->eax = 0;
        return;
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
            fb_draw_string(req->x, req->y, req->text, req->color, req->color2);
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
            static int blit_log;
            if (w <= 0 || h <= 0 || w > FB_BLIT_MAX || h > FB_BLIT_MAX ||
                pitch < w || pitch > FB_BLIT_MAX) {
                if (blit_log < 4) {
                    serial_write("[fb] blit rejected\n");
                    blit_log++;
                }
                r->eax = (uint32_t) -1;
                break;
            }
            uint32_t bytes = (uint32_t) h * (uint32_t) pitch * 4u;
            if (!user_range_ok(req->src, bytes)) {
                if (blit_log < 4) {
                    serial_write("[fb] blit pointer rejected\n");
                    blit_log++;
                }
                r->eax = EFAULT;
                break;
            }
            fb_blit(req->x, req->y, w, h, req->src, pitch);
            if (blit_log < 4) {
                serial_write("[fb] blit ok\n");
                blit_log++;
            }
            r->eax = 0;
            break;
        }
        default:
            r->eax = (uint32_t) -1;
            break;
    }
}
