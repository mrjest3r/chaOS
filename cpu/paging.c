#include "paging.h"
#include "vmm.h"
#include "memlayout.h"
#include "memsize.h"
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
static uint32_t mapped_bytes;

uint32_t mem_mapped_bytes(void) { return mapped_bytes; }

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

    /* Identity-map installed RAM, capped at 1 GiB, in 4 MiB chunks. Tables
     * live at 4 MiB, inside the 16 MiB the boot stub already maps. The
     * higher-half alias stays 16 MiB so it does not cover the framebuffer. */
    uint32_t bytes = mem_installed_bytes();
    bytes &= ~0x3FFFFFu;
    if (bytes < (uint32_t) HIGH_ALIAS_MB * 0x100000u)
        bytes = (uint32_t) HIGH_ALIAS_MB * 0x100000u;
    if (bytes > (uint32_t) MEM_CAP_MB * 0x100000u)
        bytes = (uint32_t) MEM_CAP_MB * 0x100000u;
    mapped_bytes = bytes;

    int num_tables = (int) (bytes / (4u * 1024u * 1024u));
    int alias_tables = HIGH_ALIAS_MB / 4;
    uint32_t pt_at = 0x00400000u;
    for (int t = 0; t < num_tables; t++) {
        uint32_t *pt = (uint32_t *) pt_at;
        pt_at += PAGE_SIZE;
        for (int i = 0; i < PAGE_ENTRIES; i++) {
            uint32_t frame = ((uint32_t) t * PAGE_ENTRIES + (uint32_t) i) * PAGE_SIZE;
            pt[i] = frame | PAGE_PRESENT | PAGE_RW;
        }
        kernel_directory[t] = ((uint32_t) pt) | PAGE_PRESENT | PAGE_RW;
        if (t < alias_tables)
            kernel_directory[HIGH_PDE_INDEX + t] = kernel_directory[t];
    }

    uint32_t pool = pt_at;
    uint32_t bitmap_bytes = ((bytes - pool) / PAGE_SIZE + 31) / 32 * 4;
    uint32_t *bitmap = (uint32_t *) pool;
    pool += (bitmap_bytes + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    vmm_set_kernel_pdes((uint32_t) num_tables, (uint32_t) alias_tables);
    vmm_set_pool(pool, bytes, bitmap);

    register_interrupt_handler(14, page_fault_handler);

    load_page_directory(kernel_directory);
    enable_paging();
}
