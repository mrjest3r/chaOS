/* Higher-half kernel layout.
 *
 * The boot sector loads the flat kernel binary at physical 0x10000 and jumps
 * to its first byte. That first byte is the boot stub (boot/kernel_entry.o),
 * which must run at its load address, so it is linked with VMA == LMA at
 * 0x10000. Everything else is linked at 0xC0000000 + its physical address;
 * the AT() clauses keep the file (LMA) layout contiguous so --oformat binary
 * produces the same flat image the boot sector expects.
 */
ENTRY(_start)

VIRT_BASE = 0xC0000000;

SECTIONS
{
    . = 0x00010000;

    /* Boot stub: runs with paging off, identity addresses. */
    .boot : {
        boot/kernel_entry.o(.text*)
    }

    /* The rest of the kernel lives in the higher half. */
    . = ALIGN(16);
    . += VIRT_BASE;

    .text : AT(ADDR(.text) - VIRT_BASE) {
        *(.text*)
    }
    . = ALIGN(16);
    .rodata : AT(ADDR(.rodata) - VIRT_BASE) {
        *(.rodata*)
    }
    . = ALIGN(16);
    .data : AT(ADDR(.data) - VIRT_BASE) {
        *(.data*)
    }
    . = ALIGN(16);
    /* .bss occupies no file space. boot/kernel_entry.asm zeroes
     * [__bss_start, __bss_end) itself right after enabling paging, so the
     * kernel does not depend on the loader or on RAM being clean. Keep last:
     * __bss_end is also the end of the kernel's low-memory footprint, which
     * must stay below INITRD_PHYS (checked by the Makefile). */
    .bss : AT(ADDR(.bss) - VIRT_BASE) {
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    }

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
