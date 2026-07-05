#include "ulib.h"
#include "../kernel/syscall.h" /* SYS_* numbers, shared with the kernel */

void uprint(const char *s) {
    asm volatile("int $0x80" : : "a" (SYS_PRINT), "b" (s));
}

int ugetpid(void) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_GETPID));
    return r;
}

void usleep(int ticks) {
    asm volatile("int $0x80" : : "a" (SYS_SLEEP), "b" (ticks));
}

int uwritefile(const char *name, const void *buf, int len) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_WRITEFILE), "b" (name), "c" (buf), "d" (len));
    return r;
}

int ureadfile(const char *name, void *buf, int max) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_READFILE), "b" (name), "c" (buf), "d" (max));
    return r;
}

uint32_t uuptime(void) {
    uint32_t r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_UPTIME));
    return r;
}

int ustrlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void uprint_dec(uint32_t v) {
    char tmp[11];
    char out[12];
    int i = 0, j = 0;
    if (v == 0) { uprint("0"); return; }
    while (v > 0) { tmp[i++] = (char) ('0' + (v % 10)); v /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
    uprint(out);
}

void uexit(void) {
    asm volatile("int $0x80" : : "a" (SYS_EXIT));
    for (;;) { }
}
