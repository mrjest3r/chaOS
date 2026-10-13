#include "ulib.h"

/* folio — files + notes browser (roadmap P4.1).
 * Browse the disk, open notes in scribe, create / scrap files — all on-deck.
 *
 * Commands at folio>:
 *   roster / r     list files
 *   peek NAME      print file
 *   open NAME      edit with scribe.elf
 *   note NAME      create empty note (if new) then open in scribe
 *   scrap NAME     delete file
 *   lexicon / h    help
 *   banish / q     quit
 */

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int starts_with(const char *s, const char *p) {
    int i = 0;
    while (p[i]) {
        if (s[i] != p[i]) return 0;
        i++;
    }
    return 1;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static void copy_name(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && src[i] != ' ' && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void cmd_help(void) {
    uprint("folio commands:\n");
    uprint("  roster     list files on disk (also: r, ls)\n");
    uprint("  peek NAME  show file contents (also: type NAME)\n");
    uprint("  open NAME  edit file in scribe\n");
    uprint("  note NAME  create note if needed, then scribe\n");
    uprint("  scrap NAME delete file\n");
    uprint("  banish     leave folio (also: q, quit)\n");
    uprint("  lexicon    this help (also: h, help)\n");
}

static void cmd_roster(void) {
    ufileinfo_t *files = (ufileinfo_t *) umalloc(UFS_MAX_FILES * (uint32_t) sizeof(ufileinfo_t));
    if (!files) {
        uprint("roster: out of memory\n");
        return;
    }
    int n = ulistfiles(files, UFS_MAX_FILES);
    if (n <= 0) {
        uprint("(empty disk)\n");
        ufree(files);
        return;
    }
    for (int i = 0; i < n; i++) {
        uprint("  ");
        uprint(files[i].name);
        uprint("  (");
        uprint_dec(files[i].size);
        uprint(" bytes)\n");
    }
    ufree(files);
}

static void cmd_peek(const char *path) {
    path = skip_ws(path);
    char buf[256];
    int n = ureadfile(path, buf, (int) sizeof(buf) - 1);
    if (n < 0) {
        uprint("folio: cannot peek ");
        uprint(path);
        uprint("\n");
        return;
    }
    buf[n] = '\0';
    uprint(buf);
    if (n == 0 || buf[n - 1] != '\n') uprint("\n");
}

static void run_scribe(const char *name) {
    char cmd[64];
    const char *pfx = "bin/scribe.elf ";
    int i = 0;
    while (pfx[i] && i < 63) {
        cmd[i] = pfx[i];
        i++;
    }
    int j = 0;
    while (name[j] && name[j] != ' ' && i < 63)
        cmd[i++] = name[j++];
    cmd[i] = '\0';
    if (urun(cmd) < 0)
        uprint("folio: scribe failed\n");
}

static void cmd_open(const char *path) {
    path = skip_ws(path);
    char name[24];
    copy_name(name, path, 24);
    if (!name[0]) {
        uprint("folio: open NAME\n");
        return;
    }
    run_scribe(name);
}

static void cmd_note(const char *path) {
    path = skip_ws(path);
    char name[24];
    copy_name(name, path, 24);
    if (!name[0]) {
        uprint("folio: note NAME\n");
        return;
    }
    /* Create empty file if missing (write of 0 bytes may fail — use "\n"). */
    char probe[4];
    if (ureadfile(name, probe, 1) < 0) {
        if (uwritefile(name, "\n", 1) < 0) {
            uprint("folio: cannot create ");
            uprint(name);
            uprint("\n");
            return;
        }
        uprint("created ");
        uprint(name);
        uprint("\n");
    }
    run_scribe(name);
}

static void cmd_scrap(const char *path) {
    path = skip_ws(path);
    char name[24];
    copy_name(name, path, 24);
    if (!name[0]) {
        uprint("folio: scrap NAME\n");
        return;
    }
    if (udelete(name) < 0) {
        uprint("folio: cannot scrap ");
        uprint(name);
        uprint("\n");
    } else {
        uprint("scrapped ");
        uprint(name);
        uprint("\n");
    }
}

int main(void) {
    uprint("folio - type 'lexicon'. Browse/edit notes without the host.\n");
    char line[128];
    for (;;) {
        uprint("folio> ");
        ureadline(line, (int) sizeof(line));
        if (line[0] == '\0') continue;
        else if (streq(line, "banish") || streq(line, "q") || streq(line, "quit"))
            break;
        else if (streq(line, "lexicon") || streq(line, "h") || streq(line, "help"))
            cmd_help();
        else if (streq(line, "roster") || streq(line, "r") || streq(line, "ls"))
            cmd_roster();
        else if (starts_with(line, "peek "))  cmd_peek(line + 5);
        else if (starts_with(line, "type "))  cmd_peek(line + 5);
        else if (starts_with(line, "open "))  cmd_open(line + 5);
        else if (starts_with(line, "note "))  cmd_note(line + 5);
        else if (starts_with(line, "scrap ")) cmd_scrap(line + 6);
        else {
            uprint("folio: unknown - type lexicon\n");
        }
    }
    return 0;
}
