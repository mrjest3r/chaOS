#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/serial.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../drivers/fb.h"
#include "../drivers/idle.h"
#include "../drivers/hda.h"
#include "../drivers/usb_audio.h"
#include "kheap.h"
#include "shell.h"
#include "fs.h"
#include "task.h"
#include "syscall.h"
#include "elf.h"
#include "initrd.h"
#include "../libc/string.h"
#include "kernel.h"
#include <stdint.h>

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
int kernel_initrd_installed(void) { return initrd_installed; }
#endif

/* Background kernel threads used to demonstrate multitasking. */
void worker_thread(void) {
    for (;;) {
        for (volatile int i = 0; i < 300000; i++) { }
        task_inc();
    }
}

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
    {
        char mb[16];
        int_to_ascii(mem_mapped_bytes() / (1024u * 1024u), mb);
        serial_write("[boot] memory: ");
        serial_write(mb);
        serial_write(" MiB identity map, 16 MiB higher-half alias\n");
    }

    init_kheap();
    serial_write("[boot] kernel heap initialized\n");

    hda_init();
    usb_audio_init();

    fb_init();
    mouse_enable_cursor();
    screen_history_init();
    idle_init();
    serial_write("[boot] screen scrollback ready\n");

    if (!fs_mount())
        serial_write("[fs] mount failed\n");
    else
        serial_write(fs_is_ram() ? "[fs] no IDE disk - RAM filesystem\n"
                                 : "[fs] IDE disk mounted\n");
    seed_from_initrd();

#ifdef AUTOTEST
    run_early_selftests();
#endif

    tasking_init();
    serial_write("[boot] tasking initialized\n");

#ifdef AUTOTEST
    run_autotests();
    serial_write("[test] done, powering off\n");
    qemu_debug_exit(0);
    for (;;) asm volatile("hlt");
#else
    /* Interactive: skip the on-screen self-tests so a CD/USB boot goes
     * straight to the desk instead of looking like a console probe. */
    task_create(worker_thread);
    multitasking_on = 1;

    /* The desk does not return here on q or F1. This shell runs when
     * there is no framebuffer, or if the compositor exits.
     * gate asks for a username and a password before the desk. */
    if (fs_exists("gate.elf")) {
        serial_write("[boot] gate\n");
        elf_exec("gate.elf");
    }
    /* Clicking the doom icon writes "launchdoom" and leaves the desk.
     * Run the game here, then start the desk again. */
    for (;;) {
        if (fs_exists("launchdoom") && fs_exists("doom.elf")) {
            fs_delete("launchdoom");
            serial_write("[boot] doom\n");
            elf_exec("doom.elf");
            continue;
        }
        if (fb_available() && fs_exists("chaoswm.elf")) {
            serial_write("[boot] desk\n");
            elf_exec("chaoswm.elf");
            if (fs_exists("launchdoom"))
                continue;
        }
        break;
    }

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
    kprint("No desk: this kernel shell is only here without a framebuffer.\n");
    kprint("On a framebuffer boot, open the terminal chip. q and F1 stay on the desk.\n");
    kprint("Idle: 'doze' blanks the display; key or mouse wakes it.\n");
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
