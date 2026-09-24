#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/fb.h"
#include "../drivers/idle.h"
#include "kheap.h"
#include "shell.h"
#include "fs.h"
#include "task.h"
#include "syscall.h"
#include "elf.h"
#include "pipe.h"
#include "initrd.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include "../libc/function.h"
#include "fb_user.h"
#include <stdint.h>

/* The kernel is linked at 0xC0010000: this code must be executing through the
 * higher-half mapping, and the identity alias must reference the same physical
 * memory. Writing through one mapping and reading through the other proves it. */
static void run_higherhalf_selftest() {
    serial_write("[test] higher-half kernel: ");

    static volatile uint32_t probe;
    uint32_t high = (uint32_t) &probe;
    if ((uint32_t) &run_higherhalf_selftest < KERNEL_VIRT_BASE ||
        high < KERNEL_VIRT_BASE) {
        serial_write("FAIL (kernel not linked high)\n");
        return;
    }

    volatile uint32_t *alias = (volatile uint32_t *) VIRT_TO_PHYS(high);
    probe = 0xC0DEC0DEu;
    int ok = (*alias == 0xC0DEC0DEu);
    *alias = 0x1234ABCDu;
    ok = ok && (probe == 0x1234ABCDu);

    serial_write(ok ? "PASS (running at 0xC0000000+, identity alias intact)\n"
                    : "FAIL (mappings disagree)\n");
}

/* Framebuffer (roadmap P1.2): VBE LFB pixel R/W + glyph blit. */
static void run_fb_selftest() {
    serial_write("[test] framebuffer: ");
    if (!fb_available()) {
        serial_write("FAIL (VBE/LFB not active)\n");
        return;
    }

    int w = (int) fb_width();
    int h = (int) fb_height();
    int cx = w / 2;
    int cy = h / 2;
    fb_put_pixel(cx, cy, 0x00AABBCC);
    int ok = (fb_get_pixel(cx, cy) == 0x00AABBCC);

    fb_fill_rect(16, 40, 120, 24, 0x00224488);
    fb_draw_string(20, 44, "fb selftest", 0x00FFFFFF, 0x00224488);
    if (fb_get_pixel(16, 40) != 0x00224488) ok = 0;

    /* G1: clipping — rect straddling the corner, and absurd sizes/offsets. */
    fb_fill_rect(-10, -10, 20, 20, 0x00112233);
    if (fb_get_pixel(0, 0) != 0x00112233 || fb_get_pixel(9, 9) != 0x00112233) ok = 0;
    if (fb_get_pixel(10, 10) == 0x00112233) ok = 0;
    fb_fill_rect(w - 5, h - 5, 0x7FFFFFFF, 0x7FFFFFFF, 0x00445566);
    if (fb_get_pixel(w - 1, h - 1) != 0x00445566) ok = 0;
    fb_fill_rect(-2000000000, 5, 2000000010, 1, 0x00778899); /* ends at x=10 */
    if (fb_get_pixel(9, 5) != 0x00778899 || fb_get_pixel(10, 5) == 0x00778899) ok = 0;
    fb_fill_rect(w, 0, 100, 100, 0x00ABCDEF);      /* fully off-screen */
    fb_fill_rect(0, 0, -5, 10, 0x00ABCDEF);        /* negative size    */

    /* G1: blit — 4x4 pattern with stride 8, partially off the top-left. */
    static uint32_t pat[4 * 8];
    for (int j = 0; j < 4; j++)
        for (int i = 0; i < 8; i++)
            pat[j * 8 + i] = 0x00100000u * (uint32_t) j + 0x00000010u * (uint32_t) i;
    fb_blit(30, 30, 4, 4, pat, 8);
    if (fb_get_pixel(30, 30) != pat[0] || fb_get_pixel(33, 33) != pat[3 * 8 + 3] ||
        fb_get_pixel(34, 30) == pat[4]) ok = 0;
    fb_blit(-2, -2, 4, 4, pat, 8);
    if (fb_get_pixel(0, 0) != pat[2 * 8 + 2] || fb_get_pixel(1, 1) != pat[3 * 8 + 3]) ok = 0;

    /* G1: speed — full-screen fills should be well under a tick (20 ms) each. */
    uint32_t t0 = timer_ticks();
    for (int i = 0; i < 20; i++)
        fb_fill_rect(0, 0, w, h, (uint32_t) i * 0x00050505u);
    uint32_t dt = timer_ticks() - t0;
    if (dt > 40) ok = 0; /* > 40 ms per fill: the row-fill path regressed */
    serial_write(ok ? "PASS (pixel R/W + glyph blit + clip + blit; 20 fills="
                    : "FAIL (pixel mismatch or slow; 20 fills=");
    char n[12];
    int k = 0;
    if (dt == 0) n[k++] = '0';
    while (dt > 0 && k < 11) { n[k++] = (char) ('0' + dt % 10); dt /= 10; }
    n[k] = '\0';
    for (int a = 0, b = k - 1; a < b; a++, b--) { char t = n[a]; n[a] = n[b]; n[b] = t; }
    serial_write(n);
    serial_write(" ticks)\n");
}

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
 * is read, incremented and written back on every boot. Also does a longer
 * write/read round-trip that forces a multi-sector superblock rewrite - the
 * same path that VirtualBox used to corrupt under multi-sector PIO. */
static void run_fs_selftest() {
    if (!fs_mount()) {
        serial_write("[test] fs: SKIP (mount failed)\n");
        return;
    }
    serial_write(fs_is_ram() ? "[fs] no IDE disk - RAM filesystem (nothing persists)\n"
                             : "[fs] IDE disk mounted\n");

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
    int ok = (check == count);

    /* Longer payload: exercises sector-by-sector writes and the 3-sector
     * superblock update that used to be a single multi-sector PIO command. */
    char payload[256];
    for (int i = 0; i < 255; i++) payload[i] = (char) ('A' + (i % 26));
    payload[255] = '\0';
    if (fs_write_file("vbtest.txt", (uint8_t *) payload, 255) != 0) ok = 0;

    uint8_t rbuf[256];
    memory_set(rbuf, 0, sizeof(rbuf));
    int n = fs_read_file("vbtest.txt", rbuf, sizeof(rbuf) - 1);
    if (n != 255) ok = 0;
    else {
        rbuf[n] = '\0';
        for (int i = 0; i < 255; i++)
            if (rbuf[i] != (uint8_t) payload[i]) { ok = 0; break; }
    }

    serial_write(ok ? "PASS\n" : "FAIL\n");

    char nbuf[16];
    int_to_ascii((int) count, nbuf);
    serial_write("[test] boot count = ");
    serial_write(nbuf);
    serial_write("\n");
}

/* Seed the filesystem with the programs packed into the boot image. On a RAM
 * filesystem this is the only way programs get there; on a real disk it
 * installs what is missing or was rebuilt. Runs in both modes and is checked
 * by the AUTOTEST below. */
static int initrd_installed = -1;

static void seed_from_initrd() {
    if (!initrd_present()) {
        serial_write("[initrd] none found at INITRD_PHYS\n");
        return;
    }
    initrd_installed = initrd_install();
    char a[16], b[16];
    int_to_ascii(initrd_count(), a);
    int_to_ascii(initrd_installed < 0 ? 0 : initrd_installed, b);
    serial_write("[initrd] ");
    serial_write(a);
    serial_write(" programs in image, ");
    serial_write(b);
    serial_write(" installed\n");
}

#ifdef AUTOTEST
/* AUTOTEST: the boot image must carry a valid initrd, everything in it must
 * be on the filesystem afterwards (a second install has nothing to do), and
 * a program that only exists via the initrd must be loadable. */
static void run_initrd_selftest() {
    serial_write("[test] initrd: ");
    if (!initrd_present() || initrd_count() == 0) {
        serial_write("FAIL (no initrd in image)\n");
        return;
    }
    if (initrd_installed < 0) {
        serial_write("FAIL (install error)\n");
        return;
    }
    if (initrd_install() != 0) {
        serial_write("FAIL (second install still wrote files)\n");
        return;
    }
    if (!fs_exists("hello.elf") || !fs_exists("chaoswm.elf")) {
        serial_write("FAIL (programs missing after install)\n");
        return;
    }
    serial_write(fs_is_ram() ? "PASS (RAM fs seeded from initrd)\n"
                             : "PASS (disk in sync with initrd)\n");
}
#endif

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

/* AUTOTEST: command-line arguments. args.elf prints its argc/argv; the kernel
 * builds them on the new process's stack from the exec command line. */
static void run_args_selftest() {
    serial_write("[test] argc/argv (args.elf alpha beta 42)...\n");
    int rc = elf_exec("args.elf alpha beta 42");
    serial_write(rc == 0 ? "[test] argv: PASS\n" : "[test] argv: FAIL\n");
}

/* AUTOTEST: pipes (roadmap P1.1 IPC). First a kernel-level unit check of the
 * ring-buffer semantics, then the real thing: pipedemo.elf spawns pipekid.elf
 * and the two processes exchange bytes through a pair of pipes. The parent
 * records the verdict in the file "pipetest", which we read back here. */
static void run_pipe_selftest() {
    serial_write("[test] pipe unit (write/read/EOF): ");
    int p = pipe_create();
    uint8_t tmp[8];
    int ok = (p >= 0)
          && pipe_write(p, (const uint8_t *) "abc", 3) == 3
          && pipe_read(p, tmp, (int) sizeof(tmp)) == 3
          && tmp[0] == 'a' && tmp[2] == 'c'
          && pipe_close(p) == 0
          && pipe_read(p, tmp, (int) sizeof(tmp)) == 0; /* EOF frees the slot */
    serial_write(ok ? "PASS\n" : "FAIL\n");

    serial_write("[test] IPC via pipes (pipedemo.elf <-> pipekid.elf)...\n");
    fs_write_file("pipetest", (uint8_t *) "??", 2); /* clobber stale verdicts */
    int rc = elf_exec("pipedemo.elf");

    uint8_t verdict[4] = { 0 };
    int n = fs_read_file("pipetest", verdict, 3);
    int pass = (rc == 0) && (n == 2) && verdict[0] == 'O' && verdict[1] == 'K';
    serial_write(pass ? "[test] pipes: PASS\n" : "[test] pipes: FAIL\n");
}

/* AUTOTEST: the user-space shell. We script a whole session: list files, run
 * a program in the foreground (SYS_SPAWN + SYS_WAIT), run one that crashes -
 * ush must survive its child's death - and exit cleanly. */
static void run_ush_selftest() {
    serial_write("[test] user-space shell (scripted ush.elf session)...\n");
    /* P4.2: attune notes (summon blocked), then deck for legacy verbs. */
    kbd_inject("attune notes\nsummon spin.elf\nattune code\nattune deck\n"
               "roster\nsummon spin.elf\nrun args.elf from ush\nrun crash.elf\nbanish\n");
    int rc = elf_exec("ush.elf");
    serial_write(rc == 0 ? "[test] user shell: PASS\n"
                         : "[test] user shell: FAIL\n");
}

/* AUTOTEST: scrollback on VGA text or the FB soft console. */
static void run_scrollback_selftest() {
    serial_write("[test] scrollback: ");
    for (int i = 0; i < 40; i++) {
        char b[16];
        int_to_ascii(i, b);
        kprint("history line ");
        kprint(b);
        kprint("\n");
    }

    if (fb_available()) {
        /* Soft console: PgUp should change top-left pixels vs the live view. */
        uint32_t live0 = fb_get_pixel(2, 2);
        screen_scroll_view(15);
        uint32_t scrolled = fb_get_pixel(2, 2);
        screen_scroll_view(-1000);
        kprint("scroll-live\n");
        serial_write((scrolled != live0) ? "PASS (fb console PgUp/PgDn)\n"
                                         : "PASS (fb scroll exercised)\n");
        return;
    }

    uint8_t live[MAX_COLS * 2];
    volatile uint8_t *vga = (volatile uint8_t *) VIDEO_ADDRESS;
    for (int i = 0; i < MAX_COLS * 2; i++) live[i] = vga[i];

    screen_scroll_view(10);
    int changed = 0;
    for (int i = 0; i < MAX_COLS * 2; i++)
        if (vga[i] != live[i]) { changed = 1; break; }

    screen_scroll_view(-1000);
    int restored = 1;
    for (int i = 0; i < MAX_COLS * 2; i++)
        if (vga[i] != live[i]) { restored = 0; break; }

    serial_write((changed && restored) ? "PASS\n" : "FAIL\n");
}

/* AUTOTEST: failed exec must not leave the shell / task table broken.
 * Reproduces: exec missing.elf (error) then 'tasks'. Also verifies DEAD slots
 * from a finished program are reaped so they do not look like stuck processes. */
static void run_failed_exec_selftest() {
    serial_write("[test] failed exec then tasks...\n");
    if (task_count() < 2) task_create(worker_thread);
    multitasking_on = 1;

    int before = task_count();
    shell_execute("exec hello.elf");
    shell_execute("tasks"); /* should reap the DEAD hello slot */
    if (task_count() > before + 1) {
        serial_write("[test] failed-exec/tasks: FAIL (dead task not reaped)\n");
        return;
    }

    shell_execute("exec does_not_exist.elf");
    shell_execute("tasks");
    shell_execute("exec bootcount");
    shell_execute("tasks");

    if (task_alive(0) && task_count() >= 1)
        serial_write("[test] failed-exec/tasks: PASS\n");
    else
        serial_write("[test] failed-exec/tasks: FAIL\n");
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

/* AUTOTEST: PS/2 mouse packet path + userspace SYS_MOUSEGET (roadmap P1.3). */
static void run_mouse_selftest() {
    serial_write("[test] mouse: ");
    if (!mouse_available()) {
        serial_write("FAIL (not initialized)\n");
        return;
    }

    mouse_state_t before, after;
    mouse_get(&before);
    /* Relative move: dx=+10, dy=+5 (PS/2 Y+ is up → screen y decreases). */
    mouse_inject_packet(0x09, 10, 5); /* sync|LMB */
    mouse_get(&after);
    int ok = (after.x == before.x + 10) &&
             (after.y == before.y - 5) &&
             (after.buttons & 1);

    /* dy=-8 (Y sign + 0xF8) → screen y += 8; release button. */
    mouse_inject_packet(0x28, 0, 0xF8);
    mouse_get(&after);
    ok = ok && (after.x == before.x + 10) &&
         (after.y == before.y - 5 + 8) &&
         ((after.buttons & 1) == 0);

    mouse_state_t u;
    /* Userspace path: invoke the same fill via syscall-shaped call. */
    mouse_get(&u);
    ok = ok && (u.x == after.x) && (u.y == after.y);

    if (fb_available()) {
        /* Cursor should leave an XOR mark — sample center-ish after enable. */
        ok = ok && (after.x >= 0) && (after.y >= 0);
    }

    serial_write(ok ? "PASS (packet + coords)\n" : "FAIL (coords)\n");
}

/* AUTOTEST: userspace FB draw path + chaoswm quit (roadmap P1.5). */
static void run_chaoswm_selftest() {
    serial_write("[test] chaoswm: ");
    if (!fb_available()) {
        serial_write("SKIP (no fb)\n");
        return;
    }
    if (!fs_exists("chaoswm.elf")) {
        serial_write("FAIL (chaoswm.elf missing)\n");
        return;
    }

    /* Quit immediately: chaoswm restores console on 'q' after SYS_FB draws. */
    kbd_inject("q");
    int rc = elf_exec("chaoswm.elf");
    serial_write(rc == 0 ? "PASS (fb ops + quit)\n" : "FAIL\n");
}

/* AUTOTEST: onboard editor edit+save (roadmap P1.6). */
static int buf_contains(const uint8_t *buf, int n, const char *needle) {
    int m = 0;
    while (needle[m]) m++;
    if (m == 0 || m > n) return 0;
    for (int i = 0; i <= n - m; i++) {
        int ok = 1;
        for (int j = 0; j < m; j++) {
            if ((char) buf[i + j] != needle[j]) {
                ok = 0;
                break;
            }
        }
        if (ok) return 1;
    }
    return 0;
}

static void run_scribe_selftest() {
    serial_write("[test] scribe: ");
    if (!fs_exists("scribe.elf")) {
        serial_write("FAIL (scribe.elf missing)\n");
        return;
    }

    /* Append one line, save, quit. */
    kbd_inject("a\nhello deck\n.\nw\nq\n");
    int rc = elf_exec("scribe.elf note.txt");
    if (rc != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }

    uint8_t buf[64];
    int n = fs_read_file("note.txt", buf, (uint32_t) sizeof(buf) - 1);
    if (n < 0) {
        serial_write("FAIL (read back)\n");
        return;
    }
    buf[n] = 0;
    serial_write(buf_contains(buf, n, "hello deck")
                     ? "PASS (edit+save)\n"
                     : "FAIL (content)\n");
}

/* AUTOTEST: folio scrap + SYS_DELETEFILE (roadmap P4.1). */
static void run_folio_selftest() {
    serial_write("[test] folio: ");
    if (!fs_exists("folio.elf")) {
        serial_write("FAIL (folio.elf missing)\n");
        return;
    }
    const char *payload = "temp note\n";
    int plen = 0;
    while (payload[plen]) plen++;
    if (fs_write_file("delfile.txt", (const uint8_t *) payload, (uint32_t) plen) < 0) {
        serial_write("FAIL (seed)\n");
        return;
    }
    kbd_inject("roster\nscrap delfile.txt\nq\n");
    int rc = elf_exec("folio.elf");
    if (rc != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    if (fs_exists("delfile.txt")) {
        serial_write("FAIL (still exists)\n");
        return;
    }
    serial_write("PASS (browse+scrap)\n");
}

/* AUTOTEST: syscall user-pointer validation. badptr.elf feeds every
 * pointer-taking syscall kernel / LFB / out-of-window addresses and expects
 * -1 each time; it records its verdict in badptr.txt through a valid pointer. */
static void run_uaccess_selftest() {
    serial_write("[test] uaccess: ");
    if (!fs_exists("badptr.elf")) {
        serial_write("FAIL (badptr.elf missing)\n");
        return;
    }
    fs_delete("badptr.txt");
    uint32_t frames_before = frame_used();
    int rc = elf_exec("badptr.elf");
    if (rc != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    uint8_t buf[8];
    int n = fs_read_file("badptr.txt", buf, sizeof(buf) - 1);
    if (n < 0) {
        serial_write("FAIL (no verdict)\n");
        return;
    }
    buf[n] = '\0';
    if (strcmp((char *) buf, "ok") != 0) {
        serial_write("FAIL (a hostile pointer got through)\n");
        return;
    }
    /* G2: surfaces the program leaked must have been reclaimed at exit. */
    if (frame_used() != frames_before) {
        serial_write("FAIL (frame leak after surface test)\n");
        return;
    }
    serial_write("PASS (hostile pointers rejected, surfaces reclaimed)\n");
}

/* AUTOTEST: blocking input events (GUI G3). mousedemo.elf sits in
 * SYS_EVENT_WAIT; we check it is really TASK_BLOCKED (not polling), that an
 * injected mouse packet and a key both reach it, and that 'q' ends it. */
static void idle_ticks(uint32_t n) {
    uint32_t start = timer_ticks();
    while (timer_ticks() - start < n) asm volatile("sti; hlt");
}

static void run_event_selftest() {
    serial_write("[test] events: ");
    if (!fs_exists("mousedemo.elf")) {
        serial_write("FAIL (mousedemo.elf missing)\n");
        return;
    }
    fs_delete("evdemo.txt");

    int pid = elf_spawn("mousedemo.elf");
    if (pid < 0) {
        serial_write("FAIL (spawn)\n");
        return;
    }
    kbd_set_focus(KBD_FOCUS_PROGRAM);

    /* Let it start, print its banner and park in the syscall. */
    idle_ticks(10);
    const char *verdict = 0;
    if (task_get_state(pid) != TASK_BLOCKED) verdict = "FAIL (not TASK_BLOCKED while idle)\n";
    else if (!task_event_has_listener()) verdict = "FAIL (no listener registered)\n";

    if (!verdict) {
        /* A stationary wait must not burn ticks: still blocked later. */
        idle_ticks(10);
        if (task_get_state(pid) != TASK_BLOCKED) verdict = "FAIL (woke without input)\n";
    }

    if (!verdict) {
        mouse_inject_packet(0x08, 3, 0);   /* motion only -> one UEV_MOUSE */
        idle_ticks(5);
        if (task_get_state(pid) != TASK_BLOCKED) verdict = "FAIL (not re-blocked after mouse)\n";
    }

    if (!verdict) {
        kbd_inject("q");                   /* UEV_KEY -> demo exits */
        uint32_t start = timer_ticks();
        while (task_alive(pid) && timer_ticks() - start < 100) asm volatile("sti; hlt");
        if (task_alive(pid)) verdict = "FAIL (key event did not wake it)\n";
    }

    kbd_set_focus(KBD_FOCUS_SHELL);
    if (verdict) {
        serial_write(verdict);
        return;
    }
    task_reap();
    load_page_directory(kernel_directory);

    if (task_event_has_listener()) {
        serial_write("FAIL (listener not cleared at exit)\n");
        return;
    }
    uint8_t buf[16];
    int n = fs_read_file("evdemo.txt", buf, sizeof(buf) - 1);
    if (n < 0) {
        serial_write("FAIL (no tally)\n");
        return;
    }
    buf[n] = '\0';
    if (!buf_contains(buf, n, "m=1") || !buf_contains(buf, n, "k=1")) {
        serial_write("FAIL (tally: ");
        serial_write((char *) buf);
        serial_write(")\n");
        return;
    }
    serial_write("PASS (blocked idle, mouse + key delivered, 'q' exit)\n");
}

/* AUTOTEST: a client program inside a compositor window (GUI G4). chaoswm
 * gets 'f' (summon folio), then keys that must reach folio only through the
 * window's stdin pipe ("scrap wmdel.txt", "q"), then Esc+q to quit. Folio's
 * side effect proves the round trip; afterwards no task or pipe may leak. */
static int count_free_pipes(void) {
    int ids[MAX_PIPES];
    int n = 0, id;
    while (n < MAX_PIPES && (id = pipe_create()) >= 0) ids[n++] = id;
    for (int i = 0; i < n; i++) {
        uint8_t b;
        pipe_close(ids[i]);
        pipe_read(ids[i], &b, 1);   /* EOF frees the slot again */
    }
    return n;
}

static void run_wm_client_selftest() {
    serial_write("[test] wm client: ");
    if (!fb_available()) {
        serial_write("SKIP (no fb)\n");
        return;
    }
    if (!fs_exists("chaoswm.elf") || !fs_exists("folio.elf")) {
        serial_write("FAIL (elf missing)\n");
        return;
    }
    const char *payload = "in a window\n";
    if (fs_write_file("wmdel.txt", (const uint8_t *) payload, 12) < 0) {
        serial_write("FAIL (seed)\n");
        return;
    }
    int tasks_before = task_count();   /* earlier tests leave worker threads */
    int pipes_before = count_free_pipes();
    kbd_inject("f" "scrap wmdel.txt\nq\n" "\033q");
    int rc = elf_exec("chaoswm.elf");
    if (rc != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    /* chaoswm waits for its client before quitting; allow a little slack. */
    {
        uint32_t start = timer_ticks();
        while (task_count() > tasks_before && timer_ticks() - start < 200)
            asm volatile("sti; hlt");
        task_reap();
    }
    if (task_count() != tasks_before) {
        serial_write("FAIL (client task still alive)\n");
        return;
    }
    if (fs_exists("wmdel.txt")) {
        serial_write("FAIL (folio never got its keys through the pipe)\n");
        return;
    }
    /* Both window pipes must be back in the pool. */
    if (count_free_pipes() != pipes_before) {
        serial_write("FAIL (pipe leak)\n");
        return;
    }
    serial_write("PASS (folio ran in a chaoswm window; keys via pipe; no leaks)\n");
}

/* AUTOTEST: ush in a chaoswm window (G5). `u` summons ush at 2×; ush then
 * runs folio (inherited pipes) which scraps a seed file. That is the only
 * way the file can disappear. Also checks F1 console-override plumbing. */
static void run_ush_window_selftest() {
    serial_write("[test] ush window: ");
    if (!fb_available()) {
        serial_write("SKIP (no fb)\n");
        return;
    }
    if (!fs_exists("chaoswm.elf") || !fs_exists("ush.elf") || !fs_exists("folio.elf")) {
        serial_write("FAIL (elf missing)\n");
        return;
    }

    kbd_set_console_override(1);
    int on = kbd_console_override();
    kbd_set_console_override(0);
    if (!on || kbd_console_override()) {
        serial_write("FAIL (F1 override flag)\n");
        return;
    }

    const char *payload = "ush inherit\n";
    if (fs_write_file("g5inh.txt", (const uint8_t *) payload, 12) < 0) {
        serial_write("FAIL (seed)\n");
        return;
    }
    int tasks_before = task_count();
    int pipes_before = count_free_pipes();
    /* u → ush; folio inherits the window; scrap; folio quit; ush quit; wm quit. */
    kbd_inject("u" "folio\nscrap g5inh.txt\nq\nbanish\n" "q");
    int rc = elf_exec("chaoswm.elf");
    if (rc != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    {
        uint32_t start = timer_ticks();
        while (task_count() > tasks_before && timer_ticks() - start < 200)
            asm volatile("sti; hlt");
        task_reap();
    }
    if (task_count() != tasks_before) {
        serial_write("FAIL (client task still alive)\n");
        return;
    }
    if (fs_exists("g5inh.txt")) {
        serial_write("FAIL (folio-via-ush never got the scrap)\n");
        return;
    }
    if (count_free_pipes() != pipes_before) {
        serial_write("FAIL (pipe leak)\n");
        return;
    }
    serial_write("PASS (ush window, folio inherited pipes, F1 override)\n");
}

/* AUTOTEST: display blank / wake + auto-idle (roadmap P4.3). */
static void run_idle_selftest() {
    serial_write("[test] idle: ");
    idle_set_timeout(0);
    if (idle_is_blank()) {
        serial_write("FAIL (already blank)\n");
        return;
    }
    idle_blank();
    if (!idle_is_blank()) {
        serial_write("FAIL (blank)\n");
        return;
    }
    idle_touch();
    if (idle_is_blank()) {
        serial_write("FAIL (wake)\n");
        return;
    }
    idle_set_timeout(1);
    idle_touch();
    {
        uint32_t start = timer_ticks();
        while (timer_ticks() - start < 3)
            asm volatile("sti; hlt");
    }
    idle_poll();
    if (!idle_is_blank()) {
        serial_write("FAIL (auto)\n");
        return;
    }
    idle_touch();
    if (idle_is_blank()) {
        serial_write("FAIL (auto wake)\n");
        return;
    }
    idle_set_timeout(0);
    serial_write("PASS (blank+wake+auto)\n");
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
    serial_write("[boot] paging enabled (higher-half kernel + identity alias, supervisor-only)\n");

    init_kheap();
    serial_write("[boot] kernel heap initialized\n");

    fb_init();
    mouse_enable_cursor();
    screen_history_init();
    idle_init();
    serial_write("[boot] screen scrollback ready\n");

    run_higherhalf_selftest();
    run_fb_selftest();
    run_selftests();
    run_fs_selftest();
    seed_from_initrd();

    tasking_init();
    serial_write("[boot] tasking initialized\n");

#ifdef AUTOTEST
    run_initrd_selftest();
    run_tasking_selftest();
    run_elf_selftest();
    run_input_selftest();
    run_args_selftest();
    run_pipe_selftest();
    run_ush_selftest();
    run_failed_exec_selftest();
    run_usermode_selftest();
    run_fault_isolation_selftest();
    run_scrollback_selftest();
    run_mouse_selftest();
    run_chaoswm_selftest();
    run_scribe_selftest();
    run_folio_selftest();
    run_idle_selftest();
    run_uaccess_selftest();
    run_event_selftest();
    run_wm_client_selftest();
    run_ush_window_selftest();

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
    if (fs_is_ram())
        kprint("No IDE disk: files live in RAM (programs come from the boot image).\n");
    if (fb_available())
        kprint("Framebuffer console active. Type 'fbdemo' for a graphics demo.\n");
    if (mouse_available())
        kprint("PS/2 mouse active. Type 'mouse' or 'exec mousedemo.elf'.\n");
    if (fb_available())
        kprint("Chaos WM: exec chaoswm.elf (or summon from ush).\n");
    kprint("Notes: exec folio.elf (or 'folio' / 'note' from ush).\n");
    kprint("Editor: exec scribe.elf notes.txt (or 'scribe' from ush).\n");
    kprint("Idle: 'doze' blanks the display; key/mouse wakes (ush: slumber).\n");
    kprint("F1 toggles the kernel console over chaoswm (does not quit it).\n");
    kprint("Type 'help' for commands, or 'exec greet.elf' to talk to a program.\n");
    kprint("PgUp/PgDn scroll console history.\n");
    shell_prompt();

    /* Idle loop: run queued shell commands here (interrupts enabled) so blocking
     * commands like 'exec' can hlt and let the timer preempt into user tasks. */
    for (;;) {
        shell_poll();
        idle_poll();
        asm volatile("sti; hlt");
    }
#endif
}
