#ifndef POWER_H
#define POWER_H

#include <stdint.h>

/* Powers the machine off via ACPI (works on QEMU/Bochs/VirtualBox).
 * Halts if none of the methods are available. */
void power_off();

/* Exits QEMU when the 'isa-debug-exit' device is present (port 0xF4).
 * QEMU terminates with exit status (code << 1) | 1. Used by automated tests
 * so the emulator stops on its own instead of needing to be killed. */
void qemu_debug_exit(uint8_t code);

#endif
