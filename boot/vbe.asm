; Real-mode VBE: set a 32 bpp LFB mode only (24 bpp is rejected — VirtualBox
; mis-reports pitch/channels and produces magenta margins + smash).
; Record LinBytesPerScanLine when present. Do NOT rely on Bochs DISPI here;
; the kernel may optionally use DISPI on non-VBox hosts.
[bits 16]

VBE_INFO_ADDR  equ 0x5000
MODE_INFO_ADDR equ 0x5200

vbe_init:
    pusha
    push es
    xor ax, ax
    mov es, ax

    mov di, VBE_INFO_ADDR
    xor ax, ax
    mov cx, 8
    rep stosw

    ; Prefer 800x600x32, then 1024x768x32, then 640x480x32.
    mov cx, 0x0115
    call .try
    jc .out
    mov cx, 0x0118
    call .try
    jc .out
    mov cx, 0x0112
    call .try
.out:
    pop es
    popa
    ret

.try:
    pusha
    clc
    mov ax, 0x4F01
    mov di, MODE_INFO_ADDR
    int 0x10
    cmp ax, 0x004F
    jne .fail
    test word [es:MODE_INFO_ADDR], 0x0080   ; LFB available
    jz .fail
    mov al, [es:MODE_INFO_ADDR + 25]        ; BitsPerPixel
    cmp al, 32
    jne .fail                               ; 32 bpp only

    mov ax, 0x4F02
    mov bx, cx
    or  bx, 0x4000                          ; LFB
    int 0x10
    cmp ax, 0x004F
    jne .fail

    mov ax, 0x4F01
    mov di, MODE_INFO_ADDR
    int 0x10

    mov dword [es:VBE_INFO_ADDR], 0x42464843
    mov ax, [es:MODE_INFO_ADDR + 18]        ; XResolution
    mov [es:VBE_INFO_ADDR + 4], ax
    mov ax, [es:MODE_INFO_ADDR + 20]        ; YResolution
    mov [es:VBE_INFO_ADDR + 6], ax
    mov al, [es:MODE_INFO_ADDR + 25]
    mov [es:VBE_INFO_ADDR + 8], al
    mov byte [es:VBE_INFO_ADDR + 9], 1

    mov ax, [es:MODE_INFO_ADDR + 50]        ; LinBytesPerScanLine (VBE 3)
    test ax, ax
    jnz .pitch_ok
    mov ax, [es:MODE_INFO_ADDR + 16]        ; BytesPerScanLine
.pitch_ok:
    mov [es:VBE_INFO_ADDR + 10], ax

    mov eax, [es:MODE_INFO_ADDR + 40]       ; PhysBasePtr
    mov [es:VBE_INFO_ADDR + 12], eax
    popa
    stc
    ret
.fail:
    popa
    clc
    ret
