#include "memsize.h"
#include "ports.h"

/* CMOS tells us how much RAM the BIOS enumerated. Registers:
 *   0x17/0x18  extended memory above 1 MiB, in KiB (saturates)
 *   0x34/0x35  memory above 16 MiB, in 64 KiB blocks
 * QEMU fills 0x34/0x35 for -m above 16. The published floor is 1 GiB;
 * anything past that stays unmapped so a larger PC still boots. */

#define MEM_CAP 0x40000000u /* 1 GiB */

static uint8_t cmos_read(uint8_t reg) {
    port_byte_out(0x70, (uint8_t) (reg | 0x80));
    return port_byte_in(0x71);
}

uint32_t mem_installed_bytes(void) {
    uint32_t ext_kb = (uint32_t) cmos_read(0x17) | ((uint32_t) cmos_read(0x18) << 8);
    uint32_t above = (uint32_t) cmos_read(0x34) | ((uint32_t) cmos_read(0x35) << 8);
    uint32_t bytes;
    if (above)
        bytes = 16u * 1024u * 1024u + above * 64u * 1024u;
    else
        bytes = 1024u * 1024u + ext_kb * 1024u;
    if (bytes < 16u * 1024u * 1024u) bytes = 16u * 1024u * 1024u;
    if (bytes > MEM_CAP) bytes = MEM_CAP;
    return bytes;
}
