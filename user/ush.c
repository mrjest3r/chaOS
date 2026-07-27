#include "ulib.h"

/* ush - the chaOS user-space shell.
 *
 * The whole shell is an ordinary ring-3 program loaded from disk. It reads
 * commands with SYS_READLINE and starts other programs with SYS_SPAWN +
 * SYS_WAIT, so a program crash (like crash.elf) kills that program only;
 * ush itself just keeps prompting. Start it from the kernel shell with
 * 'exec ush.elf'; 'exit' returns you to the kernel shell. */

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *prefix) {
    int i = 0;
    while (prefix[i]) {
        if (s[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void cmd_help(void) {
    uprint("ush commands:\n");
    uprint("  help       - this message\n");
    uprint("  ls         - list files on disk\n");
    uprint("  run FILE   - run an ELF program and wait for it\n");
    uprint("  bg FILE    - run an ELF program in the background\n");
    uprint("  pid        - print my task id\n");
    uprint("  uptime     - print timer ticks since boot\n");
    uprint("  exit       - leave ush (back to the kernel shell)\n");
}

static void cmd_ls(void) {
    ufileinfo_t files[32];
    int n = ulistfiles(files, 32);
    if (n <= 0) {
        uprint("(no files)\n");
        return;
    }
    for (int i = 0; i < n; i++) {
        uprint("  ");
        uprint(files[i].name);
        uprint("  (");
        uprint_dec(files[i].size);
        uprint(" bytes)\n");
    }
}

static void cmd_run(const char *path) {
    int rc = urun(path);
    if (rc < 0) {
        uprint("ush: could not run ");
        uprint(path);
        uprint("\n");
    }
}

static void cmd_bg(const char *path) {
    int pid = uspawn(path);
    if (pid < 0) {
        uprint("ush: could not run ");
        uprint(path);
        uprint("\n");
    } else {
        uprint("[");
        uprint_dec((uint32_t) pid);
        uprint("] running in background\n");
    }
}

int main(void) {
    uprint("ush - chaOS user shell (pid ");
    uprint_dec((uint32_t) ugetpid());
    uprint("). Type 'help'.\n");

    char line[128];
    for (;;) {
        uprint("ush> ");
        ureadline(line, (int) sizeof(line));

        if (line[0] == '\0')          continue;
        else if (streq(line, "exit")) break;
        else if (streq(line, "help")) cmd_help();
        else if (streq(line, "ls"))   cmd_ls();
        else if (streq(line, "pid")) {
            uprint_dec((uint32_t) ugetpid());
            uprint("\n");
        }
        else if (streq(line, "uptime")) {
            uprint_dec(uuptime());
            uprint(" ticks\n");
        }
        else if (starts_with(line, "run ")) cmd_run(line + 4);
        else if (starts_with(line, "bg "))  cmd_bg(line + 3);
        else {
            uprint("ush: unknown command: ");
            uprint(line);
            uprint("\n");
        }
    }

    uprint("[ush] bye\n");
    return 0;
}
