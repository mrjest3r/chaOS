; Physical boot stub (section .boot, linked at 0x10000). Runs before paging is
; enabled, builds page tables, turns paging on, then jumps into the higher-half
; kernel at 0xC0010000.
[bits 32]

section .boot
global boot_entry

extern __kernel_virt_start

%define BOOT_PD_PHYS   0x00020000
%define BOOT_PT0_PHYS  0x00021000
%define IDENTITY_MB    16
%define PDE_MIRROR     768          ; 0xC0000000 >> 22
%define PAGE_PRESENT   0x1
%define PAGE_RW        0x2
%define PAGE_SIZE      0x1000

boot_entry:
    ; Build identity map of the first IDENTITY_MB MiB and mirror the same page
    ; tables at 0xC0000000 so physical address P is also at virtual 0xC0000000+P.
    mov edi, BOOT_PD_PHYS
    xor eax, eax
    mov ecx, 1024
    rep stosd

    mov ebx, BOOT_PT0_PHYS
    mov ecx, IDENTITY_MB / 4        ; number of 4 MiB page tables
    xor edx, edx                    ; PDE index

.build_pt:
    push ecx
    push edx

    ; Zero the page table.
    mov edi, ebx
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; Fill with identity mappings (frame = virtual index * 4K).
    mov edi, ebx
    mov ecx, 1024
    xor eax, eax
.fill_pt:
    mov [edi], eax
    or dword [edi], PAGE_PRESENT | PAGE_RW
    add eax, PAGE_SIZE
    add edi, 4
    loop .fill_pt

    ; Install in low PDE and mirrored high PDE.
    mov eax, ebx
    or eax, PAGE_PRESENT | PAGE_RW
    mov edi, BOOT_PD_PHYS
    mov ecx, edx
    shl ecx, 2
    add edi, ecx
    mov [edi], eax

    mov edi, BOOT_PD_PHYS
    mov ecx, edx
    add ecx, PDE_MIRROR
    shl ecx, 2
    add edi, ecx
    mov [edi], eax

    pop edx
    pop ecx
    add ebx, PAGE_SIZE
    inc edx
    loop .build_pt

    ; Load page directory and enable paging.
    mov eax, BOOT_PD_PHYS
    mov cr3, eax
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    ; Jump into the higher-half kernel (linked at 0xC0010000).
    jmp 0x08:__kernel_virt_start
