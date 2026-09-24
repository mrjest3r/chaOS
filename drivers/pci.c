#include "pci.h"
#include "../cpu/ports.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t) bus << 16)
                  | ((uint32_t) dev << 11)
                  | ((uint32_t) fn  << 8)
                  | (reg & 0xFCu);
    port_dword_out(PCI_CONFIG_ADDR, addr);
    return port_dword_in(PCI_CONFIG_DATA);
}

int pci_find_device(uint16_t vendor, uint16_t device, uint32_t *bar0) {
    /* Bus 0 is enough for the emulators we target (QEMU, VirtualBox). */
    for (uint8_t dev = 0; dev < 32; dev++) {
        uint32_t id = pci_config_read32(0, dev, 0, 0x00);
        if ((id & 0xFFFFu) != vendor) continue;
        if ((id >> 16) != device) continue;

        uint32_t bar = pci_config_read32(0, dev, 0, 0x10);
        if (bar & 0x1) return -1;   /* I/O BAR: not a memory framebuffer */
        *bar0 = bar & ~0xFu;
        return 0;
    }
    return -1;
}
