#include "syscall.h"
#include "task.h"
#include "fs.h"
#include "../cpu/isr.h"
#include "../cpu/timer.h"
#include "../drivers/screen.h"
#include <stdint.h>

static void syscall_handler(registers_t *r) {
    /* Argument pointers (ebx/ecx) are caller addresses. They are reachable from
     * ring 0 because the low memory is identity-mapped. */
    switch (r->eax) {
        case SYS_PRINT:
            kprint((char *) r->ebx);
            r->eax = 0;
            break;

        case SYS_GETPID:
            r->eax = (uint32_t) task_current_id();
            break;

        case SYS_WRITEFILE:
            r->eax = (uint32_t) fs_write_file((char *) r->ebx,
                                              (uint8_t *) r->ecx, r->edx);
            break;

        case SYS_READFILE:
            r->eax = (uint32_t) fs_read_file((char *) r->ebx,
                                             (uint8_t *) r->ecx, r->edx);
            break;

        case SYS_LISTFILES:
            r->eax = (uint32_t) fs_list((fs_fileinfo_t *) r->ebx, (int) r->ecx);
            break;

        case SYS_UPTIME:
            r->eax = timer_ticks();
            break;

        case SYS_YIELD:
            task_yield();
            r->eax = 0;
            break;

        case SYS_SLEEP:
            task_sleep(r->ebx);
            r->eax = 0;
            break;

        case SYS_EXIT:
            task_exit(); /* terminates the task and reschedules; never returns */
            break;

        default:
            r->eax = (uint32_t) -1;
            break;
    }
}

void syscall_init() {
    register_interrupt_handler(0x80, syscall_handler);
}
