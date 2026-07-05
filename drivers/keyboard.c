#include "keyboard.h"
#include "../cpu/ports.h"
#include "../cpu/isr.h"
#include "screen.h"
#include "../libc/string.h"
#include "../libc/function.h"
#include "../kernel/kernel.h"
#include <stdint.h>

#define BACKSPACE 0x0E
#define ENTER     0x1C
#define LSHIFT    0x2A
#define RSHIFT    0x36
#define CAPS      0x3A

#define KEY_BUFFER_SIZE 256
static char key_buffer[KEY_BUFFER_SIZE];

static uint8_t shift = 0;
static uint8_t caps  = 0;

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
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', '?', '?', '?', ' '
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

    if (scancode == BACKSPACE) {
        if (strlen(key_buffer) > 0) {
            backspace(key_buffer);
            kprint_backspace();
        }
        return;
    }

    if (scancode == ENTER) {
        kprint("\n");
        user_input(key_buffer); /* hand the finished line to the kernel */
        key_buffer[0] = '\0';
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
