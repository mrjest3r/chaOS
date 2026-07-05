#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Segment selectors (index << 3 | RPL) */
#define KERNEL_CODE_SEL 0x08
#define KERNEL_DATA_SEL 0x10
#define USER_CODE_SEL   0x1B /* 0x18 | ring 3 */
#define USER_DATA_SEL   0x23 /* 0x20 | ring 3 */
#define TSS_SEL         0x28

/* Installs a fresh GDT (kernel + user code/data) and a TSS, then loads them. */
void init_gdt();

/* Updates the ring-0 stack the CPU uses on a privilege change (ring3 -> ring0). */
void tss_set_stack(uint32_t esp0);

#endif
