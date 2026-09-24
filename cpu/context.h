#ifndef CONTEXT_H
#define CONTEXT_H

#include <stdint.h>

/* Arch context / privilege transitions (implemented in *.asm).
 * Kernel tasking uses these; do not call from IRQ handlers. */

/* Save callee-saved regs to *old_esp, restore from new_esp, return into new task. */
void context_switch(uint32_t *old_esp, uint32_t new_esp);

/* IRET into ring 3 at entry with user stack. Does not return. */
void enter_user_mode(uint32_t entry, uint32_t user_stack);

#endif
