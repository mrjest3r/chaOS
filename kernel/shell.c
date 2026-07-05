#include "shell.h"
#include "kheap.h"
#include "fs.h"
#include "task.h"
#include "user_demo.h"
#include "elf.h"
#include "../drivers/screen.h"
#include "../libc/string.h"
#include "../libc/mem.h"
#include "../cpu/ports.h"
#include "../cpu/power.h"
#include <stdint.h>

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

void shell_prompt() {
    kprint("> ");
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
    kprint("  user        - drop to ring 3 and run a syscall demo\n");
    kprint("  exec F      - load and run ELF program F from disk\n");
    kprint("  page        - request a page-aligned kmalloc\n");
    kprint("  fault       - trigger a page fault (will halt)\n");
    kprint("  reboot      - restart the machine\n");
    kprint("  shutdown    - power off the machine\n");
    kprint("  end         - halt the CPU\n");
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
    kprint("Live tasks: ");
    print_dec((uint32_t) task_count());
    kprint("\n");
    for (int i = 0; i < task_max(); i++) {
        task_state_t st = task_get_state(i);
        if (st == TASK_UNUSED) continue;
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

static void cmd_reboot() {
    kprint("Rebooting...\n");
    uint8_t status = 0x02;
    while (status & 0x02) status = port_byte_in(0x64); /* wait for input buffer */
    port_byte_out(0x64, 0xFE); /* pulse the CPU reset line */
    asm volatile("hlt");
}

void shell_execute(char *input) {
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
    } else if (strcmp(input, "user") == 0) {
        kprint("Launching a ring-3 user task...\n");
        usermode_run();
        kprint("User task finished; back at the shell.\n");
    } else if (starts_with(input, "exec ")) {
        cmd_exec(input + 5);
    } else if (strcmp(input, "shutdown") == 0) {
        kprint("Powering off...\n");
        power_off();
    } else if (strcmp(input, "page") == 0) {
        cmd_page();
    } else if (strcmp(input, "fault") == 0) {
        cmd_fault();
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
