#ifndef VMM_H
#define VMM_H

#include <stdint.h>

/* ---- physical frame pool -------------------------------------------------
 * Frames handed out for user programs (their code/data/stack) and for the page
 * directories/tables of per-process address spaces. It sits above the kernel
 * heap (which ends at 4 MiB) and below the identity-map limit (16 MiB), so the
 * kernel can always reach a frame directly through its identity mapping. */
/* The frame pool is the identity-mapped RAM above the kernel heap and the
 * page tables init_paging() places at 4 MiB. Its end is set at boot. */

/* ---- per-process user virtual layout -------------------------------------
 * Every process maps its program, heap, and stack inside a 64 MiB window at
 * 1 GiB. The same virtual addresses are reused; each process has its own
 * frames. The heap grows from the end of the image toward the stack, and
 * SYS_BRK stops after 48 MiB so a game can hold its data and still
 * leave RAM for other tasks. */
#define USER_BASE         0x40000000u
#define USER_WINDOW_BYTES 0x04000000u /* 64 MiB                                */
#define USER_STACK_TOP    (USER_BASE + USER_WINDOW_BYTES)
#define USER_STACK_PAGES  16          /* 64 KiB user stack                     */
#define USER_HEAP_MAX     0x03000000u /* 48 MiB of SYS_BRK per process         */
#define USER_PROG_LIMIT   (USER_STACK_TOP - USER_STACK_PAGES * 0x1000u)

/* ---- surfaces (GUI G2) -----------------------------------------------------
 * Pixel buffers handed to ring 3 by SYS_SURFACE. Each task owns up to
 * SURF_SLOTS of them; slot n lives in its own 4 MiB page-directory entry at
 * SURF_BASE + n*SURF_SLOT_BYTES so freeing a surface can drop the whole table. */
#define SURF_BASE        0x50000000u
#define SURF_SLOT_BYTES  0x00400000u  /* 4 MiB: up to 1024x1024x4            */
#define SURF_SLOTS       4
#define SURF_END         (SURF_BASE + SURF_SLOTS * SURF_SLOT_BYTES)

/* Bitmap covers [start, end). Call once after the identity map exists. */
void vmm_set_pool(uint32_t start, uint32_t end, uint32_t *bitmap);

/* How many low page-directory entries are the shared identity map, and how
 * many are the higher-half alias of the kernel. Destroy skips both. */
void vmm_set_kernel_pdes(uint32_t identity_pdes, uint32_t high_alias_pdes);

/* Remember a kernel page-directory entry (MMIO) that every address space
 * must share. Call after the entry is present in kernel_directory. */
void vmm_share_pde(int pdi);

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

/* Tear down one whole 4 MiB page-directory entry: free every present frame in
 * its table, free the table, clear the entry. Never touches shared kernel /
 * LFB entries. The caller reloads CR3 if this directory is active. */
void vmm_unmap_pde(uint32_t pd_phys, uint32_t vaddr);

#endif
