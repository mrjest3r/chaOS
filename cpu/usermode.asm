[bits 32]
global enter_user_mode

section .text

; void enter_user_mode(uint32_t entry, uint32_t user_stack)
;   Switches the segment registers to user data and performs an iret into ring 3
;   at 'entry' running on 'user_stack'. This does NOT save any kernel context and
;   never returns: a ring-3 task leaves only via a syscall (SYS_EXIT) or by being
;   terminated after a fault. The task's kernel stack (TSS.esp0) is what the CPU
;   uses to re-enter ring 0 on the next interrupt/syscall.
enter_user_mode:
    mov edx, [esp + 4]    ; entry point
    mov ecx, [esp + 8]    ; user stack top

    mov ax, 0x23          ; user data selector (RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push 0x23             ; SS  (user data)
    push ecx              ; ESP (user stack)
    pushfd
    pop eax
    or eax, 0x200         ; set IF so interrupts stay enabled in ring 3
    push eax              ; EFLAGS
    push 0x1b             ; CS  (user code, RPL 3)
    push edx              ; EIP (entry)
    iret
