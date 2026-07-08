/* Link user programs to run inside the identity-mapped user region
 * (USER_LOAD_BASE = 4 MiB). The kernel's ELF loader copies the PT_LOAD
 * segments to these addresses and jumps to _start in ring 3. */
ENTRY(_start)

SECTIONS {
    . = 0x400000;

    .text : {
        *(.text._start)   /* keep crt0 entry at the very front */
        *(.text*)
    }
    .rodata : { *(.rodata*) }
    .data   : { *(.data*) }
    .bss    : { *(.bss*) *(COMMON) }

    /* Drop metadata we do not load. */
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
