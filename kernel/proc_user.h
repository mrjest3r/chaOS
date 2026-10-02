#ifndef PROC_USER_H
#define PROC_USER_H

#include <stdint.h>

/* State numbers match task_state_t in kernel/task.h.
 * A listing never includes UNUSED (0) or DEAD (4). */
#define PROC_READY    1
#define PROC_RUNNING  2
#define PROC_SLEEPING 3
#define PROC_BLOCKED  5
#define PROC_FROZEN   6

#define PROC_NAME_LEN 24

typedef struct {
    int32_t  id;
    uint32_t state;
    uint32_t user;                 /* 1 = ring 3, 0 = kernel thread          */
    char     name[PROC_NAME_LEN];
} uproc_t;

/* SYS_PSCTL (28): ebx = pid, ecx = op. User tasks only; never pid 0 or self. */
#define PSOP_SLAY       1          /* kill that process                       */
#define PSOP_OBLITERATE 2          /* kill it and its user descendants        */
#define PSOP_FREEZE     3          /* stop scheduling it                      */
#define PSOP_AWAKEN     4          /* make a frozen process runnable          */

/* SYS_POWER (29): ebx = op. Does not return on success. */
#define PWOP_REBOOT     1          /* rebirth                                 */
#define PWOP_SHUTDOWN   2          /* slumber — ACPI power off                */
#define PWOP_OFF        3          /* extinguish — ACPI power off             */

#endif
