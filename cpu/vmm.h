#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* ---- physical frame pool -------------------------------------------------
 * Frames handed out for user programs (their code/data/stack) and for the page
 * directories/tables of per-process address spaces. It sits above the kernel
 * heap (which ends at 4 MiB) and below the identity-map limit (16 MiB), so the
 * kernel can always reach a frame directly through its identity mapping. */
#define FRAME_POOL_START 0x00400000u  /* 4 MiB  */
#define FRAME_POOL_END   0x01000000u  /* 16 MiB */

/* ---- per-process user virtual layout -------------------------------------
 * Every process maps its program and stack inside one 4 MiB window at 1 GiB,
 * far above the kernel's identity map. Because each process has its own page
 * directory, they can all use these same virtual addresses while being backed
 * by different physical frames - true isolation. */
#define USER_BASE        0x40000000u  /* programs are linked to run here      */
#define USER_STACK_TOP   0x40400000u  /* top of the user window (stack grows  */
                                      /* down from here)                      */
#define USER_STACK_PAGES 4            /* 16 KiB user stack                    */
#define USER_PROG_LIMIT  (USER_STACK_TOP - USER_STACK_PAGES * 0x1000u)

/* Zeroes the frame bitmap (all frames free). Call once after paging is up. */
void vmm_init(void);

/* Allocate / release a single 4 KiB physical frame from the pool. frame_alloc
 * returns the frame's physical address (== its kernel-virtual address, since
 * the pool is identity-mapped) or 0 when the pool is exhausted. */
uint32_t frame_alloc(void);
void     frame_free(uint32_t phys);

/* Number of frames currently in use / total (for diagnostics). */
uint32_t frame_used(void);
uint32_t frame_total(void);

/* Create a new address space: a fresh page directory that shares the kernel's
 * identity mapping but has an empty user region. Returns the directory's
 * physical address (suitable for CR3), or 0 on failure. */
uint32_t vmm_create_addrspace(void);

/* Free every user frame, page table and the page directory itself. The shared
 * kernel mappings are left untouched. Must run with the kernel identity map
 * reachable (i.e. not while this directory is the only thing in CR3). */
void vmm_destroy_addrspace(uint32_t pd_phys);

/* Map one page: virtual 'vaddr' -> physical 'paddr' with 'flags' in the address
 * space 'pd_phys'. Allocates a page table from the frame pool if needed.
 * Returns 0 on success, -1 if out of frames. */
int vmm_map(uint32_t pd_phys, uint32_t vaddr, uint32_t paddr, uint32_t flags);

#endif
