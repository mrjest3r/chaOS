#include "ulib.h"

/* scribe — onboard line editor (roadmap P1.6).
 * Usage:  summon scribe.elf notes.txt
 *         scribe notes.txt          (from ush)
 *
 * At the : prompt:
 *   p        print buffer
 *   a        append lines (end with a lone .)
 *   e N TEXT replace line N
 *   d N      delete line N
 *   w        write/save
 *   q        quit (asks if dirty)
 *   wq       write and quit
 *   h        help
 */

#define MAX_LINES   40
#define LINE_LEN    72
#define BUF_MAX     (MAX_LINES * (LINE_LEN + 1))
#define NAME_MAX    24

static char lines[MAX_LINES][LINE_LEN];
static int  nlines;
static int  dirty;
static char fname[NAME_MAX];

static void cpy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && src[i] != ' ' && i < NAME_MAX - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void set_line(int idx, const char *s) {
    int i = 0;
    while (s[i] && i < LINE_LEN - 1) {
        lines[idx][i] = s[i];
        i++;
    }
    lines[idx][i] = '\0';
}

static void clear_buf(void) {
    nlines = 0;
    dirty = 0;
}

static int load_file(void) {
    char raw[BUF_MAX];
    int n = ureadfile(fname, raw, (int) sizeof(raw) - 1);
    clear_buf();
    if (n < 0) {
        /* New file. */
        return 0;
    }
    raw[n] = '\0';
    int i = 0;
    while (i <= n && nlines < MAX_LINES) {
        int j = 0;
        char tmp[LINE_LEN];
        while (i < n && raw[i] != '\n' && j < LINE_LEN - 1)
            tmp[j++] = raw[i++];
        tmp[j] = '\0';
        if (i < n && raw[i] == '\n') i++;
        set_line(nlines++, tmp);
        if (i >= n) break;
    }
    dirty = 0;
    return 0;
}

static int save_file(void) {
    char raw[BUF_MAX];
    int pos = 0;
    for (int i = 0; i < nlines; i++) {
        int k = 0;
        while (lines[i][k] && pos < BUF_MAX - 2)
            raw[pos++] = lines[i][k++];
        if (pos < BUF_MAX - 1)
            raw[pos++] = '\n';
    }
    if (uwritefile(fname, raw, pos) < 0) {
        uprint("scribe: write failed\n");
        return -1;
    }
    dirty = 0;
    uprint("wrote ");
    uprint(fname);
    uprint(" (");
    uprint_dec((uint32_t) pos);
    uprint(" bytes)\n");
    return 0;
}

static void print_buf(void) {
    if (nlines == 0) {
        uprint("(empty)\n");
        return;
    }
    for (int i = 0; i < nlines; i++) {
        uprint_dec((uint32_t) (i + 1));
        uprint("| ");
        uprint(lines[i]);
        uprint("\n");
    }
}

static void cmd_help(void) {
    uprint("scribe commands (line numbers start at 1):\n");
    uprint("  p          print buffer\n");
    uprint("  a          append lines (end with a lone .)\n");
    uprint("  e N TEXT   replace line N with TEXT\n");
    uprint("  d N        delete line N\n");
    uprint("  w          write/save file\n");
    uprint("  wq         write and quit\n");
    uprint("  q          quit (warns if unsaved)\n");
    uprint("  q!         quit discard changes\n");
    uprint("  h          this help\n");
}

static void cmd_append(void) {
    uprint("(append; '.' alone to end)\n");
    for (;;) {
        if (nlines >= MAX_LINES) {
            uprint("scribe: buffer full\n");
            break;
        }
        char line[LINE_LEN];
        ureadline(line, (int) sizeof(line));
        if (line[0] == '.' && line[1] == '\0') break;
        set_line(nlines++, line);
        dirty = 1;
    }
}

static void cmd_edit(const char *args) {
    while (*args == ' ') args++;
    int n = uatoi(args);
    if (n < 1 || n > nlines) {
        uprint("scribe: bad line\n");
        return;
    }
    while (*args && *args != ' ') args++;
    while (*args == ' ') args++;
    if (!*args) {
        uprint("scribe: e N TEXT\n");
        return;
    }
    set_line(n - 1, args);
    dirty = 1;
}

static void cmd_delete(const char *args) {
    while (*args == ' ') args++;
    int n = uatoi(args);
    if (n < 1 || n > nlines) {
        uprint("scribe: bad line\n");
        return;
    }
    for (int i = n - 1; i < nlines - 1; i++)
        set_line(i, lines[i + 1]);
    nlines--;
    dirty = 1;
}

static int starts_with(const char *s, const char *p) {
    int i = 0;
    while (p[i]) {
        if (s[i] != p[i]) return 0;
        i++;
    }
    return 1;
}

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

int main(int argc, char **argv) {
    if (argc < 2) {
        uprint("usage: scribe.elf FILE\n");
        return 1;
    }
    cpy_name(fname, argv[1]);
    if (!fname[0]) {
        uprint("usage: scribe.elf FILE\n");
        return 1;
    }

    load_file();
    uprint("scribe ");
    uprint(fname);
    uprint(" (");
    uprint_dec((uint32_t) nlines);
    uprint(" lines). Type h for help.\n");
    print_buf();

    char cmd[LINE_LEN];
    for (;;) {
        uprint(": ");
        ureadline(cmd, (int) sizeof(cmd));
        if (cmd[0] == '\0') continue;
        else if (streq(cmd, "h") || streq(cmd, "help")) cmd_help();
        else if (streq(cmd, "p")) print_buf();
        else if (streq(cmd, "a")) cmd_append();
        else if (streq(cmd, "w")) save_file();
        else if (streq(cmd, "wq")) {
            if (save_file() == 0) return 0;
        }
        else if (streq(cmd, "q")) {
            if (dirty) {
                uprint("dirty - save with w, or q! to discard\n");
                continue;
            }
            return 0;
        }
        else if (streq(cmd, "q!")) return 0;
        else if (starts_with(cmd, "e ")) cmd_edit(cmd + 2);
        else if (starts_with(cmd, "d ")) cmd_delete(cmd + 2);
        else {
            uprint("scribe: unknown (h for help)\n");
        }
    }
}
