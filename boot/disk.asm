; Load CX sectors from LBA AX into ES:0000, BIOS drive DL.
; HDD (DL >= 0x80): int 13h AH=42, then CHS 63/16. Floppy: CHS 18/2.
; One sector per call so a cylinder crossing never fails the BIOS.

disk_load:
    pusha
    mov [drv], dl
    mov di, cx
    mov [lba], ax

.next:
    test di, di
    jz .ok

    cmp byte [drv], 0x80
    jb .flop

    mov ax, es
    mov [dap_seg], ax
    mov ax, [lba]
    mov [dap_lba], ax
    mov si, dap
    mov dl, [drv]
    mov ah, 0x42
    int 0x13
    jnc .adv
    ; LBA failed — HDD CHS 63/16
    mov ax, [lba]
    xor dx, dx
    mov bx, 63
    div bx
    inc dx
    mov cl, dl
    xor dx, dx
    mov bx, 16
    div bx
    jmp .readchs

.flop:
    mov ax, [lba]
    xor dx, dx
    mov bx, 18
    div bx
    inc dx
    mov cl, dl
    xor dx, dx
    shr ax, 1
    mov ch, al
    adc dh, 0
    jmp .int13

.readchs:
    mov ch, al
    mov dh, dl
.int13:
    mov ah, 0x02
    mov al, 1
    mov dl, [drv]
    xor bx, bx
    int 0x13
    jc disk_error

.adv:
    mov ax, es
    add ax, 0x20
    mov es, ax
    inc word [lba]
    dec di
    jmp .next

.ok:
    popa
    ret

disk_error:
    jmp $

drv db 0
lba dw 0
dap:
    db 16, 0
    dw 1
    dw 0
dap_seg: dw 0
dap_lba: dd 0, 0
