#include "vmm.h"
#include "paging.h"
#include "../libc/mem.h"

/* ---- physical frame allocator -------------------------------------------
 * A flat bitmap over the [FRAME_POOL_START, FRAME_POOL_END) region: one bit per
 * 4 KiB frame, 1 = used. The pool is identity-mapped by the kernel, so a frame's
 * physical address is also the address the kernel uses to read/write it. */

#define NUM_FRAMES ((FRAME_POOL_END - FRAME_POOL_START) / PAGE_SIZE)

static uint32_t frame_bitmap[NUM_FRAMES / 32];
static uint32_t frames_in_use = 0;

static inline void bm_set(uint32_t i)   { frame_bitmap[i >> 5] |=  (1u << (i & 31)); }
static inline void bm_clear(uint32_t i) { frame_bitmap[i >> 5] &= ~(1u << (i & 31)); }
static inline int  bm_test(uint32_t i)  { return (frame_bitmap[i >> 5] >> (i & 31)) & 1u; }

void vmm_init(void) {
    for (uint32_t i = 0; i < NUM_FRAMES / 32; i++) frame_bitmap[i] = 0;
    frames_in_use = 0;
}

uint32_t frame_alloc(void) {
    for (uint32_t i = 0; i < NUM_FRAMES; i++) {
        if (!bm_test(i)) {
            bm_set(i);
            frames_in_use++;
            return FRAME_POOL_START + i * PAGE_SIZE;
        }
    }
    return 0; /* pool exhausted */
}

void frame_free(uint32_t phys) {
    if (phys < FRAME_POOL_START || phys >= FRAME_POOL_END) return;
    uint32_t i = (phys - FRAME_POOL_START) / PAGE_SIZE;
    if (bm_test(i)) {
        bm_clear(i);
        frames_in_use--;
    }
}

uint32_t frame_used(void)  { return frames_in_use; }
uint32_t frame_total(void) { return NUM_FRAMES; }

/* ---- address spaces ------------------------------------------------------ */

/* How many page-directory entries cover the shared kernel identity map. */
#define KERNEL_PDES (IDENTITY_MAP_MB / 4)

uint32_t vmm_create_addrspace(void) {
    uint32_t pd_phys = frame_alloc();
    if (!pd_phys) return 0;

    uint32_t *pd = (uint32_t *) pd_phys;      /* identity-mapped, so phys == virt */
    memory_set((uint8_t *) pd, 0, PAGE_SIZE);

    /* Share the kernel's page tables (0..IDENTITY_MAP_MB). They are marked
     * supervisor-only, so ring-3 code cannot touch kernel memory, but ring-0
     * (syscalls, interrupts, the frame pool) works in every address space. */
    for (int i = 0; i < KERNEL_PDES; i++) pd[i] = kernel_directory[i];

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

void vmm_destroy_addrspace(uint32_t pd_phys) {
    uint32_t *pd = (uint32_t *) pd_phys;

    /* Skip the shared kernel PDEs; free only this process's private tables. */
    for (int i = KERNEL_PDES; i < PAGE_ENTRIES; i++) {
        if (!(pd[i] & PAGE_PRESENT)) continue;

        uint32_t *pt = (uint32_t *) (pd[i] & ~0xFFFu);
        for (int j = 0; j < PAGE_ENTRIES; j++) {
            if (pt[j] & PAGE_PRESENT) frame_free(pt[j] & ~0xFFFu);
        }
        frame_free(pd[i] & ~0xFFFu);
    }

    frame_free(pd_phys);
}
