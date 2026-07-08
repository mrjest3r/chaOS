#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>

/* Kernel heap lives in the identity-mapped region [1 MiB, 4 MiB). */
#define KHEAP_START 0x00100000
#define KHEAP_SIZE  0x00300000 /* 3 MiB */

void init_kheap();

void *malloc(uint32_t size);
void free(void *ptr);

/* Fills in current heap usage (bytes in use, bytes free, block count). */
void kheap_stats(uint32_t *used, uint32_t *freebytes, uint32_t *blocks);

#endif
