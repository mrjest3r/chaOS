[bits 32]
global context_switch

; void context_switch(uint32_t *old_esp, uint32_t new_esp)
;   Saves the callee-saved registers of the current task, stores the current
;   stack pointer into *old_esp, loads new_esp, and restores the next task's
;   registers. Returning then resumes wherever the next task last switched out.
context_switch:
    push ebp
    push ebx
    push esi
    push edi

    mov eax, [esp + 20]   ; eax = old_esp (pointer)
    mov [eax], esp        ; *old_esp = current esp
    mov eax, [esp + 24]   ; eax = new_esp (value)
    mov esp, eax          ; switch stacks

    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
