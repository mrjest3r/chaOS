#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* Primary IDE channel, master drive (QEMU -hda), LBA28 PIO mode. */

/* Probes the primary master. Returns 1 if a disk is present, 0 otherwise. */
int init_ata();

/* LBA28 capacity from IDENTIFY, or 0 if the drive did not report one. */
uint32_t ata_sector_count(void);

/* Read/write 'count' 512-byte sectors starting at 'lba'.
 * Each sector is its own PIO command (MAP invariant 4 — VirtualBox).
 * Return 0 on success, -1 on error. */
int ata_read_sectors(uint32_t lba, uint8_t count, uint8_t *buffer);
int ata_write_sectors(uint32_t lba, uint8_t count, const uint8_t *buffer);

#endif
