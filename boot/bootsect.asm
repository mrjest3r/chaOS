; Boot sector: load kernel + initrd, optional VBE graphics, enter protected mode.
;
; Image layout (sectors):  0 = this   1..KERNEL_SECTORS = kernel.bin (padded)
;                          then INITRD_SECTORS of initrd.bin (user programs).
; Both counts come from the Makefile (-D). Load addresses (see
; cpu/memlayout.h): kernel at 0x10000, initrd at INITRD_PHYS = 0x34000.
[org 0x7c00]
KERNEL_OFFSET equ 0x10000
%ifndef KERNEL_SECTORS
%error "assemble with -DKERNEL_SECTORS=n -DINITRD_SECTORS=n (see Makefile)"
%endif

    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov bp, 0x9000
    mov sp, bp
    mov [BOOT_DRIVE], dl

    call load_kernel
    call vbe_init
    call switch_to_pm
    jmp $

%include "boot/disk.asm"
%include "boot/gdt.asm"
%include "boot/vbe.asm"
%include "boot/switch_pm.asm"

[bits 16]
load_kernel:
    mov dl, [BOOT_DRIVE]
    mov ax, 0x1000          ; kernel -> 0x10000
    mov es, ax
    mov ax, 1
    mov cx, KERNEL_SECTORS
    call disk_load
    mov ax, 0x3400          ; initrd -> 0x34000 (INITRD_PHYS)
    mov es, ax
    mov ax, 1 + KERNEL_SECTORS
    mov cx, INITRD_SECTORS
    call disk_load
    ret

[bits 32]
BEGIN_PM:
    call KERNEL_OFFSET
    jmp $

BOOT_DRIVE db 0

times 510 - ($-$$) db 0
dw 0xaa55
