#include "shell.h"
#include "kheap.h"
#include "fs.h"
#include "task.h"
#include "elf.h"
#include "../cpu/arch.h"
#include "../drivers/screen.h"
#include "../drivers/fb.h"
#include "../drivers/mouse.h"
#include "../drivers/idle.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include <stdint.h>

/* ---- command queue ------------------------------------------------------ */
/* The keyboard IRQ hands finished lines to shell_submit(); the kernel idle
 * loop picks them up with shell_poll(). Commands therefore always run with
 * interrupts enabled and can safely block (e.g. 'exec' waiting on a task). */

#define SHELL_LINE_MAX 256
static char shell_pending_line[SHELL_LINE_MAX];
static volatile int shell_pending = 0;

void shell_submit(char *input) {
    if (shell_pending) return; /* previous command still queued; drop */
    int i = 0;
    while (input[i] != '\0' && i < SHELL_LINE_MAX - 1) {
        shell_pending_line[i] = input[i];
        i++;
    }
    shell_pending_line[i] = '\0';
    shell_pending = 1;
}

void shell_poll() {
    if (!shell_pending) return;
    char line[SHELL_LINE_MAX];
    int i = 0;
    while (shell_pending_line[i] != '\0') { line[i] = shell_pending_line[i]; i++; }
    line[i] = '\0';
    shell_pending = 0;
    shell_execute(line);
}

/* Returns 1 if 's' begins with 'prefix'. */
static int starts_with(char *s, const char *prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void print_dec(uint32_t n) {
    char buf[16];
    int_to_ascii((int) n, buf);
    kprint(buf);
}

static void print_hex(uint32_t n) {
    char buf[16] = "";
    hex_to_ascii((int) n, buf);
    kprint(buf);
}

/* ---- line editor -------------------------------------------------------- */
#define EDIT_MAX 256
static char edit_buf[EDIT_MAX];
static int edit_len = 0;
static int edit_caret = 0;

static void edit_redraw(void) {
    screen_redraw_input(edit_buf, edit_caret);
}

void shell_prompt() {
    kprint("> ");
    edit_len = 0;
    edit_caret = 0;
    edit_buf[0] = '\0';
    screen_mark_input_origin();
    screen_caret_show(1);
    edit_redraw();
}

void shell_input_char(char c) {
    if (c < 32 || c > 126) return;
    if (edit_len >= EDIT_MAX - 1) return;
    for (int i = edit_len; i > edit_caret; i--)
        edit_buf[i] = edit_buf[i - 1];
    edit_buf[edit_caret] = c;
    edit_len++;
    edit_caret++;
    edit_buf[edit_len] = '\0';
    edit_redraw();
}

void shell_input_backspace(void) {
    if (edit_caret <= 0) return;
    for (int i = edit_caret - 1; i < edit_len - 1; i++)
        edit_buf[i] = edit_buf[i + 1];
    edit_len--;
    edit_caret--;
    edit_buf[edit_len] = '\0';
    edit_redraw();
}

void shell_input_enter(void) {
    screen_caret_show(0);
    kprint("\n");
    shell_submit(edit_buf);
    edit_len = edit_caret = 0;
    edit_buf[0] = '\0';
}

void shell_input_left(void) {
    if (edit_caret > 0) {
        edit_caret--;
        edit_redraw();
    }
}

void shell_input_right(void) {
    if (edit_caret < edit_len) {
        edit_caret++;
        edit_redraw();
    }
}

void shell_input_home(void) {
    edit_caret = 0;
    edit_redraw();
}

void shell_input_end(void) {
    edit_caret = edit_len;
    edit_redraw();
}

void shell_input_click(int px, int py) {
    /* Mouse caret placement is FB-only; VGA uses arrow keys / Home / End. */
    if (!fb_available()) return;
    int col = 0;
    int r = fb_console_click(px, py, &col);
    if (r == 1) {
        if (col < 0) col = 0;
        if (col > edit_len) col = edit_len;
        edit_caret = col;
        edit_redraw();
    }
}

static void cmd_help() {
    kprint("Available commands:\n");
    kprint("  help        - show this message\n");
    kprint("  clear       - clear the screen\n");
    kprint("  echo X      - print X back\n");
    kprint("  mem         - run a heap alloc/free test and show stats\n");
    kprint("  ls          - list files on disk\n");
    kprint("  cat F       - print the contents of file F\n");
    kprint("  write F X   - write text X into file F\n");
    kprint("  rm F        - delete file F\n");
    kprint("  format      - erase the filesystem\n");
    kprint("  tasks       - list running tasks and activity counters\n");
    kprint("  exec F      - load and run ELF program F in its own address space\n");
    kprint("  frames      - show physical frame pool usage\n");
    kprint("  page        - request a page-aligned kmalloc\n");
    kprint("  fault       - trigger a page fault (will halt)\n");
    kprint("  fbdemo      - draw shapes on the framebuffer (if active)\n");
    kprint("  mouse       - print pointer x/y/buttons\n");
    kprint("  doze        - blank the display until a key/mouse wakes it\n");
    kprint("  reboot      - restart the machine\n");
    kprint("  shutdown    - power off the machine\n");
    kprint("  end         - halt the CPU\n");
    kprint("Arrows/Home/End move the text caret; click the input line to place it.\n");
    kprint("PgUp/PgDn (or click above the prompt) scroll console history.\n");
}

static const char *task_state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:    return "ready";
        case TASK_RUNNING:  return "running";
        case TASK_SLEEPING: return "sleeping";
        case TASK_DEAD:     return "dead";
        default:            return "unused";
    }
}

static void cmd_tasks() {
    /* Reap exited tasks before listing so finished/failed programs do not look
     * like stuck processes. Use task_reap (not yield) so we never nest schedule
     * with interrupts enabled. */
    task_reap();

    kprint("Live tasks: ");
    print_dec((uint32_t) task_count());
    kprint("\n");
    for (int i = 0; i < task_max(); i++) {
        task_state_t st = task_get_state(i);
        if (st == TASK_UNUSED || st == TASK_DEAD) continue;
        kprint("  task ");
        print_dec((uint32_t) i);
        if (i == 0) kprint(" (kernel/shell)");
        kprint(": ");
        kprint((char *) task_state_name(st));
        kprint(" counter=");
        print_dec(task_get_counter(i));
        kprint("\n");
    }
}

static void cmd_ls() {
    fs_fileinfo_t files[FS_MAX_FILES];
    int n = fs_list(files, FS_MAX_FILES);
    if (n == 0) {
        kprint("(no files)\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        kprint("  ");
        kprint(files[i].name);
        kprint("  (");
        print_dec(files[i].size);
        kprint(" bytes)\n");
    }
}

static void cmd_cat(char *name) {
    uint8_t *buf = (uint8_t *) malloc(FS_MAX_FILESIZE + 1);
    if (!buf) {
        kprint("cat: out of memory\n");
        return;
    }
    int r = fs_read_file(name, buf, FS_MAX_FILESIZE);
    if (r < 0) {
        kprint("cat: file not found: ");
        kprint(name);
        kprint("\n");
        free(buf);
        return;
    }
    buf[r] = '\0';
    kprint((char *) buf);
    kprint("\n");
    free(buf);
}

/* Splits "name rest..." into a name and the remaining text. */
static void cmd_write(char *args) {
    int i = 0;
    while (args[i] != '\0' && args[i] != ' ') i++;

    char name[FS_NAME_LEN];
    int nlen = i < FS_NAME_LEN - 1 ? i : FS_NAME_LEN - 1;
    for (int j = 0; j < nlen; j++) name[j] = args[j];
    name[nlen] = '\0';

    if (name[0] == '\0') {
        kprint("usage: write <file> <text>\n");
        return;
    }

    char *text = (args[i] == ' ') ? args + i + 1 : args + i; /* skip the space */
    int len = strlen(text);

    if (fs_write_file(name, (uint8_t *) text, (uint32_t) len) == 0) {
        kprint("Wrote ");
        print_dec((uint32_t) len);
        kprint(" bytes to ");
        kprint(name);
        kprint("\n");
    } else {
        kprint("write: failed (disk full or no disk?)\n");
    }
}

static void cmd_rm(char *name) {
    if (fs_delete(name) == 0) {
        kprint("Deleted ");
        kprint(name);
        kprint("\n");
    } else {
        kprint("rm: file not found: ");
        kprint(name);
        kprint("\n");
    }
}

static void cmd_mem() {
    uint32_t used, freeb, blocks;

    void *a = malloc(64);
    void *b = malloc(256);
    void *c = malloc(1024);

    kprint("Allocated a=0x"); print_hex((uint32_t) a);
    kprint(" b=0x"); print_hex((uint32_t) b);
    kprint(" c=0x"); print_hex((uint32_t) c);
    kprint("\n");

    free(b); /* free the middle block, then reuse it */
    void *d = malloc(128);
    kprint("Freed b, reallocated d=0x"); print_hex((uint32_t) d);
    kprint("\n");

    free(a); free(c); free(d);

    kheap_stats(&used, &freeb, &blocks);
    kprint("Heap: used="); print_dec(used);
    kprint(" free="); print_dec(freeb);
    kprint(" blocks="); print_dec(blocks);
    kprint("\n");
}

static void cmd_page() {
    uint32_t phys_addr;
    uint32_t page = kmalloc(1000, 1, &phys_addr);
    kprint("Page: 0x"); print_hex(page);
    kprint(", physical address: 0x"); print_hex(phys_addr);
    kprint("\n");
}

static void cmd_frames() {
    kprint("Frame pool: ");
    print_dec(frame_used());
    kprint(" / ");
    print_dec(frame_total());
    kprint(" frames used (4 KiB each)\n");
}

static void cmd_exec(char *name) {
    kprint("Loading ");
    kprint(name);
    kprint(" from disk...\n");
    int rc = elf_exec(name);
    if (rc == 0) {
        kprint("Program exited; back at the shell.\n");
    } else {
        kprint("exec failed (error ");
        print_dec((uint32_t) (-rc));
        kprint(")\n");
    }
}

static void cmd_fault() {
    /* Touch an address outside the identity-mapped region (16 MiB). */
    uint32_t *bad = (uint32_t *) 0x02000000;
    volatile uint32_t value = *bad;
    (void) value;
}

static void cmd_fbdemo() {
    if (!fb_available()) {
        kprint("Framebuffer not active (boot stayed in VGA text mode).\n");
        return;
    }
    int w = (int) fb_width();
    int h = (int) fb_height();
    /* Chaotic-ish demo blocks — preview of the future WM aesthetic. */
    fb_fill_rect(w / 8, h / 3, w / 3, h / 4, 0x00CC3355);
    fb_fill_rect(w / 3 + 20, h / 4 - 15, w / 4, h / 5, 0x0033AACC);
    fb_fill_rect(w / 2, h / 2, w / 5, h / 6, 0x00DDAA22);
    fb_draw_string(w / 8 + 8, h / 3 + 8, "summon: chaos", 0x00FFFFFF, 0x00CC3355);
    fb_draw_string(w / 3 + 28, h / 4 - 7, "misaligned", 0x00000000, 0x0033AACC);
    kprint("[fbdemo] drew demo rectangles on the LFB\n");
}

static void cmd_mouse() {
    if (!mouse_available()) {
        kprint("Mouse not available.\n");
        return;
    }
    mouse_state_t m;
    mouse_get(&m);
    kprint("mouse x=");
    print_dec((uint32_t) (m.x < 0 ? 0 : m.x));
    kprint(" y=");
    print_dec((uint32_t) (m.y < 0 ? 0 : m.y));
    kprint(" buttons=");
    print_dec(m.buttons);
    kprint(" (L=1 R=2 M=4)\n");
}

static void cmd_doze(void) {
    kprint("dozing (key or mouse to wake)...\n");
    idle_doze();
    kprint("awake.\n");
}

static void cmd_reboot() {
    kprint("Rebooting...\n");
    uint8_t status = 0x02;
    while (status & 0x02) status = port_byte_in(0x64); /* wait for input buffer */
    port_byte_out(0x64, 0xFE); /* pulse the CPU reset line */
    asm volatile("hlt");
}

void shell_execute(char *input) {
    /* Commands may have been reached after a path that left IF clear (e.g. a
     * task_exit that scheduled into us). Always run the shell with IRQs on. */
    asm volatile("sti");

    if (input[0] == '\0') {
        shell_prompt();
        return;
    }

    if (strcmp(input, "help") == 0) {
        cmd_help();
    } else if (strcmp(input, "clear") == 0) {
        clear_screen();
    } else if (starts_with(input, "echo ")) {
        kprint(input + 5);
        kprint("\n");
    } else if (strcmp(input, "echo") == 0) {
        kprint("\n");
    } else if (strcmp(input, "mem") == 0) {
        cmd_mem();
    } else if (strcmp(input, "ls") == 0) {
        cmd_ls();
    } else if (starts_with(input, "cat ")) {
        cmd_cat(input + 4);
    } else if (starts_with(input, "write ")) {
        cmd_write(input + 6);
    } else if (starts_with(input, "rm ")) {
        cmd_rm(input + 3);
    } else if (strcmp(input, "format") == 0) {
        fs_format();
        kprint("Filesystem formatted.\n");
    } else if (strcmp(input, "tasks") == 0) {
        cmd_tasks();
    } else if (starts_with(input, "exec ")) {
        cmd_exec(input + 5);
    } else if (strcmp(input, "frames") == 0) {
        cmd_frames();
    } else if (strcmp(input, "shutdown") == 0) {
        kprint("Powering off...\n");
        /* After exec, leftover user tasks / CR3 made ACPI+hlt look like a crash. */
        task_prepare_poweroff();
        power_off();
    } else if (strcmp(input, "page") == 0) {
        cmd_page();
    } else if (strcmp(input, "fault") == 0) {
        cmd_fault();
    } else if (strcmp(input, "fbdemo") == 0) {
        cmd_fbdemo();
    } else if (strcmp(input, "mouse") == 0) {
        cmd_mouse();
    } else if (strcmp(input, "doze") == 0) {
        cmd_doze();
    } else if (strcmp(input, "reboot") == 0) {
        cmd_reboot();
    } else if (strcmp(input, "end") == 0) {
        kprint("Stopping the CPU. Bye!\n");
        asm volatile("hlt");
    } else {
        kprint("Unknown command: ");
        kprint(input);
        kprint("\nType 'help' for a list of commands.\n");
    }

    shell_prompt();
}
