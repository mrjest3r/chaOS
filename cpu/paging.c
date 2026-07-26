#include "paging.h"
#include "vmm.h"
#include "isr.h"
#include "../libc/mem.h"
#include "../libc/string.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../kernel/task.h"

/* Because we identity-map memory, the pointer value returned by kmalloc
 * (a virtual address) is also its physical address, which is exactly what
 * the CPU wants in CR3 and in the directory entries. */
uint32_t *kernel_directory = 0;

/* Fires on interrupt 14. The faulting linear address lives in CR2 and the
 * error code (already in the registers struct) describes the fault. */
static void page_fault_handler(registers_t *r) {
    uint32_t faulting_address;
    asm volatile("mov %%cr2, %0" : "=r" (faulting_address));

    int present   = !(r->err_code & 0x1); /* page not present */
    int rw        = r->err_code & 0x2;    /* write operation? */
    int us        = r->err_code & 0x4;    /* processor was in user mode? */
    int reserved  = r->err_code & 0x8;    /* overwritten CPU-reserved bits? */

    kprint("Page fault at ");
    char addr_str[16];
    memory_set((uint8_t *) addr_str, 0, sizeof(addr_str));
    hex_to_ascii((int) faulting_address, addr_str);
    kprint(addr_str);
    kprint(" (");
    if (present)  kprint("not-present ");
    if (rw)       kprint("write ");
    else          kprint("read ");
    if (us)       kprint("user-mode ");
    if (reserved) kprint("reserved-bit ");
    kprint(")\n");

    /* User faults: try kprint (VGA). Kernel faults: use serial only so we do
     * not recurse if the fault was caused by a bad VGA address. */
    if (us) {
        kprint("[kernel] page fault in user mode - terminating task\n");
        task_exit();
    }

    serial_write("[kernel] page fault in kernel mode at ");
    serial_write(addr_str);
    serial_write(" - halting\n");
    for (;;) asm volatile("cli; hlt");
}

void init_paging() {
    uint32_t phys;

    kernel_directory = (uint32_t *) kmalloc(PAGE_ENTRIES * sizeof(uint32_t), 1, &phys);

    /* Mark every directory entry as not-present, supervisor, read/write. */
    for (int i = 0; i < PAGE_ENTRIES; i++) {
        kernel_directory[i] = PAGE_RW;
    }

    /* Identity map the first IDENTITY_MAP_MB MiB: virtual addr == physical addr.
     * One page table covers 4 MiB, so we allocate several. Every page is marked
     * supervisor-only (no PAGE_USER): ring-3 tasks run in their own address
     * spaces at USER_BASE and cannot reach kernel memory here. The kernel keeps
     * full access to this region (including the physical frame pool) in ring 0
     * from any address space, because these tables are shared into every one. */
    int num_tables = IDENTITY_MAP_MB / 4; /* 4 MiB per page table */
    for (int t = 0; t < num_tables; t++) {
        uint32_t *pt = (uint32_t *) kmalloc(PAGE_ENTRIES * sizeof(uint32_t), 1, &phys);
        for (int i = 0; i < PAGE_ENTRIES; i++) {
            uint32_t frame = ((uint32_t) t * PAGE_ENTRIES + i) * PAGE_SIZE;
            pt[i] = frame | PAGE_PRESENT | PAGE_RW;
        }
        kernel_directory[t] = ((uint32_t) pt) | PAGE_PRESENT | PAGE_RW;
    }

    register_interrupt_handler(14, page_fault_handler);

    load_page_directory(kernel_directory);
    enable_paging();

    /* The physical frame allocator manages memory above the identity-mapped
     * kernel region; it is what backs per-process address spaces. */
    vmm_init();
}
