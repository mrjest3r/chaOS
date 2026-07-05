; C runtime startup for user programs. Runs first in ring 3, calls main(), then
; asks the kernel to terminate the task via the SYS_EXIT (1) system call.
[bits 32]

section .text._start
global _start
extern main

_start:
    call main
    mov eax, 1        ; SYS_EXIT
    int 0x80
.hang:
    jmp .hang         ; SYS_EXIT never returns, but just in case
