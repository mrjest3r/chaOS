#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE    0x1000  /* 4 KiB */
#define PAGE_ENTRIES 1024    /* entries per directory / table */

/* How much low memory we identity-map. The kernel + heap live in the first
 * 4 MiB; the region above that (up to this limit) holds the physical frame
 * pool that backs per-process address spaces. */
#define IDENTITY_MAP_MB 16
#define MAPPED_LIMIT    (IDENTITY_MAP_MB * 0x100000u)

/* Page directory / table entry flags */
#define PAGE_PRESENT 0x1
#define PAGE_RW      0x2
#define PAGE_USER    0x4

/* The kernel's page directory (physical address). Every per-process address
 * space shares its kernel mappings, and kernel threads run directly on it. */
extern uint32_t *kernel_directory;

/* Implemented in paging_asm.asm */
void load_page_directory(uint32_t *page_directory);
void enable_paging();

/* Identity-maps the first IDENTITY_MAP_MB MiB, registers the page-fault handler
 * and turns paging on. */
void init_paging();

#endif
