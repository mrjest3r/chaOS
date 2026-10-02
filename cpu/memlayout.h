#ifndef MEMLAYOUT_H
#define MEMLAYOUT_H

/* ---- higher-half kernel layout --------------------------------------------
 * The kernel is loaded at physical 0x10000 but linked at 0xC0010000. The boot
 * stub (boot/kernel_entry.asm) and init_paging() map the first IDENTITY_MAP_MB
 * MiB of physical memory twice:
 *
 *   virt 0x00000000..  -> phys 0x00000000..   (identity; kept permanently so
 *                                              phys == virt tricks still work)
 *   virt 0xC0000000..  -> phys 0x00000000..   (higher half; kernel code/data)
 */
#define KERNEL_PHYS_BASE 0x00010000u
#define KERNEL_VIRT_BASE 0xC0000000u

#define PHYS_TO_VIRT(p) ((uint32_t) (p) + KERNEL_VIRT_BASE)
#define VIRT_TO_PHYS(v) ((uint32_t) (v) - KERNEL_VIRT_BASE)

/* First page-directory index of the higher-half mapping (0xC0000000 >> 22). */
#define HIGH_PDE_INDEX (KERNEL_VIRT_BASE >> 22)

/* ---- boot image layout (low memory, filled by boot/bootsect.asm) -----------
 *
 *   0x10000 .. < INITRD_PHYS   kernel.bin + .bss   (KERNEL_LOAD_SECTORS budget;
 *                              the Makefile checks __bss_end fits as well)
 *   INITRD_PHYS 0x34000 .. +INITRD_MAX initrd.bin: every user program, packed by
 *                              tools/mkinitrd.c; kernel/initrd.c seeds the
 *                              filesystem from it (makes the floppy/ISO
 *                              self-contained, no disk.img needed)
 *   0x90000                    boot stack top (grows down)
 *
 * Both regions are inside the identity map, so the kernel reads them directly.
 * Keep the same numbers in boot/bootsect.asm (0x1000 / 0x3000 segments) and
 * the Makefile (KERNEL_SECTORS / INITRD_MAX). */
#define KERNEL_LOAD_SECTORS 256u                 /* 128 KiB file budget      */
#define INITRD_PHYS         0x00034000u /* kernel .bss may extend past 0x30000 */
#define INITRD_MAX          0x00058000u          /* 352 KiB -> ends 0x88000  */

#endif
