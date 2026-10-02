/* Link user programs at USER_BASE (1 GiB). Separate RX and RW loads so
 * the ELF is not one RWX segment (i386-elf-ld warning + writable text). */
ENTRY(_start)

PHDRS {
    text PT_LOAD FLAGS(5); /* PF_R | PF_X */
    data PT_LOAD FLAGS(6); /* PF_R | PF_W */
}

SECTIONS {
    . = 0x40000000;

    .text : {
        *(.text._start)
        *(.text*)
    } :text

    .rodata : {
        *(.rodata*)
    } :text

    . = ALIGN(4096);
    .data : {
        *(.data*)
    } :data

    .bss : {
        *(.bss*)
        *(COMMON)
    } :data

    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
