#include "idle.h"
#include "screen.h"
#include "fb.h"
#include "mouse.h"
#include "keyboard.h"
#include "../cpu/timer.h"
#include "../kernel/kheap.h"
#include <stdint.h>

/* Use the same VGA mapping as the console driver. */
#ifndef VIDEO_ADDRESS
#define VIDEO_ADDRESS 0xb8000
#endif

/* PIT runs at 50 Hz (cpu/isr.c). 30 seconds of silence -> blank. */
#define IDLE_DEFAULT_TICKS 1500u

static int blanked = 0;
static uint32_t last_touch = 0;
static uint32_t timeout_ticks = IDLE_DEFAULT_TICKS;

/* VGA: save screen under the blank so wake restores it. */
static uint8_t *vga_save = 0;
#define VGA_BYTES (80 * 25 * 2)

static void blank_draw(void) {
    if (fb_available()) {
        mouse_cursor_hide();
        fb_clear(0x00000000u);
        return;
    }
    /* VGA text: stash then black out. */
    uint8_t *vid = (uint8_t *) VIDEO_ADDRESS;
    if (!vga_save)
        vga_save = (uint8_t *) malloc(VGA_BYTES);
    if (vga_save) {
        for (int i = 0; i < VGA_BYTES; i++) vga_save[i] = vid[i];
    }
    for (int i = 0; i < VGA_BYTES; i += 2) {
        vid[i] = ' ';
        vid[i + 1] = 0x00; /* black on black */
    }
}

static void blank_restore(void) {
    if (fb_available()) {
        fb_console_full_redraw();
        mouse_cursor_show();
        return;
    }
    uint8_t *vid = (uint8_t *) VIDEO_ADDRESS;
    if (vga_save) {
        for (int i = 0; i < VGA_BYTES; i++) vid[i] = vga_save[i];
    }
}

void idle_init(void) {
    blanked = 0;
    last_touch = timer_ticks();
#ifdef AUTOTEST
    timeout_ticks = 0; /* no auto-blank during the test suite */
#else
    timeout_ticks = IDLE_DEFAULT_TICKS;
#endif
}

void idle_set_timeout(uint32_t ticks) {
    timeout_ticks = ticks;
}

uint32_t idle_get_timeout(void) {
    return timeout_ticks;
}

int idle_is_blank(void) {
    return blanked;
}

void idle_touch(void) {
    last_touch = timer_ticks();
    if (blanked) {
        blanked = 0;
        blank_restore();
    }
}

int idle_wake_if_blank(void) {
    if (!blanked) return 0;
    blanked = 0;
    last_touch = timer_ticks();
    blank_restore();
    return 1;
}

void idle_blank(void) {
    if (blanked) return;
    blank_draw();
    blanked = 1;
}

void idle_doze(void) {
    idle_blank();
    /* Wait for wake via IRQ path (keyboard/mouse call idle_wake_if_blank). */
    while (blanked) {
        asm volatile("sti; hlt");
    }
}

void idle_poll(void) {
    if (blanked) return;
    if (timeout_ticks == 0) return;
    /* Do not auto-blank while a user program owns the keyboard. */
    if (!kbd_focus_is_shell()) return;

    uint32_t now = timer_ticks();
    if (now - last_touch >= timeout_ticks)
        idle_blank();
}
