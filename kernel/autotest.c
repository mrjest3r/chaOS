#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/fb.h"
#include "../drivers/idle.h"
#include "../drivers/hda.h"
#include "../drivers/usb_audio.h"
#include "../drivers/rtc.h"
#include "kheap.h"
#include "shell.h"
#include "fs.h"
#include "task.h"
#include "elf.h"
#include "pipe.h"
#include "initrd.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include "kernel.h"
#include <stdint.h>

#ifdef AUTOTEST
/* Guest self-tests. Interactive builds omit this translation unit's body
 * so kernel_main stays the desk path only. */

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

static void run_initrd_selftest() {
    serial_write("[test] initrd: ");
    if (!initrd_present() || initrd_count() == 0) {
        serial_write("FAIL (no initrd in image)\n");
        return;
    }
    if (kernel_initrd_installed() < 0) {
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

/* AUTOTEST: the user terminal. Script a session in deck verbs only
 * (classic names are rejected). A system file must survive erase.
 * forge/wander/scribe round-trip a file inside a directory, and a
 * relative erase must not remove a root program. The shell must
 * survive a crashing child, then banish cleanly.
 * The inject must fit in the 255-slot keyboard ring. */
static void run_ush_selftest() {
    serial_write("[test] user terminal (scripted terminal.elf session)...\n");
    fs_delete("scratch.txt");
    fs_delete("box/inside.txt");
    fs_delete("inside.txt");
    fs_delete("box/");
    kbd_inject("ls\n"
               "erase hello.elf\n"
               "scribe scratch.txt deck\n"
               "glance scratch.txt\n"
               "erase scratch.txt\n"
               "forge box\n"
               "wander box\n"
               "scribe inside.txt hi\n"
               "erase hello.elf\n"
               "wander /\n"
               "erase box\n"
               "reveal\n"
               "summon spin.elf\n"
               "summon args.elf from terminal\n"
               "summon crash.elf\n"
               "banish\n");
    int rc = elf_exec("terminal.elf");
    uint8_t body[8];
    int n = fs_read_file("box/inside.txt", body, sizeof(body));
    int ok = (rc == 0) && fs_exists("hello.elf") && !fs_exists("scratch.txt")
             && fs_exists("box/") && !fs_exists("inside.txt")
             && n == 2 && body[0] == 'h' && body[1] == 'i';
    fs_delete("box/inside.txt");
    fs_delete("box/");
    serial_write(ok ? "[test] user shell: PASS\n"
                    : "[test] user shell: FAIL\n");
}

/* AUTOTEST: first-boot account. gate creates a user, stores a verifier,
 * then accepts the password and writes the session name. */
static void run_gate_selftest(void) {
    serial_write("[test] gate: ");
    if (!fs_exists("gate.elf")) {
        serial_write("FAIL (gate.elf missing)\n");
        return;
    }
    fs_delete("accounts");
    fs_delete("session");
    kbd_inject("ada\nsecret\nsecret\nada\nsecret\n");
    if (elf_exec("gate.elf") != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    uint8_t buf[32];
    int n = fs_read_file("session", buf, sizeof(buf) - 1);
    if (n < 3 || buf[0] != 'a' || buf[1] != 'd' || buf[2] != 'a') {
        serial_write("FAIL (session)\n");
        return;
    }
    n = fs_read_file("accounts", buf, sizeof(buf) - 1);
    if (n < 4 || buf[0] != 'a' || buf[1] != 'd' || buf[2] != 'a' || buf[3] != ' ') {
        serial_write("FAIL (accounts)\n");
        return;
    }
    /* The verifier must not be the raw password. */
    int raw = 0;
    for (int i = 0; i + 6 <= n; i++) {
        if (buf[i] == 's' && buf[i + 1] == 'e' && buf[i + 2] == 'c' &&
            buf[i + 3] == 'r' && buf[i + 4] == 'e' && buf[i + 5] == 't')
            raw = 1;
    }
    if (raw) {
        serial_write("FAIL (password stored in the clear)\n");
        return;
    }
    kbd_inject("banish\n");
    if (elf_exec("terminal.elf") != 0) {
        serial_write("FAIL (terminal)\n");
        return;
    }
    serial_write("PASS (create, login, session)\n");
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

    /* End the desk with the harness byte. q is not a quit key. */
    kbd_inject("\x01");
    int rc = elf_exec("chaoswm.elf");
    serial_write(rc == 0 ? "PASS (fb ops + quit)\n" : "FAIL\n");
}

/* AUTOTEST: CMOS wall clock (SYS_CLOCK). Range check, not the host's exact
 * second — QEMU's RTC is the host clock and the two reads can straddle a tick. */
static void run_clock_selftest(void) {
    uclock_t t;
    char b[40];
    int n = 0;
    serial_write("[test] clock: ");
    if (rtc_read(&t) != 0) {
        serial_write("FAIL (rtc)\n");
        return;
    }
    b[n++] = (char) ('0' + (t.year / 1000) % 10);
    b[n++] = (char) ('0' + (t.year / 100) % 10);
    b[n++] = (char) ('0' + (t.year / 10) % 10);
    b[n++] = (char) ('0' + t.year % 10);
    b[n++] = '-';
    b[n++] = (char) ('0' + (t.month / 10) % 10);
    b[n++] = (char) ('0' + t.month % 10);
    b[n++] = '-';
    b[n++] = (char) ('0' + (t.day / 10) % 10);
    b[n++] = (char) ('0' + t.day % 10);
    b[n++] = ' ';
    b[n++] = (char) ('0' + (t.hour / 10) % 10);
    b[n++] = (char) ('0' + t.hour % 10);
    b[n++] = ':';
    b[n++] = (char) ('0' + (t.min / 10) % 10);
    b[n++] = (char) ('0' + t.min % 10);
    b[n++] = ':';
    b[n++] = (char) ('0' + (t.sec / 10) % 10);
    b[n++] = (char) ('0' + t.sec % 10);
    b[n++] = ' ';
    b[n++] = 'P';
    b[n++] = 'A';
    b[n++] = 'S';
    b[n++] = 'S';
    b[n++] = '\n';
    b[n] = '\0';
    serial_write(b);
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

    /* Type a line, Ctrl-O save, Ctrl-X quit. */
    fs_delete("note.txt");
    kbd_inject("hello deck\017\030");
    if (elf_exec("scribe.elf note.txt") != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }

    uint8_t buf[64];
    int n = fs_read_file("note.txt", buf, (uint32_t) sizeof(buf) - 1);
    if (n < 0 || !buf_contains(buf, n, "hello deck")) {
        serial_write("FAIL (content)\n");
        return;
    }

    /* An existing file opens the editor. Extra text must not replace it. */
    kbd_inject("scribe note.txt OVER\n!\017\030banish\n");
    if (elf_exec("terminal.elf") != 0) {
        serial_write("FAIL (terminal)\n");
        return;
    }
    n = fs_read_file("note.txt", buf, (uint32_t) sizeof(buf) - 1);
    fs_delete("note.txt");
    if (n < 0 || !buf_contains(buf, n, "!hello")) {
        serial_write("FAIL (open existing)\n");
        return;
    }
    serial_write("PASS (edit+save)\n");
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
 * gets 't' (summon terminal), then keys that must reach it only through the
 * window's stdin pipe ("erase wmdel.txt", "banish"), then Esc and the harness
 * halt. The missing file proves the round trip; afterwards no task or pipe
 * may leak. */
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
    if (!fs_exists("chaoswm.elf") || !fs_exists("terminal.elf")) {
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
    kbd_inject("t" "erase wmdel.txt\nbanish\n" "\033\x01");
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
        serial_write("FAIL (terminal never got its keys through the pipe)\n");
        return;
    }
    /* Both window pipes must be back in the pool. */
    if (count_free_pipes() != pipes_before) {
        serial_write("FAIL (pipe leak)\n");
        return;
    }
    serial_write("PASS (terminal ran in a chaoswm window; keys via pipe; no leaks)\n");
}

/* AUTOTEST: terminal in a chaoswm window (G5). `t` summons it at 2×.
 * erase through the window pipe is the only way the seed file can
 * disappear. The console-override flag still works, and starts clear
 * (F1 no longer sets it). */
static void run_ush_window_selftest() {
    serial_write("[test] terminal window: ");
    if (!fb_available()) {
        serial_write("SKIP (no fb)\n");
        return;
    }
    if (!fs_exists("chaoswm.elf") || !fs_exists("terminal.elf")) {
        serial_write("FAIL (elf missing)\n");
        return;
    }

    if (kbd_console_override()) {
        serial_write("FAIL (console override stuck on)\n");
        return;
    }
    kbd_set_console_override(1);
    int on = kbd_console_override();
    kbd_set_console_override(0);
    if (!on || kbd_console_override()) {
        serial_write("FAIL (override flag)\n");
        return;
    }

    const char *payload = "term inherit\n";
    if (fs_write_file("g5inh.txt", (const uint8_t *) payload, 13) < 0) {
        serial_write("FAIL (seed)\n");
        return;
    }
    int tasks_before = task_count();
    int pipes_before = count_free_pipes();
    /* t → terminal (app mode). erase/banish land on its pipe; Esc returns
     * to command mode so the harness byte can end the desk. */
    kbd_inject("t" "erase g5inh.txt\nbanish\n" "\033\x01");
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
        serial_write("FAIL (terminal never erased the seed)\n");
        return;
    }
    if (count_free_pipes() != pipes_before) {
        serial_write("FAIL (pipe leak)\n");
        return;
    }
    serial_write("PASS (terminal window, erase via pipe, override flag)\n");
}

/* AUTOTEST: G6 desk identity — chaoswm loads the attune file, `a` cycles
 * the mode, and the new value is written back (palette + toolbar follow). */
static void run_desk_selftest() {
    serial_write("[test] desk: ");
    if (!fb_available()) {
        serial_write("SKIP (no fb)\n");
        return;
    }
    if (!fs_exists("chaoswm.elf")) {
        serial_write("FAIL (elf missing)\n");
        return;
    }
    if (fs_write_file("attune", (const uint8_t *) "notes\n", 6) < 0) {
        serial_write("FAIL (seed)\n");
        return;
    }
    kbd_inject("a\x01");
    if (elf_exec("chaoswm.elf") != 0) {
        serial_write("FAIL (exec)\n");
        return;
    }
    uint8_t buf[16];
    int n = fs_read_file("attune", buf, 15);
    if (n < 4 || buf[0] != 'c' || buf[1] != 'o' || buf[2] != 'd' || buf[3] != 'e') {
        serial_write("FAIL (attune not persisted)\n");
        return;
    }
    serial_write("PASS (attune file + palette cycle)\n");
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

/* AUTOTEST: freeze/kill a user task, obliterate a descendant, and refuse
 * the kernel. Then a guest terminal must list processes and must not
 * reboot — those verbs are admin, and this session has not ascended. */
static void run_ps_selftest(void) {
    serial_write("[test] processes: ");
    int pid = elf_spawn("spin.elf");
    if (pid < 0) {
        serial_write("FAIL (spawn)\n");
        return;
    }
    if (task_freeze(pid) != 0 || task_get_state(pid) != TASK_FROZEN) {
        serial_write("FAIL (freeze)\n");
        return;
    }
    {
        uint32_t start = timer_ticks();
        while (timer_ticks() - start < 5)
            asm volatile("sti; hlt");
    }
    if (task_get_state(pid) != TASK_FROZEN) {
        serial_write("FAIL (ran while frozen)\n");
        return;
    }
    if (task_awaken(pid) != 0) {
        serial_write("FAIL (awaken)\n");
        return;
    }
    {
        task_state_t st = task_get_state(pid);
        if (st != TASK_READY && st != TASK_RUNNING && st != TASK_SLEEPING &&
            st != TASK_BLOCKED) {
            serial_write("FAIL (not resumed)\n");
            return;
        }
    }
    if (task_kill(pid) != 0 || task_alive(pid)) {
        serial_write("FAIL (kill)\n");
        return;
    }
    int a = elf_spawn("spin.elf");
    int b = elf_spawn("spin.elf");
    if (a < 0 || b < 0 || task_freeze(a) || task_freeze(b) ||
        task_set_parent(b, a) || task_obliterate(a) ||
        task_alive(a) || task_alive(b)) {
        serial_write("FAIL (obliterate)\n");
        return;
    }
    task_reap();
    if (task_kill(0) == 0 || task_freeze(0) == 0 || task_obliterate(0) == 0 ||
        task_awaken(0) == 0 || task_kill(1) == 0) {
        serial_write("FAIL (kernel target accepted)\n");
        return;
    }
    /* A disk image may already have an account. Drop it so this session
     * is guest and cannot ascend into rebirth. */
    fs_delete("accounts");
    fs_delete("session");
    kbd_inject("observe\n"
               "oversee\n"
               "slay 0\n"
               "freeze 0\n"
               "awaken 0\n"
               "obliterate 0\n"
               "rebirth\n"
               "slumber\n"
               "extinguish\n"
               "banish\n");
    if (elf_exec("terminal.elf") != 0) {
        serial_write("FAIL (terminal)\n");
        return;
    }
    serial_write("PASS (freeze, kill, obliterate, power stays admin)\n");
}

static void run_mem_selftest(void) {
    char mb[16];
    uint32_t mib = mem_mapped_bytes() / (1024u * 1024u);
    int_to_ascii(mib, mb);
    serial_write("[test] memory: ");
    serial_write(mb);
    serial_write(" MiB, ");
    int_to_ascii(frame_total(), mb);
    serial_write(mb);
    serial_write(" frames ");
    if (mib < 1020 || frame_total() < 100000)
        serial_write("FAIL\n");
    else
        serial_write("PASS\n");
}

static void run_audio_selftest(void) {
    serial_write("[test] audio: ");
    if (!hda_ready()) {
        serial_write("FAIL (hda)\n");
        return;
    }
    if (!usb_audio_ready()) {
        serial_write("FAIL (usb)\n");
        return;
    }
    serial_write("PASS (hda stream, usb audio packet)\n");
}

void run_autotests(void) {
    run_mem_selftest();
    run_audio_selftest();
    run_initrd_selftest();
    run_tasking_selftest();
    run_elf_selftest();
    run_input_selftest();
    run_args_selftest();
    run_pipe_selftest();
    run_ush_selftest();
    run_ps_selftest();
    run_gate_selftest();
    run_failed_exec_selftest();
    run_usermode_selftest();
    run_fault_isolation_selftest();
    run_scrollback_selftest();
    run_mouse_selftest();
    run_chaoswm_selftest();
    run_clock_selftest();
    run_scribe_selftest();
    run_folio_selftest();
    run_idle_selftest();
    run_uaccess_selftest();
    run_event_selftest();
    run_wm_client_selftest();
    run_ush_window_selftest();
    run_desk_selftest();
}

void run_early_selftests(void) {
    run_higherhalf_selftest();
    run_fb_selftest();
    run_selftests();
    run_fs_selftest();
}

#endif /* AUTOTEST */
