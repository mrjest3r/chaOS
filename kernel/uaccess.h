#ifndef UACCESS_H
#define UACCESS_H

#include <stdint.h>
#include "../cpu/arch.h"
#include "task.h"

/* User-pointer validation for syscalls.
 *
 * Ring-3 tasks live in one window [USER_BASE, USER_STACK_TOP) plus any
 * SYS_SURFACE buffers the task currently owns (tracked per task). Everything
 * else that is mapped (identity alias, higher-half kernel, LFB) is reachable
 * from ring 0, so a syscall that dereferences a caller-supplied pointer must
 * refuse anything outside those regions or it becomes a kernel read/write
 * primitive. Every pointer argument goes through these before use. */

static inline int user_range_ok(const void *p, uint32_t len) {
    uint32_t a = (uint32_t) p;
    if (a >= USER_BASE && a < USER_STACK_TOP)
        return len <= USER_STACK_TOP - a; /* also catches a + len overflow */
    if (a >= SURF_BASE && a < SURF_END)
        return task_surface_range_ok(a, len);
    return 0;
}

/* NUL-terminated string fully inside the window, at most 'max' bytes long
 * (including the terminator). Returns the length without NUL, or -1. */
static inline int user_str_ok(const char *s, uint32_t max) {
    uint32_t a = (uint32_t) s;
    if (a < USER_BASE || a >= USER_STACK_TOP) return -1;
    uint32_t room = USER_STACK_TOP - a;
    if (max < room) room = max;
    for (uint32_t i = 0; i < room; i++) {
        if (s[i] == '\0') return (int) i;
    }
    return -1;
}

/* Upper bound on strings the kernel will accept from userspace. */
#define USER_STR_MAX 4096u

#endif
