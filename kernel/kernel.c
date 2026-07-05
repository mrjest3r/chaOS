#include "../cpu/isr.h"
#include "../cpu/paging.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "kernel.h"
#include "kheap.h"
#include "shell.h"
#include "fs.h"
#include "task.h"
#include "syscall.h"
#include "user_demo.h"
#include "elf.h"
#include "../cpu/gdt.h"
#include "../cpu/timer.h"
#include "../cpu/power.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include "../libc/function.h"
#include <stdint.h>

/* Boot-time sanity checks. Results go to the serial port so they can be
 * captured without a display. */
static void run_selftests() {
    serial_write("[test] heap alloc/write/free: ");

    uint8_t *p1 = (uint8_t *) malloc(100);
    uint8_t *p2 = (uint8_t *) malloc(200);
    if (!p1 || !p2 || p1 == p2) {
        serial_write("FAIL (allocation)\n");
        return;
    }

    int ok = 1;
    for (int i = 0; i < 100; i++) p1[i] = (uint8_t) (i & 0xFF);
    for (int i = 0; i < 100; i++) if (p1[i] != (uint8_t) (i & 0xFF)) ok = 0;

    free(p1);
    free(p2);

    /* After freeing and coalescing, a fresh allocation should succeed. */
    void *p3 = malloc(100);
    if (!p3) ok = 0;
    free(p3);

    serial_write(ok ? "PASS\n" : "FAIL\n");
}

/* Exercises the filesystem and demonstrates persistence: a "bootcount" file
 * is read, incremented and written back on every boot. */
static void run_fs_selftest() {
    if (!fs_mount()) {
        serial_write("[test] fs: SKIP (no disk)\n");
        return;
    }

    serial_write("[test] fs read/write: ");

    uint32_t count = 0;
    uint8_t buf[8];
    if (fs_read_file("bootcount", buf, sizeof(buf)) >= 4) {
        count = (uint32_t) buf[0] | ((uint32_t) buf[1] << 8)
              | ((uint32_t) buf[2] << 16) | ((uint32_t) buf[3] << 24);
    }
    count++;

    uint8_t out[4] = {
        (uint8_t) (count), (uint8_t) (count >> 8),
        (uint8_t) (count >> 16), (uint8_t) (count >> 24)
    };
    fs_write_file("bootcount", out, 4);

    /* Verify the write round-trips. */
    uint32_t check = 0;
    uint8_t vbuf[8];
    if (fs_read_file("bootcount", vbuf, sizeof(vbuf)) >= 4) {
        check = (uint32_t) vbuf[0] | ((uint32_t) vbuf[1] << 8)
              | ((uint32_t) vbuf[2] << 16) | ((uint32_t) vbuf[3] << 24);
    }
    serial_write(check == count ? "PASS\n" : "FAIL\n");

    char nbuf[16];
    int_to_ascii((int) count, nbuf);
    serial_write("[test] boot count = ");
    serial_write(nbuf);
    serial_write("\n");
}

/* Background kernel threads used to demonstrate multitasking. Each just does a
 * bit of busy work and bumps its own activity counter, forever. */
static void worker_thread() {
    for (;;) {
        for (volatile int i = 0; i < 300000; i++) { }
        task_inc();
    }
}

#ifdef AUTOTEST
/* AUTOTEST: spawn two worker threads, let the scheduler run them for a while,
 * verify both made progress, then power off the emulator. */
static void run_tasking_selftest() {
    task_create(worker_thread);
    task_create(worker_thread);
    multitasking_on = 1;

    uint32_t start = timer_ticks();
    while (timer_ticks() - start < 100) { /* ~2s at 50 Hz */
        asm volatile("hlt");
    }

    uint32_t c1 = task_get_counter(1);
    uint32_t c2 = task_get_counter(2);
    serial_write("[test] multitasking: ");
    serial_write((c1 > 0 && c2 > 0) ? "PASS\n" : "FAIL\n");

    char b[16];
    int_to_ascii((int) c1, b); serial_write("[test] task1 counter = "); serial_write(b); serial_write("\n");
    int_to_ascii((int) c2, b); serial_write("[test] task2 counter = "); serial_write(b); serial_write("\n");
}

/* AUTOTEST: spawn two ring-3 user tasks that run concurrently. Each writes a
 * file, sleeps (blocking via the scheduler), reads it back and exits through
 * SYS_EXIT. We wait until both terminate, proving preemptive user-mode
 * multitasking, blocking syscalls and process teardown all work. */
static void run_usermode_selftest() {
    serial_write("[test] spawning 2 ring-3 user tasks...\n");
    int a = usermode_spawn();
    int b = usermode_spawn();
    if (a < 0 || b < 0) {
        serial_write("[test] user tasks: FAIL (spawn)\n");
        return;
    }

    uint32_t start = timer_ticks();
    while ((task_alive(a) || task_alive(b)) && (timer_ticks() - start) < 400) {
        asm volatile("hlt");
    }

    int done = !task_alive(a) && !task_alive(b);
    serial_write("[test] user tasks: ");
    serial_write(done ? "PASS (both exited cleanly)\n" : "FAIL (timeout)\n");
}

/* AUTOTEST: spawn a rogue ring-3 task that runs a privileged instruction. The
 * kernel must fault-isolate it (terminate the task) and keep running. If the
 * kernel survived to run this and the task ends up dead, fault isolation works.*/
static void run_fault_isolation_selftest() {
    serial_write("[test] spawning rogue ring-3 task (expects a #GP)...\n");
    int r = usermode_spawn_faulting();
    if (r < 0) {
        serial_write("[test] fault isolation: FAIL (spawn)\n");
        return;
    }

    uint32_t start = timer_ticks();
    while (task_alive(r) && (timer_ticks() - start) < 200) {
        asm volatile("hlt");
    }

    serial_write("[test] fault isolation: ");
    serial_write(!task_alive(r) ? "PASS (rogue task killed, kernel alive)\n"
                                : "FAIL (task still alive)\n");
}

/* AUTOTEST: load and run an ELF program from the disk filesystem. The program
 * (user/hello.elf) is injected into the image by 'make programs'. This proves
 * the whole pipeline: fs read -> ELF parse -> segment load -> ring-3 exec. */
static void run_elf_selftest() {
    serial_write("[test] exec hello.elf from disk...\n");
    int rc = elf_exec("hello.elf");
    serial_write(rc == 0 ? "[test] elf loader: PASS\n"
                         : "[test] elf loader: FAIL\n");
}
#endif /* AUTOTEST */

void kernel_main() {
    init_serial();
    serial_write("\n[boot] serial ready\n");

    init_gdt();
    serial_write("[boot] GDT + TSS loaded\n");

    isr_install();
    irq_install();
    syscall_init();
    serial_write("[boot] interrupts + syscalls installed\n");

    init_paging();
    serial_write("[boot] paging enabled (first 16 MiB identity-mapped)\n");

    init_kheap();
    serial_write("[boot] kernel heap initialized\n");

    run_selftests();
    run_fs_selftest();

    tasking_init();
    serial_write("[boot] tasking initialized\n");

#ifdef AUTOTEST
    run_tasking_selftest();
    run_usermode_selftest();
    run_fault_isolation_selftest();
    run_elf_selftest();

    serial_write("[test] done, powering off\n");
    qemu_debug_exit(0);
    for (;;) asm volatile("hlt");
#else
    /* Interactive mode: run one background heartbeat thread so the 'tasks'
     * command visibly shows concurrent progress while you use the shell. */
    task_create(worker_thread);
    multitasking_on = 1;

    clear_screen();
    kprint("              888                  .d88888b.   .d8888b.\n");
    kprint("              888                 d88P\" \"Y88b d88P  Y88b\n");
    kprint("              888                 888     888 Y88b.\n");
    kprint("     .d8888b  88888b.  8888b.     888     888  \"Y888b.\n");
    kprint("    d88P\"     888 \"88b     \"88b   888     888     \"Y88b.\n");
    kprint("    888       888  888 .d888888   888     888       \"888\n");
    kprint("    Y88b.     888  888 888  888   Y88b. .d88P Y88b  d88P\n");
    kprint("     \"Y8888P  888  888 \"Y888888    \"Y88888P\"   \"Y8888P\"\n\n");
    kprint("=== chaOS ===\n");
    kprint("Paging on, heap + disk ready, multitasking live.\n");
    kprint("Type 'help' for commands.\n");
    shell_prompt();
#endif
}

void user_input(char *input) {
    shell_execute(input);
}
