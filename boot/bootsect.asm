; Identical to lesson 13's boot sector, but the %included files have new paths
[org 0x7c00]
; The kernel is loaded and linked at 0x10000 (64 KiB), safely above the
; real-mode boot stack at 0x9000. This removes the old ~32 KiB load ceiling and
; lets the kernel grow up toward the protected-mode stack at 0x90000.
KERNEL_OFFSET equ 0x10000

    mov [BOOT_DRIVE], dl ; Remember that the BIOS sets us the boot drive in 'dl' on boot
    mov bp, 0x9000
    mov sp, bp

    mov bx, MSG_REAL_MODE 
    call print
    call print_nl

    call load_kernel ; read the kernel from disk
    call switch_to_pm ; disable interrupts, load GDT,  etc. Finally jumps to 'BEGIN_PM'
    jmp $ ; Never executed

%include "boot/print.asm"
%include "boot/print_hex.asm"
%include "boot/disk.asm"
%include "boot/gdt.asm"
%include "boot/32bit_print.asm"
%include "boot/switch_pm.asm"

[bits 16]
load_kernel:
    mov bx, MSG_LOAD_KERNEL
    call print
    call print_nl

    ; Load the kernel to physical 0x10000 (segment 0x1000, offset 0). disk_load
    ; advances ES per sector, so this scales well past the old 32 KiB limit.
    ; 120 sectors (60 KiB) comfortably covers the current kernel with room grow.
    mov ax, 0x1000
    mov es, ax
    xor bx, bx
    mov dh, 120
    mov dl, [BOOT_DRIVE]
    call disk_load
    ret

[bits 32]
BEGIN_PM:
    mov ebx, MSG_PROT_MODE
    call print_string_pm
    call KERNEL_OFFSET ; Give control to the kernel
    jmp $ ; Stay here when the kernel returns control to us (if ever)


BOOT_DRIVE db 0 ; It is a good idea to store it in memory because 'dl' may get overwritten
MSG_REAL_MODE db "Started in 16-bit Real Mode", 0
MSG_PROT_MODE db "Landed in 32-bit Protected Mode", 0
MSG_LOAD_KERNEL db "Loading kernel into memory", 0
MSG_RETURNED_KERNEL db "Returned from kernel. Error?", 0

; padding
times 510 - ($-$$) db 0
dw 0xaa55
