; Load 'dh' sectors starting at LBA 1 (the sector after the boot sector) into
; memory beginning at ES:0000. We read ONE sector per BIOS call, converting the
; linear sector number to CHS each time - a single multi-sector int 13h read is
; not reliable once it must cross a cylinder boundary. After each sector ES is
; advanced by 0x20 paragraphs (512 bytes) so BX can stay 0 and a large kernel
; can be loaded without BX ever overflowing.
;
; Floppy geometry (1.44 MiB): 18 sectors/track, 2 heads.

SECTORS_PER_TRACK equ 18
NUM_HEADS         equ 2

disk_load:
    pusha
    mov [dl_drive], dl
    mov [total_sectors], dh
    mov byte [done_cnt], 0
    mov word [cur_lba], 1
    xor bx, bx                 ; every sector is read to ES:0000

.next_sector:
    mov al, [done_cnt]
    cmp al, [total_sectors]
    jae .finish

    ; --- Convert cur_lba -> CHS ---
    mov ax, [cur_lba]
    xor dx, dx
    div word [spt]             ; ax = lba / SPT, dx = lba % SPT
    mov cl, dl
    inc cl                     ; cl = sector = (lba % SPT) + 1
    xor dx, dx
    div word [num_heads]       ; ax = cylinder, dx = head
    mov ch, al                 ; ch = cylinder low 8 bits
    mov dh, dl                 ; dh = head

    ; --- Read one sector into ES:0000 ---
    mov ah, 0x02
    mov al, 1
    mov dl, [dl_drive]
    int 0x13
    jc disk_error

    ; advance the load segment by 512 bytes (0x20 paragraphs)
    mov ax, es
    add ax, 0x20
    mov es, ax

    inc word [cur_lba]
    inc byte [done_cnt]
    jmp .next_sector

.finish:
    popa
    ret

disk_error:
    mov bx, DISK_ERROR
    call print
    call print_nl
    jmp $

DISK_ERROR    db "Disk read error", 0

; disk_load scratch variables
dl_drive      db 0
total_sectors db 0
done_cnt      db 0
cur_lba       dw 0
spt           dw SECTORS_PER_TRACK
num_heads     dw NUM_HEADS
