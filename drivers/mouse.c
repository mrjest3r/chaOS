#include "mouse.h"
#include "fb.h"
#include "idle.h"
#include "../cpu/ports.h"
#include "../cpu/isr.h"
#include "../libc/function.h"
#include "serial.h"
#include "../kernel/shell.h"
#include "../kernel/task.h"
#include "keyboard.h"
#include <stdint.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64
#define KBD_CMD    0x64

static int mouse_ok = 0;
static int cursor_on = 0;
static int cursor_drawn = 0;
static int32_t mx = 0, my = 0;
static uint32_t mbuttons = 0;
static uint32_t prev_buttons = 0;
static int packet_idx = 0;
static uint8_t packet[3];

/* Simple 8x8 arrow-ish cursor (1 = draw). */
static const uint8_t cursor_bits[8] = {
    0x80, 0xC0, 0xE0, 0xF0, 0xF8, 0xFC, 0xC0, 0x80
};
#define CURSOR_W 8
#define CURSOR_H 8

static void mouse_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if ((port_byte_in(KBD_STATUS) & 0x02) == 0) return;
}

static void mouse_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (port_byte_in(KBD_STATUS) & 0x01) return;
}

static void mouse_write(uint8_t val) {
    mouse_wait_write();
    port_byte_out(KBD_CMD, 0xD4);
    mouse_wait_write();
    port_byte_out(KBD_DATA, val);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return port_byte_in(KBD_DATA);
}

static void cursor_xor_at(int x, int y) {
    if (!fb_available()) return;
    int max_x = (int) fb_width();
    int max_y = (int) fb_height();
    for (int row = 0; row < CURSOR_H; row++) {
        uint8_t bits = cursor_bits[row];
        for (int col = 0; col < CURSOR_W; col++) {
            if (!(bits & (0x80u >> col))) continue;
            int px = x + col;
            int py = y + row;
            if (px < 0 || py < 0 || px >= max_x || py >= max_y) continue;
            uint32_t c = fb_get_pixel(px, py);
            fb_put_pixel(px, py, c ^ 0x00FFFFFFu);
        }
    }
}

static void cursor_erase(void) {
    if (cursor_on && cursor_drawn) {
        cursor_xor_at(mx, my);
        cursor_drawn = 0;
    }
}

static void cursor_draw(void) {
    if (!cursor_on || !fb_available()) return;
    cursor_xor_at(mx, my);
    cursor_drawn = 1;
}

static void clamp_pos(void) {
    int max_x = fb_available() ? (int) fb_width() - 1 : 799;
    int max_y = fb_available() ? (int) fb_height() - 1 : 599;
    if (max_x < 0) max_x = 0;
    if (max_y < 0) max_y = 0;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > max_x) mx = max_x;
    if (my > max_y) my = max_y;
}

static void apply_packet(uint8_t b0, uint8_t b1, uint8_t b2, int deliver_click) {
    /* Sync bit must be set on first byte. */
    if (!(b0 & 0x08)) return;

    int32_t dx = (int32_t) b1;
    int32_t dy = (int32_t) b2;
    if (b0 & 0x10) dx -= 256;
    if (b0 & 0x20) dy -= 256;
    /* Discard overflow packets. */
    if (b0 & 0xC0) return;

    cursor_erase();
    mx += dx;
    my -= dy; /* PS/2 Y+ is up; screen Y+ is down */
    clamp_pos();
    uint32_t nb = b0 & 0x07;
    /* Caret placement belongs to the kernel shell; a program with focus
     * (or an event listener) must not have its screen repainted under it. */
    if (deliver_click && (nb & 1) && !(prev_buttons & 1) && kbd_focus_is_shell())
        shell_input_click(mx, my);
    prev_buttons = mbuttons = nb;
    cursor_draw();
    if (!kbd_console_override())
        task_event_mouse(mx, my, nb);
}

static void mouse_callback(registers_t *regs) {
    uint8_t status = port_byte_in(KBD_STATUS);
    if (!(status & 0x01)) { UNUSED(regs); return; }
    /* Only consume auxiliary (mouse) bytes here. */
    if (!(status & 0x20)) { UNUSED(regs); return; }

    uint8_t data = port_byte_in(KBD_DATA);
    if (!mouse_ok) { UNUSED(regs); return; }

    if (packet_idx == 0 && !(data & 0x08)) {
        UNUSED(regs);
        return; /* wait for sync */
    }
    packet[packet_idx++] = data;
    if (packet_idx >= 3) {
        /* Motion/click wakes a blanked display; swallow that packet. */
        if (idle_wake_if_blank()) {
            packet_idx = 0;
            UNUSED(regs);
            return;
        }
        idle_touch();
        apply_packet(packet[0], packet[1], packet[2], 1);
        packet_idx = 0;
    }
    UNUSED(regs);
}

void init_mouse(void) {
    mouse_ok = 0;
    packet_idx = 0;
    mx = my = 0;
    mbuttons = 0;

    mouse_wait_write();
    port_byte_out(KBD_CMD, 0xA8); /* enable auxiliary device */

    mouse_wait_write();
    port_byte_out(KBD_CMD, 0x20); /* get Compaq status byte */
    mouse_wait_read();
    uint8_t status = port_byte_in(KBD_DATA);
    status |= 0x02;  /* enable IRQ12 */
    status &= (uint8_t) ~0x20; /* clear disable mouse clock */
    mouse_wait_write();
    port_byte_out(KBD_CMD, 0x60);
    mouse_wait_write();
    port_byte_out(KBD_DATA, status);

    mouse_write(0xF6); /* set defaults */
    (void) mouse_read(); /* ACK */
    mouse_write(0xF4); /* enable streaming */
    (void) mouse_read();

    register_interrupt_handler(IRQ12, mouse_callback);
    mouse_ok = 1;
    serial_write("[mouse] PS/2 streaming enabled\n");
}

void mouse_enable_cursor(void) {
    if (!mouse_ok || !fb_available()) return;
    cursor_on = 1;
    mx = (int32_t) (fb_width() / 2);
    my = (int32_t) (fb_height() / 2);
    cursor_drawn = 0;
    cursor_draw();
}

void mouse_fb_cleared(void) {
    /* Framebuffer was wiped; XOR underlay is gone — do not erase. */
    cursor_drawn = 0;
    if (cursor_on) cursor_draw();
}

void mouse_cursor_hide(void) {
    cursor_erase();
}

void mouse_cursor_show(void) {
    if (cursor_on && !cursor_drawn) cursor_draw();
}

int mouse_cursor_overlaps(int x, int y, int w, int h) {
    if (!cursor_on || !cursor_drawn) return 0;
    return !(x >= mx + CURSOR_W || x + w <= mx ||
             y >= my + CURSOR_H || y + h <= my);
}

void mouse_get(mouse_state_t *out) {
    if (!out) return;
    out->x = mx;
    out->y = my;
    out->buttons = mbuttons;
}

void mouse_inject_packet(uint8_t b0, uint8_t b1, uint8_t b2) {
    apply_packet(b0, b1, b2, 0);
}

int mouse_available(void) {
    return mouse_ok;
}
