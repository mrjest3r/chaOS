#include "keyboard.h"
#include "../cpu/ports.h"
#include "../cpu/isr.h"
#include "screen.h"
#include "fb.h"
#include "idle.h"
#include "../libc/string.h"
#include "../libc/function.h"
#include "../kernel/shell.h"
#include "../kernel/task.h"
#include <stdint.h>

#define BACKSPACE 0x0E
#define ENTER     0x1C
#define LSHIFT    0x2A
#define RSHIFT    0x36
#define CAPS      0x3A
#define PGUP      0x49
#define PGDN      0x51
#define HOME      0x47
#define END       0x4F
#define ARROW_UP  0x48
#define ARROW_LEFT  0x4B
#define ARROW_RIGHT 0x4D
#define ARROW_DOWN  0x50
#define F1          0x3B

static uint8_t shift = 0;
static uint8_t caps  = 0;
static uint8_t e0_prefix = 0;

/* Where keystrokes are routed. The shell owns the keyboard by default; while
 * 'exec' runs a foreground program the focus is handed to it. */
static int kbd_focus = KBD_FOCUS_SHELL;
static int console_override = 0;
static int saved_focus = KBD_FOCUS_SHELL;

/* Ring buffer of raw characters for user programs (SYS_GETCHAR/SYS_READLINE).
 * Navigation bytes for the readline caret share values with UEV_KEY_*. */
#define RING_SIZE 256
#define KBD_LEFT  UEV_KEY_LEFT
#define KBD_RIGHT UEV_KEY_RIGHT
#define KBD_HOME  UEV_KEY_HOME
#define KBD_END   UEV_KEY_END

static char ring[RING_SIZE];
static volatile uint32_t ring_head = 0;
static volatile uint32_t ring_tail = 0;

static void ring_push(char c) {
    uint32_t next = (ring_head + 1) % RING_SIZE;
    if (next == ring_tail) return;
    ring[ring_head] = c;
    ring_head = next;
}

/* Program-bound character: an event listener (SYS_EVENT_WAIT) takes it,
 * otherwise it queues for SYS_GETCHAR / SYS_READLINE. */
static void deliver(char c) {
    if (task_event_key(c)) return;
    ring_push(c);
}

void kbd_set_focus(int focus) {
    kbd_focus = focus;
}

int kbd_focus_is_shell(void) {
    return kbd_focus == KBD_FOCUS_SHELL;
}

int kbd_console_override(void) {
    return console_override;
}

void kbd_set_console_override(int on) {
    if (on) {
        if (console_override) return;
        saved_focus = kbd_focus;
        kbd_focus = KBD_FOCUS_SHELL;
        console_override = 1;
        if (fb_available()) fb_console_full_redraw();
    } else {
        if (!console_override) return;
        console_override = 0;
        kbd_focus = saved_focus;
    }
}

int kbd_haschar() {
    return ring_head != ring_tail;
}

char kbd_getchar() {
    if (ring_head == ring_tail) return 0;
    char c = ring[ring_tail];
    ring_tail = (ring_tail + 1) % RING_SIZE;
    return c;
}

void kbd_inject(const char *s) {
    while (*s) deliver(*s++);
}

#define SC_MAX 57

static const char sc_ascii[] = {
    '?', '?', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '?', '?',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '?', '?',
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '?', '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '?', '?', '?', ' '
};

static const char sc_ascii_shift[] = {
    '?', '?', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '?', '?',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '?', '?',
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', '?', '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '?', '?', ' '
};

static void handle_nav(uint8_t scancode) {
    if (kbd_focus == KBD_FOCUS_PROGRAM) {
        if (scancode == ARROW_LEFT)  deliver(KBD_LEFT);
        else if (scancode == ARROW_RIGHT) deliver(KBD_RIGHT);
        else if (scancode == HOME) deliver(KBD_HOME);
        else if (scancode == END)  deliver(KBD_END);
        /* Up/Down scroll the console unless an event listener wants them. */
        else if (scancode == ARROW_UP) {
            if (!task_event_key(UEV_KEY_UP)) screen_scroll_view(1);
        } else if (scancode == ARROW_DOWN) {
            if (!task_event_key(UEV_KEY_DOWN)) screen_scroll_view(-1);
        }
        return;
    }
    if (scancode == ARROW_LEFT)  shell_input_left();
    else if (scancode == ARROW_RIGHT) shell_input_right();
    else if (scancode == HOME) shell_input_home();
    else if (scancode == END)  shell_input_end();
    else if (scancode == ARROW_UP) screen_scroll_view(3);
    else if (scancode == ARROW_DOWN) screen_scroll_view(-3);
}

static void keyboard_callback(registers_t *regs) {
    if (port_byte_in(0x64) & 0x20) {
        UNUSED(regs);
        return;
    }
    uint8_t scancode = port_byte_in(0x60);

    if (scancode == 0xE0) {
        e0_prefix = 1;
        UNUSED(regs);
        return;
    }

    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == LSHIFT || released == RSHIFT) shift = 0;
        e0_prefix = 0;
        UNUSED(regs);
        return;
    }

    /* Key-down wakes a blanked display; that key is swallowed. */
    if (idle_wake_if_blank()) {
        e0_prefix = 0;
        UNUSED(regs);
        return;
    }
    idle_touch();

    /* F1: peel the kernel console out from under a compositor (G5). */
    if (scancode == F1) {
        kbd_set_console_override(!console_override);
        e0_prefix = 0;
        UNUSED(regs);
        return;
    }

    if (scancode == LSHIFT || scancode == RSHIFT) { shift = 1; e0_prefix = 0; return; }
    if (scancode == CAPS) { caps = !caps; e0_prefix = 0; return; }

    if (scancode == PGUP || (e0_prefix && scancode == PGUP)) {
        screen_scroll_view(10);
        e0_prefix = 0;
        return;
    }
    if (scancode == PGDN || (e0_prefix && scancode == PGDN)) {
        screen_scroll_view(-10);
        e0_prefix = 0;
        return;
    }

    if (e0_prefix || scancode == ARROW_LEFT || scancode == ARROW_RIGHT ||
        scancode == ARROW_UP || scancode == ARROW_DOWN ||
        scancode == HOME || scancode == END) {
        handle_nav(scancode);
        e0_prefix = 0;
        return;
    }
    e0_prefix = 0;

    if (scancode == BACKSPACE) {
        if (kbd_focus == KBD_FOCUS_PROGRAM) deliver('\b');
        else shell_input_backspace();
        return;
    }

    if (scancode == ENTER) {
        if (kbd_focus == KBD_FOCUS_PROGRAM) deliver('\n');
        else shell_input_enter();
        return;
    }

    if (scancode > SC_MAX) return;

    char base = sc_ascii[(int) scancode];
    if (base == '?') return;

    char letter;
    if (base >= 'a' && base <= 'z')
        letter = (shift ^ caps) ? sc_ascii_shift[(int) scancode] : base;
    else
        letter = shift ? sc_ascii_shift[(int) scancode] : base;

    if (kbd_focus == KBD_FOCUS_PROGRAM) {
        deliver(letter);
        return;
    }
    shell_input_char(letter);
    UNUSED(regs);
}

void init_keyboard() {
    register_interrupt_handler(IRQ1, keyboard_callback);
}
