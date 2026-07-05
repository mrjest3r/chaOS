#include "gdt.h"
#include "../libc/mem.h"

struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

struct tss_entry {
    uint32_t prev_tss;
    uint32_t esp0;   /* stack pointer to load on ring0 entry */
    uint32_t ss0;    /* stack segment to load on ring0 entry */
    uint32_t esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags;
    uint32_t eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed));

#define GDT_ENTRIES 6

static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr   gp;
static struct tss_entry tss;

/* Dedicated ring-0 stack used when the CPU switches from user to kernel mode. */
static uint8_t tss_kernel_stack[8192];

static void gdt_set(int i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[i].base_low    = base & 0xFFFF;
    gdt[i].base_mid    = (base >> 16) & 0xFF;
    gdt[i].base_high   = (base >> 24) & 0xFF;
    gdt[i].limit_low   = limit & 0xFFFF;
    gdt[i].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[i].access      = access;
}

static void write_tss(int i) {
    uint32_t base  = (uint32_t) &tss;
    uint32_t limit = sizeof(tss) - 1;

    memory_set((uint8_t *) &tss, 0, sizeof(tss));
    tss.ss0  = KERNEL_DATA_SEL;
    tss.esp0 = (uint32_t) (tss_kernel_stack + sizeof(tss_kernel_stack));
    /* Segment selectors seen by the CPU while in the TSS (ring 3 with RPL 3). */
    tss.cs = USER_CODE_SEL;
    tss.ss = tss.ds = tss.es = tss.fs = tss.gs = USER_DATA_SEL;
    tss.iomap_base = sizeof(tss);

    /* 0x89 = present, ring 0, type = 32-bit TSS (available). */
    gdt_set(i, base, limit, 0x89, 0x00);
}

static void gdt_flush() {
    asm volatile(
        "lgdt (%0)\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "mov %%ax, %%ss\n"
        "ljmp $0x08, $1f\n"
        "1:\n"
        : : "r" (&gp) : "eax", "memory");
}

static void tss_flush() {
    asm volatile("ltr %%ax" : : "a" (TSS_SEL));
}

void init_gdt() {
    gdt_set(0, 0, 0, 0, 0);              /* null */
    gdt_set(1, 0, 0xFFFFF, 0x9A, 0xCF); /* kernel code */
    gdt_set(2, 0, 0xFFFFF, 0x92, 0xCF); /* kernel data */
    gdt_set(3, 0, 0xFFFFF, 0xFA, 0xCF); /* user code (ring 3) */
    gdt_set(4, 0, 0xFFFFF, 0xF2, 0xCF); /* user data (ring 3) */
    write_tss(5);

    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t) &gdt;

    gdt_flush();
    tss_flush();
}

void tss_set_stack(uint32_t esp0) {
    tss.esp0 = esp0;
}
