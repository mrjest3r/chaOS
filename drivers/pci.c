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

static void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t fn,
                               uint8_t reg, uint32_t value) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t) bus << 16)
                  | ((uint32_t) dev << 11)
                  | ((uint32_t) fn  << 8)
                  | (reg & 0xFCu);
    port_dword_out(PCI_CONFIG_ADDR, addr);
    port_dword_out(PCI_CONFIG_DATA, value);
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

int pci_find_class(uint8_t class, uint8_t subclass, uint8_t progif,
                   uint8_t *dev_out, uint8_t *fn_out, uint32_t *bar0) {
    for (uint8_t dev = 0; dev < 32; dev++) {
        for (uint8_t fn = 0; fn < 8; fn++) {
            uint32_t id = pci_config_read32(0, dev, fn, 0x00);
            if ((id & 0xFFFFu) == 0xFFFFu) continue;
            uint32_t cls = pci_config_read32(0, dev, fn, 0x08);
            if ((cls >> 24) != class) continue;
            if (((cls >> 16) & 0xFFu) != subclass) continue;
            if (((cls >> 8) & 0xFFu) != progif) continue;
            *dev_out = dev;
            *fn_out = fn;
            *bar0 = pci_config_read32(0, dev, fn, 0x10);
            return 0;
        }
    }
    return -1;
}

void pci_enable(uint8_t dev, uint8_t fn, int mem, int io, int bus_master) {
    uint32_t cmd = pci_config_read32(0, dev, fn, 0x04);
    if (mem) cmd |= 2u;
    if (io) cmd |= 1u;
    if (bus_master) cmd |= 4u;
    pci_config_write32(0, dev, fn, 0x04, cmd);
}
