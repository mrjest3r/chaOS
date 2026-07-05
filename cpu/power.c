#include "power.h"
#include "ports.h"

void power_off() {
    /* QEMU >= 2.0 ACPI shutdown */
    port_word_out(0x604, 0x2000);
    /* Older QEMU / Bochs */
    port_word_out(0xB004, 0x2000);
    /* VirtualBox */
    port_word_out(0x4004, 0x3400);
    /* If we're still running, just stop. */
    asm volatile("cli; hlt");
}

void qemu_debug_exit(uint8_t code) {
    port_byte_out(0xF4, code);
}
