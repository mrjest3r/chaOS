#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* Minimal PCI configuration-space access (mechanism #1, ports 0xCF8/0xCFC).
 * Just enough to locate a device by vendor/device id and read its BARs. */

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);

/* Scans bus 0 for vendor:device. Returns 0 and fills *bar0 (memory BAR,
 * low flag bits cleared) on success, -1 if not found. */
int pci_find_device(uint16_t vendor, uint16_t device, uint32_t *bar0);

/* Class/subclass/programming-interface match on bus 0. *bar0 is the raw BAR
 * (bit 0 set means I/O). Returns 0 and the device coordinates, or -1. */
int pci_find_class(uint8_t class, uint8_t subclass, uint8_t progif,
                   uint8_t *dev, uint8_t *fn, uint32_t *bar0);

/* Command-register bits: memory decode, I/O decode, bus master. */
void pci_enable(uint8_t dev, uint8_t fn, int mem, int io, int bus_master);

#endif
