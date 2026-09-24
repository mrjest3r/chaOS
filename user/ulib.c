#include "ulib.h"
#include "../kernel/syscall.h" /* SYS_* numbers, shared with the kernel */

void uprint(const char *s) {
    asm volatile("int $0x80" : : "a" (SYS_PRINT), "b" (s));
}

char ugetchar(void) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_GETCHAR));
    return (char) r;
}

int ureadline(char *buf, int max) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_READLINE), "b" (buf), "c" (max));
    return r;
}

int uspawn(const char *path) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_SPAWN), "b" (path));
    return r;
}

int uwait(int pid) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_WAIT), "b" (pid));
    return r;
}

int urun(const char *path) {
    int pid = uspawn(path);
    if (pid < 0) return pid;
    return uwait(pid);
}

int ulistfiles(ufileinfo_t *out, int max) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_LISTFILES), "b" (out), "c" (max));
    return r;
}

int upipe(void) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_PIPE));
    return r;
}

int upiperead(int id, void *buf, int max) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_PIPEREAD), "b" (id), "c" (buf), "d" (max));
    return r;
}

int upipewrite(int id, const void *buf, int len) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_PIPEWRITE), "b" (id), "c" (buf), "d" (len));
    return r;
}

int upipeclose(int id) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_PIPECLOSE), "b" (id));
    return r;
}

int upipepoll(int id) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_PIPEPOLL), "b" (id));
    return r;
}

int uspawnio(const char *path, int stdin_pipe, int stdout_pipe) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_SPAWNIO), "b" (path), "c" (stdin_pipe), "d" (stdout_pipe));
    return r;
}

int umouse(umouse_t *out) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_MOUSEGET), "b" (out));
    return r;
}

int uevent_wait(uevent_t *ev, uint32_t timeout_ticks) {
    int r;
    asm volatile("int $0x80"
                 : "=a" (r)
                 : "a" (SYS_EVENT_WAIT), "b" (ev), "c" (timeout_ticks)
                 : "memory");
    return r;
}

char utrychar(void) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_TRYCHAR));
    return (char) r;
}

int ufb(fb_req_t *req) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_FB), "b" (req));
    return r;
}

uint32_t *usurface(int w, int h) {
    uint32_t r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_SURFACE), "b" (w), "c" (h));
    return (uint32_t *) r;
}

int usurface_free(uint32_t *s) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_SURFFREE), "b" (s));
    return r;
}

int ufbblit(int x, int y, int w, int h, const uint32_t *src, int src_pitch_px) {
    fb_req_t r;
    r.op = FBOP_BLIT;
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.src = src;
    r.src_pitch = src_pitch_px;
    return ufb(&r);
}

int uatoi(const char *s) {
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
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

int udelete(const char *name) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (SYS_DELETEFILE), "b" (name));
    return r;
}

void udoze(void) {
    asm volatile("int $0x80" : : "a" (SYS_DOZE));
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
