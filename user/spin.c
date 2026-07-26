#include "ulib.h"

/* A tiny ring-3 program used to show off concurrent, isolated processes: the
 * kernel's self-test runs two copies of it at the same time. Both are linked at
 * the same virtual address (USER_BASE) yet live in separate address spaces, so
 * they cannot see each other's memory. Each prints its pid a few times, sleeping
 * in between so the two visibly interleave in the output. */
int main(void) {
    int pid = ugetpid();

    for (int i = 0; i < 3; i++) {
        uprint("[spin] pid ");
        uprint_dec((uint32_t) pid);
        uprint(" tick ");
        uprint_dec((uint32_t) i);
        uprint("\n");
        usleep(8);
    }

    uprint("[spin] pid ");
    uprint_dec((uint32_t) pid);
    uprint(" done\n");
    return 0;
}
