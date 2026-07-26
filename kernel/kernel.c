#include "../cpu/isr.h"
#include "../cpu/paging.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "kheap.h"
#include "shell.h"
#include "fs.h"
#include "task.h"
#include "syscall.h"
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

/* AUTOTEST: load and run an ELF program from the disk filesystem. The program
 * (user/hello.elf) is injected into the image by 'make programs'. This proves
 * the whole pipeline: fs read -> address space -> ELF parse -> segment load ->
 * ring-3 exec -> clean teardown. */
static void run_elf_selftest() {
    serial_write("[test] exec hello.elf from disk...\n");
    int rc = elf_exec("hello.elf");
    serial_write(rc == 0 ? "[test] elf loader: PASS\n"
                         : "[test] elf loader: FAIL\n");
}

/* AUTOTEST: spawn two copies of spin.elf at once. Each runs in its own address
 * space at the same virtual address (USER_BASE), sleeps between prints and
 * exits. We wait until both terminate, proving per-process address spaces,
 * preemptive user-mode multitasking, blocking syscalls and teardown all work. */
static void run_usermode_selftest() {
    serial_write("[test] spawning 2 isolated ELF tasks (spin.elf x2)...\n");
    int a = elf_spawn("spin.elf");
    int b = elf_spawn("spin.elf");
    if (a < 0 || b < 0) {
        serial_write("[test] isolated tasks: FAIL (spawn)\n");
        return;
    }

    uint32_t start = timer_ticks();
    while ((task_alive(a) || task_alive(b)) && (timer_ticks() - start) < 600) {
        asm volatile("hlt");
    }

    int done = !task_alive(a) && !task_alive(b);
    serial_write("[test] isolated tasks: ");
    serial_write(done ? "PASS (both isolated tasks exited)\n" : "FAIL (timeout)\n");
}

/* AUTOTEST: prove user programs can read the keyboard. We inject fake
 * keystrokes into the keyboard ring buffer, then run greet.elf, which reads a
 * line via SYS_READLINE and prints a greeting built from it. */
static void run_input_selftest() {
    serial_write("[test] keyboard input syscalls (greet.elf)...\n");
    kbd_inject("chaOS tester\n");
    int rc = elf_exec("greet.elf");
    serial_write(rc == 0 ? "[test] input syscalls: PASS\n"
                         : "[test] input syscalls: FAIL\n");
}

/* AUTOTEST: scrollback. Print enough lines to push some into history, scroll
 * the view up, check the visible top row changed, scroll back down and check
 * the live screen was restored exactly. */
static void run_scrollback_selftest() {
    for (int i = 0; i < 30; i++) {
        char b[16];
        int_to_ascii(i, b);
        kprint_at("history line ", 0, MAX_ROWS - 1);
        kprint(b);
        kprint("\n");
    }

    uint8_t live[MAX_COLS * 2];
    volatile uint8_t *vga = (volatile uint8_t *) VIDEO_ADDRESS;
    for (int i = 0; i < MAX_COLS * 2; i++) live[i] = vga[i];

    screen_scroll_view(10);
    int changed = 0;
    for (int i = 0; i < MAX_COLS * 2; i++)
        if (vga[i] != live[i]) { changed = 1; break; }

    screen_scroll_view(-1000); /* all the way back to live */
    int restored = 1;
    for (int i = 0; i < MAX_COLS * 2; i++)
        if (vga[i] != live[i]) { restored = 0; break; }

    serial_write("[test] scrollback: ");
    serial_write((changed && restored) ? "PASS\n" : "FAIL\n");
}

/* AUTOTEST: run crash.elf, which reads kernel memory from ring 3. With per-
 * process isolation the kernel is mapped supervisor-only, so this page-faults
 * and the kernel terminates just that task. If it ends up dead and the kernel
 * is still running this code, isolation and fault handling both work. */
static void run_fault_isolation_selftest() {
    serial_write("[test] spawning crash.elf (reads kernel memory from ring 3)...\n");
    int r = elf_spawn("crash.elf");
    if (r < 0) {
        serial_write("[test] isolation: FAIL (spawn)\n");
        return;
    }

    uint32_t start = timer_ticks();
    while (task_alive(r) && (timer_ticks() - start) < 200) {
        asm volatile("hlt");
    }

    serial_write("[test] isolation: ");
    serial_write(!task_alive(r) ? "PASS (task killed, kernel alive)\n"
                                : "FAIL (task still alive)\n");
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
    serial_write("[boot] paging enabled (kernel identity-mapped, supervisor-only)\n");

    init_kheap();
    serial_write("[boot] kernel heap initialized\n");

    screen_history_init();
    serial_write("[boot] screen scrollback ready\n");

    run_selftests();
    run_fs_selftest();

    tasking_init();
    serial_write("[boot] tasking initialized\n");

#ifdef AUTOTEST
    run_tasking_selftest();
    run_elf_selftest();
    run_input_selftest();
    run_usermode_selftest();
    run_fault_isolation_selftest();
    run_scrollback_selftest();

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
    kprint("Paging + isolated address spaces, heap + disk, preemptive tasks.\n");
    kprint("Type 'help' for commands, or 'exec greet.elf' to talk to a program.\n");
    kprint("PgUp/PgDn scroll back through screen history.\n");
    shell_prompt();

    /* Idle loop: run queued shell commands here (interrupts enabled) so blocking
     * commands like 'exec' can hlt and let the timer preempt into user tasks. */
    for (;;) {
        shell_poll();
        asm volatile("sti; hlt");
    }
#endif
}
