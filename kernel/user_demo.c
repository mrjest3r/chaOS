#include "user_demo.h"
#include "task.h"
#include "syscall.h"
#include "fs.h"
#include <stdint.h>

/* ---- ring-3 syscall wrappers -------------------------------------------- */

static inline void sys_print(const char *s) {
    asm volatile("int $0x80" : : "a" (SYS_PRINT), "b" (s));
}

static inline int sys_getpid(void) {
    int ret;
    asm volatile("int $0x80" : "=a" (ret) : "a" (SYS_GETPID));
    return ret;
}

static inline int sys_writefile(const char *name, const void *buf, int len) {
    int ret;
    asm volatile("int $0x80"
                 : "=a" (ret)
                 : "a" (SYS_WRITEFILE), "b" (name), "c" (buf), "d" (len));
    return ret;
}

static inline int sys_readfile(const char *name, void *buf, int max) {
    int ret;
    asm volatile("int $0x80"
                 : "=a" (ret)
                 : "a" (SYS_READFILE), "b" (name), "c" (buf), "d" (max));
    return ret;
}

static inline void sys_sleep(int ticks) {
    asm volatile("int $0x80" : : "a" (SYS_SLEEP), "b" (ticks));
}

static inline void sys_exit(void) {
    asm volatile("int $0x80" : : "a" (SYS_EXIT));
}

/* ---- self-contained helpers (no kernel calls) --------------------------- */

static int u_utoa(uint32_t v, char *out) {
    char tmp[11];
    int i = 0;
    if (v == 0) { out[0] = '0'; out[1] = '\0'; return 1; }
    while (v > 0) { tmp[i++] = (char) ('0' + (v % 10)); v /= 10; }
    int j = 0;
    while (i > 0) out[j++] = tmp[--i];
    out[j] = '\0';
    return j;
}

/* Builds "userN.txt" for this task's pid. */
static void make_filename(int pid, char *out) {
    char num[12];
    int nlen = u_utoa((uint32_t) pid, num);
    const char *pfx = "user";
    int k = 0;
    for (int i = 0; pfx[i]; i++) out[k++] = pfx[i];
    for (int i = 0; i < nlen; i++) out[k++] = num[i];
    const char *sfx = ".txt";
    for (int i = 0; sfx[i]; i++) out[k++] = sfx[i];
    out[k] = '\0';
}

/* ---- the ring-3 program ------------------------------------------------- */

/* Runs entirely in ring 3. It may ONLY use syscalls: any privileged instruction
 * would fault (and, thanks to fault isolation, would just kill this task). */
static void user_program() {
    int pid = sys_getpid();
    char tag[24];
    int t = 0;
    const char *p = "[user ";
    for (int i = 0; p[i]; i++) tag[t++] = p[i];
    char num[12];
    int nlen = u_utoa((uint32_t) pid, num);
    for (int i = 0; i < nlen; i++) tag[t++] = num[i];
    tag[t++] = ']'; tag[t++] = ' '; tag[t] = '\0';

    sys_print(tag); sys_print("start\n");

    /* Write a private file through the filesystem syscall. */
    char fname[24];
    make_filename(pid, fname);
    const char *payload = "ring-3 task wrote this";
    int plen = 0; while (payload[plen]) plen++;
    sys_writefile(fname, payload, plen);
    sys_print(tag); sys_print("wrote "); sys_print(fname); sys_print("\n");

    /* Block for a while: this yields the CPU to the other task via the
     * scheduler, demonstrating cooperative blocking from user space. */
    sys_print(tag); sys_print("sleeping\n");
    sys_sleep(15);
    sys_print(tag); sys_print("awake\n");

    /* Read the file back to prove persistence across the sleep/switch. */
    char rd[64];
    int n = sys_readfile(fname, rd, sizeof(rd) - 1);
    if (n >= 0) {
        rd[n] = '\0';
        sys_print(tag); sys_print("read back: \""); sys_print(rd); sys_print("\"\n");
    }

    sys_print(tag); sys_print("exit\n");
    sys_exit();
    for (;;) { } /* not reached */
}

/* A misbehaving ring-3 program: executes a privileged instruction (cli), which
 * the CPU rejects with a #GP while in user mode. The kernel's fault handler
 * should terminate this task rather than hang. */
static void faulting_program() {
    sys_print("[rogue] about to run a privileged instruction...\n");
    asm volatile("cli"); /* privileged: triggers #GP in ring 3 */
    sys_print("[rogue] this should never print\n");
    sys_exit();
    for (;;) { }
}

/* ---- kernel-side entry points ------------------------------------------- */

int usermode_spawn() {
    return task_create_user(user_program);
}

int usermode_spawn_faulting() {
    return task_create_user(faulting_program);
}

void usermode_run() {
    int id = usermode_spawn();
    if (id < 0) return;
    /* Idle (task 0) until the spawned task finishes. The timer keeps preempting
     * us so the user task actually runs. */
    while (task_alive(id)) asm volatile("hlt");
}
