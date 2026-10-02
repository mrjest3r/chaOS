#ifndef MEMSIZE_H
#define MEMSIZE_H

#include <stdint.h>

/* Bytes of RAM the OS will use: CMOS size, at least 16 MiB, at most 1 GiB. */
uint32_t mem_installed_bytes(void);

#endif
