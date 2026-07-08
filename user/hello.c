#include "ulib.h"

/* A standalone user program, compiled and linked separately from the kernel,
 * stored on the disk and loaded at runtime by the kernel's ELF loader. It runs
 * in ring 3 and can only reach the kernel through system calls. */
int main(void) {
    uprint("Hello from hello.elf - a program loaded from disk!\n");

    uprint("[hello] my pid is ");
    uprint_dec((uint32_t) ugetpid());
    uprint("\n");

    const char *msg = "written by hello.elf through a syscall";
    uwritefile("elfout.txt", msg, ustrlen(msg));
    uprint("[hello] wrote elfout.txt\n");

    char buf[64];
    int n = ureadfile("elfout.txt", buf, (int) sizeof(buf) - 1);
    if (n >= 0) {
        buf[n] = '\0';
        uprint("[hello] read it back: \"");
        uprint(buf);
        uprint("\"\n");
    }

    uprint("[hello] uptime is ");
    uprint_dec(uuptime());
    uprint(" ticks\n");

    uprint("[hello] goodbye!\n");
    return 0;
}
