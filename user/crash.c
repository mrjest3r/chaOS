#include "ulib.h"

/* Deliberately misbehaves to prove memory isolation. The kernel lives at low
 * physical addresses that are mapped supervisor-only in every address space, so
 * a ring-3 read of kernel memory triggers a page fault. The kernel's fault
 * handler terminates just this task and keeps running - the read never succeeds.
 */
int main(void) {
    uprint("[crash] reading kernel memory at 0x10000 from ring 3...\n");

    volatile uint32_t *kernel_mem = (volatile uint32_t *) 0x10000;
    uint32_t stolen = *kernel_mem;   /* faults here: supervisor page, user access */

    /* Never reached: the task is killed by the page-fault handler above. */
    uprint("[crash] isolation FAILED - read kernel value ");
    uprint_dec(stolen);
    uprint("\n");
    return 0;
}
