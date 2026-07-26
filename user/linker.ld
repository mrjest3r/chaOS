/* Link user programs to run at USER_BASE (1 GiB). Every process gets its own
 * address space with this window mapped, so all programs can be linked to the
 * same address and still run concurrently, fully isolated. */
ENTRY(_start)

SECTIONS {
    . = 0x40000000;

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
