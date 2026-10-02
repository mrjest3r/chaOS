#include "power.h"
#include "ports.h"

void power_reboot(void) {
    asm volatile("cli");
    uint8_t status = 0x02;
    int spins = 100000;
    while ((status & 0x02) && spins--) status = port_byte_in(0x64);
    port_byte_out(0x64, 0xFE);
    for (;;) asm volatile("hlt");
}

void power_off() {
    /* No preemption / NMIs while we tear down. */
    asm volatile("cli");

    /* QEMU >= 2.0 ACPI shutdown */
    port_word_out(0x604, 0x2000);
    /* Older QEMU / Bochs */
    port_word_out(0xB004, 0x2000);
    /* VirtualBox */
    port_word_out(0x4004, 0x3400);

    /* Bochs/old ACPI */
    port_word_out(0xB004, 0x2000);

    /* Keyboard-controller pulse reset as last ditch (same as reboot). */
    {
        uint8_t status = 0x02;
        int spins = 100000;
        while ((status & 0x02) && spins--) status = port_byte_in(0x64);
        port_byte_out(0x64, 0xFE);
    }

    /* Still here: idle forever (single hlt returns on NMI and looked like a hang). */
    for (;;) asm volatile("hlt");
}

void qemu_debug_exit(uint8_t code) {
    port_byte_out(0xF4, code);
}
