#include "idt.h"
#include "type.h"

/* Definitions for the globals declared 'extern' in idt.h */
idt_gate_t idt[IDT_ENTRIES];
idt_register_t idt_reg;

void set_idt_gate_flags(int n, uint32_t handler, uint8_t flags) {
    idt[n].low_offset = low_16(handler);
    idt[n].sel = KERNEL_CS;
    idt[n].always0 = 0;
    idt[n].flags = flags;
    idt[n].high_offset = high_16(handler);
}

void set_idt_gate(int n, uint32_t handler) {
    /* 0x8E = present, ring 0, 32-bit interrupt gate */
    set_idt_gate_flags(n, handler, 0x8E);
}

void set_idt() {
    idt_reg.base = (uint32_t) &idt;
    idt_reg.limit = IDT_ENTRIES * sizeof(idt_gate_t) - 1;
    /* Don't make the mistake of loading &idt -- always load &idt_reg */
    asm volatile("lidtl (%0)" : : "r" (&idt_reg));
}
