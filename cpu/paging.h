#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

#define PAGE_SIZE    0x1000  /* 4 KiB */
#define PAGE_ENTRIES 1024    /* entries per directory / table */

/* The boot stub maps 16 MiB with 4 MiB pages. init_paging() rebuilds a
 * 4 KiB map of installed RAM, capped at 1 GiB. Only the first 16 MiB is
 * aliased at 0xC0000000: a full 1 GiB alias would cover the framebuffer. */
#define HIGH_ALIAS_MB 16
#define MEM_CAP_MB    1024

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

/* Builds the identity map (up to 1 GiB) and the 16 MiB higher-half alias,
 * registers the page-fault handler, and turns paging on. */
void init_paging(void);

/* Bytes covered by the identity map after init_paging(). */
uint32_t mem_mapped_bytes(void);

#endif
