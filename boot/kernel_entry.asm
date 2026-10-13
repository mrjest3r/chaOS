; Boot stub: the very first kernel code, entered by the boot sector at
; physical 0x10000 in 32-bit protected mode with paging OFF.
;
; The kernel proper is linked at 0xC0010000 (higher half), so before we can
; call kernel_main we must build a temporary page directory of 4 MiB pages
; that maps the first 16 MiB of physical memory twice:
;
;   virt 0x00000000.. -> phys 0x00000000..   (identity - this stub itself)
;   virt 0xC0000000.. -> phys 0x00000000..   (higher half - the kernel)
;
; init_paging() later replaces this directory with the real one built from
; 4 KiB page tables; it keeps exactly the same double mapping.
;
; The linker script (boot/kernel.ld) places this file's .text at 0x10000
; (VMA == LMA) while everything else is linked high.

[bits 32]
[extern kernel_main]
[extern __bss_start]
[extern __bss_end]
global _start

PAGE_DIR  equ 0x8000        ; free page during boot, only used until init_paging
PDE_4M    equ 0x83          ; present | rw | 4 MiB page size
HIGH_PDE  equ 768           ; 0xC0000000 >> 22
NUM_PDES  equ 4             ; 4 entries x 4 MiB = 16 MiB

_start:
    ; VirtualBox's BIOS leaves A20 off. With that gate closed, bit 20 of
    ; every physical address is forced to 0, so the heap at 0x100000 is
    ; written over the kernel and the next interrupt triple-faults.
    ; QEMU's BIOS already has A20 open; the test below returns immediately.
    call enable_a20

    ; Clear the boot page directory.
    cld
    mov edi, PAGE_DIR
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; Fill the identity and higher-half entries with 4 MiB pages.
    xor ecx, ecx
.fill:
    mov eax, ecx
    shl eax, 22             ; phys = index * 4 MiB
    or  eax, PDE_4M
    mov [PAGE_DIR + ecx*4], eax
    mov [PAGE_DIR + (HIGH_PDE*4) + ecx*4], eax
    inc ecx
    cmp ecx, NUM_PDES
    jb  .fill

    ; CR4.PSE: enable 4 MiB pages.
    mov eax, cr4
    or  eax, 0x00000010
    mov cr4, eax

    mov eax, PAGE_DIR
    mov cr3, eax

    mov eax, cr0
    or  eax, 0x80000000     ; CR0.PG
    mov cr0, eax

    ; Paging is on. This stub keeps executing through the identity mapping.
    ; The loader only reads the file image (.text/.rodata/.data); .bss must be
    ; cleared here rather than trusting the BIOS/emulator to hand us zero RAM.
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    ; Switch the stack to its higher-half alias and enter the high kernel.
    mov esp, 0xC0090000
    mov eax, kernel_main
    call eax

    jmp $

; Returns with ZF clear when [0x500] and [0x100500] are different bytes.
enable_a20:
    call a20_is_open
    jnz .open
    in al, 0x92                 ; fast A20 gate (VirtualBox implements this)
    or al, 00000010b
    and al, 11111110b           ; bit 0 is reset; leave it clear
    out 0x92, al
    call a20_is_open
    jnz .open
    call a20_via_kbc
    call a20_is_open
    jnz .open
    mov dword [0xB8000], 0x4F414F41
    mov dword [0xB8004], 0x4F324F32
    mov dword [0xB8008], 0x4F304F30
    cli
    hlt
.open:
    ret

a20_is_open:
    push eax
    mov byte [0x500], 0
    mov byte [0x100500], 1
    cmp byte [0x500], 1         ; equal means the write wrapped, so A20 is shut
    pop eax
    ret

a20_via_kbc:
    call .wait_in
    mov al, 0xAD                ; disable the keyboard
    out 0x64, al
    call .wait_in
    mov al, 0xD0                ; read controller output port
    out 0x64, al
    call .wait_out
    in al, 0x60
    or al, 00000010b
    push eax
    call .wait_in
    mov al, 0xD1                ; write controller output port
    out 0x64, al
    call .wait_in
    pop eax
    out 0x60, al
    call .wait_in
    mov al, 0xAE                ; enable the keyboard
    out 0x64, al
    call .wait_in
    ret

.wait_in:
    mov ecx, 100000
.spin_in:
    in al, 0x64
    test al, 2
    jz .ready_in
    loop .spin_in
.ready_in:
    ret

.wait_out:
    mov ecx, 100000
.spin_out:
    in al, 0x64
    test al, 1
    jnz .ready_out
    loop .spin_out
.ready_out:
    ret
