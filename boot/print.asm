print:
    pusha
.loop:
    mov al, [bx]
    test al, al
    jz .done
    mov ah, 0x0e
    int 0x10
    inc bx
    jmp .loop
.done:
    popa
    ret
