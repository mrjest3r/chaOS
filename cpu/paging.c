#include "paging.h"
#include "isr.h"
#include "../libc/mem.h"
#include "../libc/string.h"
#include "../drivers/screen.h"
#include "../kernel/task.h"

/* Because we identity-map memory, the pointer value returned by kmalloc
 * (a virtual address) is also its physical address, which is exactly what
 * the CPU wants in CR3 and in the directory entries. */
static uint32_t *page_directory = 0;

/* Fires on interrupt 14. The faulting linear address lives in CR2 and the
 * error code (already in the registers struct) describes the fault. */
static void page_fault_handler(registers_t *r) {
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int present   = !(r->err_code & 0x1); /* page not present */
    int rw        = r->err_code & 0x2;    /* write operation? */
    int us        = r->err_code & 0x4;    /* processor was in user mode? */
    int reserved  = r->err_code & 0x8;    /* overwritten CPU-reserved bits? */

    kprint("Page fault at 0x");
    char addr_str[16] = "";
    hex_to_ascii(faulting_address, addr_str);
    kprint(addr_str);
    kprint(" (");
    if (present)  kprint("not-present ");
    if (rw)       kprint("write ");
    else          kprint("read ");
    if (us)       kprint("user-mode ");
    if (reserved) kprint("reserved-bit ");
    kprint(")\n");

    /* A fault caused by a user-mode task is contained: kill just that task and
     * keep the kernel running. A kernel-mode fault is a bug, so we stop. */
    if (us) {
        kprint("[kernel] page fault in user mode - terminating task\n");
        task_exit(); /* never returns */
    }

    kprint("Halting.\n");
    for (;;) asm volatile("cli; hlt");
}

void init_paging() {
    uint32_t phys;

    page_directory = (uint32_t *) kmalloc(PAGE_ENTRIES * sizeof(uint32_t), 1, &phys);

    /* Mark every directory entry as not-present, supervisor, read/write. */
    for (int i = 0; i < PAGE_ENTRIES; i++) {
        page_directory[i] = PAGE_RW;
    }

    /* Identity map the first IDENTITY_MAP_MB MiB: virtual addr == physical addr.
     * One page table covers 4 MiB, so we allocate several. Everything is marked
     * user-accessible so ring-3 code, its stack (kernel heap) and programs
     * loaded above 4 MiB can run. NOTE: intentionally permissive for a learning
     * OS - it does not isolate kernel memory from user tasks (that is Phase 7). */
    int num_tables = IDENTITY_MAP_MB / 4; /* 4 MiB per page table */
    for (int t = 0; t < num_tables; t++) {
        uint32_t *pt = (uint32_t *) kmalloc(PAGE_ENTRIES * sizeof(uint32_t), 1, &phys);
        for (int i = 0; i < PAGE_ENTRIES; i++) {
            uint32_t frame = ((uint32_t) t * PAGE_ENTRIES + i) * PAGE_SIZE;
            pt[i] = frame | PAGE_PRESENT | PAGE_RW | PAGE_USER;
        }
        page_directory[t] = ((uint32_t) pt) | PAGE_PRESENT | PAGE_RW | PAGE_USER;
    }

    register_interrupt_handler(14, page_fault_handler);

    load_page_directory(page_directory);
    enable_paging();
}
