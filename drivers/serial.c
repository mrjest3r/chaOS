#include "serial.h"
#include "../cpu/ports.h"

#define COM1 0x3F8

static int initialized = 0;

int init_serial() {
    port_byte_out(COM1 + 1, 0x00); /* Disable all interrupts */
    port_byte_out(COM1 + 3, 0x80); /* Enable DLAB (set baud rate divisor) */
    port_byte_out(COM1 + 0, 0x03); /* Divisor low byte  (3 => 38400 baud) */
    port_byte_out(COM1 + 1, 0x00); /* Divisor high byte */
    port_byte_out(COM1 + 3, 0x03); /* 8 bits, no parity, one stop bit */
    port_byte_out(COM1 + 2, 0xC7); /* Enable FIFO, clear, 14-byte threshold */
    port_byte_out(COM1 + 4, 0x0B); /* IRQs enabled, RTS/DSR set */

    /* Loopback test: write a byte and check we read it back. */
    port_byte_out(COM1 + 4, 0x1E);
    port_byte_out(COM1 + 0, 0xAE);
    if (port_byte_in(COM1 + 0) != 0xAE) {
        return 1;
    }

    /* Back to normal (non-loopback) operation. */
    port_byte_out(COM1 + 4, 0x0F);
    initialized = 1;
    return 0;
}

static int is_transmit_empty() {
    return port_byte_in(COM1 + 5) & 0x20;
}

void serial_write_char(char c) {
    if (!initialized) return;
    while (is_transmit_empty() == 0) { }
    port_byte_out(COM1, (unsigned char) c);
}

void serial_write(const char *s) {
    int i = 0;
    while (s[i] != '\0') {
        if (s[i] == '\n') serial_write_char('\r'); /* CRLF for terminals */
        serial_write_char(s[i]);
        i++;
    }
}
