#ifndef ATAPI_H
#define ATAPI_H

#include <stdint.h>

/* CD-ROM on the IDE bus. One 2048-byte sector per call.
 * Returns 1 if a drive was found. */
int atapi_init(void);

/* Read one 2048-byte sector at the ISO9660 LBA. Returns 0 or -1. */
int atapi_read(uint32_t lba, uint8_t *buf);

#endif
