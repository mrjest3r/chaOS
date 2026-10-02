#include "vmm.h"
#include "paging.h"
#include "memlayout.h"
#include "../libc/mem.h"
#include "../drivers/fb.h"

/* ---- physical frame allocator -------------------------------------------
 * A flat bitmap over the [FRAME_POOL_START, FRAME_POOL_END) region: one bit per
 * 4 KiB frame, 1 = used. The pool is identity-mapped by the kernel, so a frame's
 * physical address is also the address the kernel uses to read/write it. */

static uint32_t *frame_bitmap;
static uint32_t frame_count;
static uint32_t pool_start, pool_end;
static uint32_t frames_in_use;
static uint32_t id_pdes = 4;
static uint32_t hi_pdes = 4;
static int shared_pde[4];
static int n_shared;

static int is_shared_pde(int pdi) {
    for (int i = 0; i < n_shared; i++)
        if (shared_pde[i] == pdi) return 1;
    return 0;
}

static inline void bm_set(uint32_t i)   { frame_bitmap[i >> 5] |=  (1u << (i & 31)); }
static inline void bm_clear(uint32_t i) { frame_bitmap[i >> 5] &= ~(1u << (i & 31)); }
static inline int  bm_test(uint32_t i)  { return (frame_bitmap[i >> 5] >> (i & 31)) & 1u; }

void vmm_set_kernel_pdes(uint32_t identity_pdes, uint32_t high_alias_pdes) {
    id_pdes = identity_pdes;
    hi_pdes = high_alias_pdes;
}

void vmm_share_pde(int pdi) {
    if (is_shared_pde(pdi) || n_shared >= 4) return;
    shared_pde[n_shared++] = pdi;
}

void vmm_set_pool(uint32_t start, uint32_t end, uint32_t *bitmap) {
    pool_start = start;
    pool_end = end;
    frame_count = (end - start) / PAGE_SIZE;
    frame_bitmap = bitmap;
    frames_in_use = 0;
    for (uint32_t i = 0; i < (frame_count + 31) / 32; i++) frame_bitmap[i] = 0;
}

uint32_t frame_alloc(void) {
    for (uint32_t i = 0; i < frame_count; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            frames_in_use++;
            return pool_start + i * PAGE_SIZE;
        }
    }
    return 0; /* pool exhausted */
}

void frame_free(uint32_t phys) {
    if (phys < pool_start || phys >= pool_end) return;
    uint32_t i = (phys - pool_start) / PAGE_SIZE;
    if (bm_test(i)) {
        bm_clear(i);
        frames_in_use--;
    }
}

uint32_t frame_used(void)  { return frames_in_use; }
uint32_t frame_total(void) { return frame_count; }

/* ---- address spaces ------------------------------------------------------ */

uint32_t vmm_create_addrspace(void) {
    uint32_t pd_phys = frame_alloc();
    if (!pd_phys) return 0;

    uint32_t *pd = (uint32_t *) pd_phys;      /* identity-mapped, so phys == virt */
    memory_set((uint8_t *) pd, 0, PAGE_SIZE);

    /* Share the kernel's page tables (0..IDENTITY_MAP_MB), both the identity
     * mapping and its higher-half alias at 0xC0000000. They are marked
     * supervisor-only, so ring-3 code cannot touch kernel memory, but ring-0
     * (syscalls, interrupts, the frame pool) works in every address space. */
    for (uint32_t i = 0; i < id_pdes; i++)
        pd[i] = kernel_directory[i];
    for (uint32_t i = 0; i < hi_pdes; i++)
        pd[HIGH_PDE_INDEX + i] = kernel_directory[HIGH_PDE_INDEX + i];

    /* Share the linear framebuffer mapping so kprint still works while this
     * address space is loaded in CR3 (syscalls run on the user directory). */
    fb_share_into(pd);
    for (int i = 0; i < n_shared; i++)
        pd[shared_pde[i]] = kernel_directory[shared_pde[i]];

    return pd_phys;
}

int vmm_map(uint32_t pd_phys, uint32_t vaddr, uint32_t paddr, uint32_t flags) {
    uint32_t *pd = (uint32_t *) pd_phys;
    uint32_t pdi = vaddr >> 22;
    uint32_t pti = (vaddr >> 12) & 0x3FF;

    if (!(pd[pdi] & PAGE_PRESENT)) {
        uint32_t pt_phys = frame_alloc();
        if (!pt_phys) return -1;
        memory_set((uint8_t *) pt_phys, 0, PAGE_SIZE);
        /* The directory entry must allow user access; the page-level U/S bit
         * (in 'flags') decides per-page whether ring 3 may reach it. */
        pd[pdi] = pt_phys | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    uint32_t *pt = (uint32_t *) (pd[pdi] & ~0xFFFu);
    pt[pti] = (paddr & ~0xFFFu) | (flags & 0xFFFu);
    return 0;
}

void vmm_unmap_pde(uint32_t pd_phys, uint32_t vaddr) {
    uint32_t *pd = (uint32_t *) pd_phys;
    int i = (int) (vaddr >> 22);
    if (i < (int) id_pdes) return;
    if (i >= (int) HIGH_PDE_INDEX && i < (int) (HIGH_PDE_INDEX + hi_pdes)) return;
    if (fb_is_shared_pde(i) || is_shared_pde(i)) return;
    if (!(pd[i] & PAGE_PRESENT)) return;

    uint32_t *pt = (uint32_t *) (pd[i] & ~0xFFFu);
    for (int j = 0; j < PAGE_ENTRIES; j++) {
        if (pt[j] & PAGE_PRESENT) frame_free(pt[j] & ~0xFFFu);
    }
    frame_free(pd[i] & ~0xFFFu);
    pd[i] = 0;
}

void vmm_destroy_addrspace(uint32_t pd_phys) {
    uint32_t *pd = (uint32_t *) pd_phys;

    /* Skip the shared kernel PDEs (identity map AND its higher-half alias);
     * free only this process's private tables. Freeing the aliased kernel
     * tables here would pull the kernel out from under every address space. */
    for (int i = (int) id_pdes; i < PAGE_ENTRIES; i++) {
        if (i >= (int) HIGH_PDE_INDEX && i < (int) (HIGH_PDE_INDEX + hi_pdes))
            continue;
        if (fb_is_shared_pde(i) || is_shared_pde(i))
            continue;
        if (!(pd[i] & PAGE_PRESENT)) continue;

        uint32_t *pt = (uint32_t *) (pd[i] & ~0xFFFu);
        for (int j = 0; j < PAGE_ENTRIES; j++) {
            if (pt[j] & PAGE_PRESENT) frame_free(pt[j] & ~0xFFFu);
        }
        frame_free(pd[i] & ~0xFFFu);
    }

    frame_free(pd_phys);
}
