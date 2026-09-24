#ifndef ARCH_H
#define ARCH_H

/* chaOS arch API (x86 today).
 *
 * Kernel and drivers should prefer this header (or the specific public headers
 * it includes) over cpu/ internals. A future port keeps these *names* and
 * contracts; only the .c/.asm under cpu/ change. ARM is not scheduled.
 *
 * Public (stable contracts — see docs/agent/MAP.md "Arch API"):
 *   memlayout.h  — KERNEL_VIRT_BASE, PHYS_TO_VIRT / VIRT_TO_PHYS
 *   paging.h     — init_paging, PAGE_*, load_page_directory
 *   vmm.h        — frames, addrspaces, USER_BASE layout
 *   isr.h        — isr/irq install, register_interrupt_handler, registers_t
 *   gdt.h        — init_gdt, tss_set_stack, segment selectors
 *   timer.h      — init_timer, timer_ticks
 *   power.h      — power_off, qemu_debug_exit
 *   ports.h      — PIO (x86 I/O space; drivers may include directly)
 *   context.h    — context_switch, enter_user_mode
 *
 * Private to cpu/ (do not include from kernel/drivers):
 *   idt.h, type.h, interrupt.asm gate stubs
 */

#include "memlayout.h"
#include "paging.h"
#include "vmm.h"
#include "isr.h"
#include "gdt.h"
#include "timer.h"
#include "power.h"
#include "ports.h"
#include "context.h"

#endif
