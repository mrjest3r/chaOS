#include "ulib.h"
#include "../kernel/syscall.h"

/* Hands the kernel pointers it must refuse: kernel addresses, the identity
 * alias, the LFB, and buffers that run off the end of the user window. Every
 * call must come back -1 and this task must keep running. The verdict goes
 * into badptr.txt through a *valid* pointer so the AUTOTEST can read it. */

static int raw3(int n, uint32_t a, uint32_t b, uint32_t c) {
    int r;
    asm volatile("int $0x80" : "=a" (r) : "a" (n), "b" (a), "c" (b), "d" (c));
    return r;
}

static int fails;
static char scratch[64];

static void expect_fault(const char *what, int r) {
    if (r == -1) return;
    fails++;
    uprint("[badptr] NOT rejected: ");
    uprint(what);
    uprint("\n");
}

int main(void) {
    uprint("[badptr] probing syscalls with hostile pointers...\n");

    const uint32_t kern_hi  = 0xC0010000u; /* higher-half kernel text   */
    const uint32_t kern_lo  = 0x00010000u; /* identity alias of same    */
    const uint32_t lfb      = 0xE0000000u; /* shared LFB mapping        */
    const uint32_t win_end  = 0x44000000u; /* USER_STACK_TOP (64 MiB)   */
    const uint32_t nullp    = 0;

    expect_fault("print kernel",      raw3(SYS_PRINT, kern_hi, 0, 0));
    expect_fault("print identity",    raw3(SYS_PRINT, kern_lo, 0, 0));
    expect_fault("print null",        raw3(SYS_PRINT, nullp, 0, 0));

    expect_fault("readfile -> kernel",
                 raw3(SYS_READFILE, (uint32_t) "sys/bootcount", kern_hi, 4));
    expect_fault("readfile -> lfb",
                 raw3(SYS_READFILE, (uint32_t) "sys/bootcount", lfb, 4));
    expect_fault("readfile past window",
                 raw3(SYS_READFILE, (uint32_t) "sys/bootcount", win_end - 8, 64));
    expect_fault("readfile len overflow",
                 raw3(SYS_READFILE, (uint32_t) "sys/bootcount",
                      (uint32_t) scratch, 0xFFFFFFF0u));

    expect_fault("writefile from kernel",
                 raw3(SYS_WRITEFILE, (uint32_t) "leak.txt", kern_hi, 512));
    expect_fault("writefile name in kernel",
                 raw3(SYS_WRITEFILE, kern_hi, (uint32_t) scratch, 4));

    expect_fault("readline -> kernel", raw3(SYS_READLINE, kern_hi, 16, 0));
    expect_fault("listfiles -> kernel", raw3(SYS_LISTFILES, kern_hi, 4, 0));
    expect_fault("mouseget -> kernel", raw3(SYS_MOUSEGET, kern_hi, 0, 0));
    expect_fault("fb req in kernel",   raw3(SYS_FB, kern_hi, 0, 0));
    expect_fault("fb blit src kernel",
                 ufbblit(0, 0, 4, 4, (const uint32_t *) kern_hi, 4));
    expect_fault("fb blit src lfb",
                 ufbblit(0, 0, 4, 4, (const uint32_t *) lfb, 4));
    expect_fault("fb blit src past window",
                 ufbblit(0, 0, 64, 64, (const uint32_t *) (win_end - 64), 64));
    expect_fault("delete name kernel", raw3(SYS_DELETEFILE, kern_hi, 0, 0));
    expect_fault("spawn name kernel",  raw3(SYS_SPAWN, kern_hi, 0, 0));
    expect_fault("spawnio name kernel", raw3(SYS_SPAWNIO, kern_hi, -1, -1));
    expect_fault("spawnio name lfb",    raw3(SYS_SPAWNIO, lfb, -1, -1));
    /* G3: the pointer is checked before we would become the listener, so
     * these must neither block nor steal the keyboard from the shell. */
    expect_fault("event_wait -> kernel", raw3(SYS_EVENT_WAIT, kern_hi, 0, 0));
    expect_fault("event_wait -> lfb",    raw3(SYS_EVENT_WAIT, lfb, 0, 0));
    expect_fault("event_wait past window",
                 raw3(SYS_EVENT_WAIT, win_end - 4, 0, 0));
    expect_fault("procs -> kernel", raw3(SYS_PROCS, kern_hi, 4, 0));
    expect_fault("procs -> lfb", raw3(SYS_PROCS, lfb, 1, 0));
    expect_fault("procs past window", raw3(SYS_PROCS, win_end - 8, 4, 0));
    expect_fault("clock -> kernel", raw3(SYS_CLOCK, kern_hi, 0, 0));
    expect_fault("clock -> lfb", raw3(SYS_CLOCK, lfb, 0, 0));
    expect_fault("clock past window", raw3(SYS_CLOCK, win_end - 4, 0, 0));
    expect_fault("clock null", raw3(SYS_CLOCK, nullp, 0, 0));
    {
        uclock_t clk;
        if (uclock(&clk) < 0 || clk.month < 1 || clk.month > 12 || clk.hour > 23) {
            fails++;
            uprint("[badptr] valid clock FAILED\n");
        }
    }
    {
        uint32_t brk = ubrk(0);
        uint32_t grown = ubrk(brk + 4096u);
        if (brk == (uint32_t) -1 || grown < brk + 4096u) {
            fails++;
            uprint("[badptr] brk FAILED\n");
        } else {
            *(volatile uint32_t *) brk = 0x1234u;
            if (ubrk(0x50000000u) != (uint32_t) -1) {
                fails++;
                uprint("[badptr] brk past window accepted\n");
            }
        }
    }
    {
        uproc_t one;
        if (ulistprocs(&one, 1) < 1) {
            fails++;
            uprint("[badptr] valid proclist FAILED\n");
        }
    }

    /* G2 surfaces: a live surface is a valid pointer, a freed one is not,
     * and reading off its end or from an unallocated slot must fail. */
    {
        uint32_t *s = usurface(16, 16);
        if (!s) {
            fails++;
            uprint("[badptr] usurface(16,16) returned 0\n");
        } else {
            s[0] = 0x00ABCDEFu;
            s[16 * 16 - 1] = 0x00123456u;
            if (ufbblit(0, 0, 16, 16, s, 16) != 0) {
                fails++;
                uprint("[badptr] blit from live surface rejected\n");
            }
            expect_fault("blit past surface end", ufbblit(0, 0, 16, 17, s, 16));
            expect_fault("blit from empty surface slot",
                         ufbblit(0, 0, 4, 4, s + 0x00400000u / 4, 4));
            expect_fault("readfile into surface tail overflow",
                         raw3(SYS_READFILE, (uint32_t) "sys/bootcount",
                              (uint32_t) (s + 16 * 16 - 1), 8));
            if (usurface_free(s) != 0) {
                fails++;
                uprint("[badptr] usurface_free failed\n");
            }
            expect_fault("blit from freed surface", ufbblit(0, 0, 16, 16, s, 16));
            expect_fault("double free", usurface_free(s));
        }
        expect_fault("free of non-surface", usurface_free((uint32_t *) scratch));
        if (usurface(0, 10) != 0 || usurface(2048, 2048) != 0) {
            fails++;
            uprint("[badptr] bad surface size accepted\n");
        }
        /* Slot exhaustion: 4 slots max, 5th fails, all reclaimed at exit. */
        uint32_t *many[5];
        int got = 0;
        for (int i = 0; i < 5; i++) {
            many[i] = usurface(8, 8);
            if (many[i]) got++;
        }
        if (got != 4) {
            fails++;
            uprint("[badptr] expected exactly 4 surface slots\n");
        }
        for (int i = 0; i < 2; i++)
            if (many[i]) usurface_free(many[i]);
        /* many[2], many[3] intentionally leaked -> task exit must reclaim. */
    }

    int p = upipe();
    if (p >= 0) {
        expect_fault("pipewrite from kernel", raw3(SYS_PIPEWRITE, (uint32_t) p, kern_hi, 8));
        expect_fault("piperead -> kernel",    raw3(SYS_PIPEREAD, (uint32_t) p, kern_hi, 8));
        upipeclose(p);
    }

    /* Sanity: a legitimate pointer still works. */
    int n = ureadfile("sys/bootcount", scratch, (int) sizeof(scratch));
    if (n < 0) {
        fails++;
        uprint("[badptr] valid readfile FAILED\n");
    }

    const char *verdict = fails ? "fail" : "ok";
    uwritefile("badptr.txt", verdict, ustrlen(verdict));
    uprint(fails ? "[badptr] some hostile pointers got through\n"
                 : "[badptr] all hostile pointers rejected\n");
    return 0;
}
