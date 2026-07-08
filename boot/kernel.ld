/* chaOS kernel linker script: a tiny boot stub at the physical load address,
 * then the main kernel linked in the higher half at 0xC0010000. The boot
 * sector loads the flat binary contiguously starting at 0x10000. */
OUTPUT_FORMAT("binary")
ENTRY(boot_entry)

KERNEL_PHYS = 0x10000;
KERNEL_VIRT = 0xC0010000;

SECTIONS
{
    /* Runs at physical 0x10000 before paging is enabled. */
    .boot KERNEL_PHYS : {
        *(.boot)
    }

    /* Main kernel: virtually in the higher half, physically packed after .boot. */
    .text KERNEL_VIRT : AT(KERNEL_PHYS + SIZEOF(.boot)) {
        *(.text .text.*)
    }

    .rodata : AT(LOADADDR(.text) + SIZEOF(.text)) {
        *(.rodata .rodata.*)
    }

    .data : AT(LOADADDR(.rodata) + SIZEOF(.rodata)) {
        *(.data .data.*)
    }

    .bss : AT(LOADADDR(.data) + SIZEOF(.data)) {
        *(.bss .bss.* COMMON)
    }

    __kernel_phys_start = LOADADDR(.text);
    __kernel_virt_start = ADDR(.text);
    __kernel_end = ADDR(.bss) + SIZEOF(.bss);
}
