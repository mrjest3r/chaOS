#include "keyboard.h"
#include "../cpu/ports.h"
#include "../cpu/isr.h"
#include "screen.h"
#include "../libc/string.h"
#include "../libc/function.h"
#include "../kernel/shell.h"
#include <stdint.h>

#define BACKSPACE 0x0E
#define ENTER     0x1C
#define LSHIFT    0x2A
#define RSHIFT    0x36
#define CAPS      0x3A
#define PGUP      0x49
#define PGDN      0x51

#define KEY_BUFFER_SIZE 256
static char key_buffer[KEY_BUFFER_SIZE];

static uint8_t shift = 0;
static uint8_t caps  = 0;

/* Where keystrokes are routed. The shell owns the keyboard by default; while
 * 'exec' runs a foreground program the focus is handed to it. */
static int kbd_focus = KBD_FOCUS_SHELL;

/* Ring buffer of raw characters for user programs (SYS_GETCHAR/SYS_READLINE).
 * Written from the IRQ handler, read from syscall context; single producer,
 * single consumer, so plain volatile indices are enough. */
#define RING_SIZE 256
static char ring[RING_SIZE];
static volatile uint32_t ring_head = 0; /* next write position */
static volatile uint32_t ring_tail = 0; /* next read position  */

static void ring_push(char c) {
    uint32_t next = (ring_head + 1) % RING_SIZE;
    if (next == ring_tail) return; /* full: drop the key */
    ring[ring_head] = c;
    ring_head = next;
}

/* Note: no buffer reset on focus change. Shell-mode typing goes to the shell's
 * line editor, never into this ring, so there are no stale keys to flush - and
 * self-tests pre-fill the ring with kbd_inject() before exec'ing a program. */
void kbd_set_focus(int focus) {
    kbd_focus = focus;
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
    while (*s) ring_push(*s++);
}

#define SC_MAX 57

/* Unshifted characters, indexed by scancode. '?' marks a key we don't map. */
static const char sc_ascii[] = {
    '?', '?', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '?', '?',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '?', '?',
    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '?', '\\',
    'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', '?', '?', '?', ' '
};

/* Characters produced while Shift is held. */
static const char sc_ascii_shift[] = {
    '?', '?', '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '?', '?',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '?', '?',
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~', '?', '|',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '?', '?', ' '
};

static void keyboard_callback(registers_t *regs) {
    uint8_t scancode = port_byte_in(0x60);

    /* High bit set => key release. We only care about Shift being released. */
    if (scancode & 0x80) {
        uint8_t released = scancode & 0x7F;
        if (released == LSHIFT || released == RSHIFT) shift = 0;
        UNUSED(regs);
        return;
    }

    /* Modifier keys. */
    if (scancode == LSHIFT || scancode == RSHIFT) { shift = 1; return; }
    if (scancode == CAPS) { caps = !caps; return; }

    /* PgUp/PgDn scroll the screen's history in every mode. (These arrive
     * either bare or after an 0xE0 prefix byte, which falls through the
     * SC_MAX check below and is ignored.) */
    if (scancode == PGUP) { screen_scroll_view(10);  return; }
    if (scancode == PGDN) { screen_scroll_view(-10); return; }

    if (scancode == BACKSPACE) {
        if (kbd_focus == KBD_FOCUS_PROGRAM) {
            ring_push('\b'); /* line editing happens in SYS_READLINE */
        } else if (strlen(key_buffer) > 0) {
            backspace(key_buffer);
            kprint_backspace();
        }
        return;
    }

    if (scancode == ENTER) {
        if (kbd_focus == KBD_FOCUS_PROGRAM) {
            ring_push('\n');
        } else {
            kprint("\n");
            shell_submit(key_buffer); /* queue for the main loop; do not run here */
            key_buffer[0] = '\0';
        }
        return;
    }

    if (scancode > SC_MAX) return;

    char base = sc_ascii[(int) scancode];
    if (base == '?') return; /* unmapped key */

    char letter;
    if (base >= 'a' && base <= 'z') {
        /* Letters: Shift XOR CapsLock selects uppercase. */
        letter = (shift ^ caps) ? sc_ascii_shift[(int) scancode] : base;
    } else {
        letter = shift ? sc_ascii_shift[(int) scancode] : base;
    }

    if (kbd_focus == KBD_FOCUS_PROGRAM) {
        ring_push(letter); /* echo happens when the program consumes it */
        return;
    }

    if (strlen(key_buffer) < KEY_BUFFER_SIZE - 1) {
        char str[2] = { letter, '\0' };
        append(key_buffer, letter);
        kprint(str);
    }

    UNUSED(regs);
}

void init_keyboard() {
    register_interrupt_handler(IRQ1, keyboard_callback);
}
