#ifndef PCI_H
#define PCI_H

#include <stdint.h>

/* Minimal PCI configuration-space access (mechanism #1, ports 0xCF8/0xCFC).
 * Just enough to locate a device by vendor/device id and read its BARs. */

uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t fn, uint8_t reg);

/* Scans bus 0 for vendor:device. Returns 0 and fills *bar0 (memory BAR,
 * low flag bits cleared) on success, -1 if not found. */
int pci_find_device(uint16_t vendor, uint16_t device, uint32_t *bar0);

#endif
