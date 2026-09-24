#include "ulib.h"

/* ush — chaOS user shell with personal command lexicon (docs/agent/LEXICON.md).
 * P4.2: deck modes (attune) switch prompt + allowed verb sets. */

enum {
    MODE_DECK = 0,
    MODE_NOTES = 1,
    MODE_CODE = 2
};

static int mode = MODE_DECK;

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

static const char *skip_ws(const char *s) {
    while (*s == ' ') s++;
    return s;
}

static const char *mode_name(void) {
    if (mode == MODE_NOTES) return "notes";
    if (mode == MODE_CODE) return "code";
    return "deck";
}

/* Which verbs are legal in the current mode (banish/attune/lexicon always ok). */
static int verb_allowed(const char *line) {
    if (streq(line, "banish") || streq(line, "exit")) return 1;
    if (streq(line, "lexicon") || streq(line, "help")) return 1;
    if (starts_with(line, "attune ") || streq(line, "attune") ||
        starts_with(line, "mode ")) return 1;
    if (streq(line, "who") || streq(line, "pid")) return 1;
    if (streq(line, "pulse") || streq(line, "uptime")) return 1;
    if (streq(line, "slumber") || streq(line, "doze")) return 1;

    if (mode == MODE_DECK) return 1;

    if (mode == MODE_NOTES) {
        if (streq(line, "roster") || streq(line, "ls")) return 1;
        if (streq(line, "folio")) return 1;
        if (starts_with(line, "note ") || starts_with(line, "scrap ") ||
            starts_with(line, "scribe ") || starts_with(line, "peek ") ||
            starts_with(line, "stash ")) return 1;
        return 0;
    }

    /* MODE_CODE — forge / run pipeline */
    if (streq(line, "roster") || streq(line, "ls")) return 1;
    if (starts_with(line, "summon ") || starts_with(line, "ignite ") ||
        starts_with(line, "run ") || starts_with(line, "loose ") ||
        starts_with(line, "bg ") || starts_with(line, "scribe ") ||
        starts_with(line, "peek ") || starts_with(line, "stash ")) return 1;
    return 0;
}

static void print_mode_banner(void) {
    uprint("[attune] ");
    uprint(mode_name());
    uprint(" - ");
    if (mode == MODE_NOTES) {
        uprint("notes layout: folio, note, scrap, scribe, peek, stash\n");
    } else if (mode == MODE_CODE) {
        uprint("code layout: summon/ignite/loose, scribe, peek, stash\n");
    } else {
        uprint("full deck: all verbs\n");
    }
}

static void cmd_lexicon(void) {
    uprint("chaOS lexicon (mode=");
    uprint(mode_name());
    uprint(") - docs/agent/LEXICON.md\n");
    uprint("  attune MODE  - deck | notes | code  (alias: mode)\n");
    if (mode == MODE_DECK || mode == MODE_NOTES) {
        uprint("  roster       - list files\n");
        uprint("  folio        - files + notes browser\n");
        uprint("  note FILE    - create/open note\n");
        uprint("  scrap FILE   - delete file\n");
        uprint("  scribe FILE  - edit file\n");
        uprint("  peek / stash - read / write text\n");
    }
    if (mode == MODE_DECK || mode == MODE_CODE) {
        uprint("  summon FILE  - run ELF (wait)\n");
        uprint("  ignite FILE  - run ELF (wait)\n");
        uprint("  loose FILE   - run ELF (background)\n");
        if (mode == MODE_CODE) {
            uprint("  scribe/peek/stash - source + artifacts\n");
            uprint("  roster       - list files\n");
        }
    }
    if (mode == MODE_DECK) {
        uprint("  (deck has every verb above)\n");
    }
    uprint("  pulse / who  - uptime / pid\n");
    uprint("  slumber      - blank display until key/mouse (alias: doze)\n");
    uprint("  banish       - leave ush\n");
}

static void cmd_attune(const char *args) {
    args = skip_ws(args);
    if (!args[0] || streq(args, "?")) {
        uprint("modes: deck, notes, code  (now=");
        uprint(mode_name());
        uprint(")\n");
        return;
    }
    if (streq(args, "deck") || streq(args, "full")) {
        mode = MODE_DECK;
    } else if (streq(args, "notes") || streq(args, "note")) {
        mode = MODE_NOTES;
    } else if (streq(args, "code") || streq(args, "forge")) {
        mode = MODE_CODE;
    } else {
        uprint("ush: unknown mode (deck|notes|code)\n");
        return;
    }
    print_mode_banner();
}

static void cmd_scribe(const char *path) {
    path = skip_ws(path);
    if (!path[0]) {
        uprint("ush: scribe FILE\n");
        return;
    }
    char cmd[64];
    const char *pfx = "scribe.elf ";
    int i = 0;
    while (pfx[i] && i < 63) {
        cmd[i] = pfx[i];
        i++;
    }
    int j = 0;
    while (path[j] && path[j] != ' ' && i < 63) {
        cmd[i++] = path[j++];
    }
    cmd[i] = '\0';
    int rc = urun(cmd);
    if (rc < 0) {
        uprint("ush: could not scribe ");
        uprint(path);
        uprint("\n");
    }
}

static void cmd_roster(void) {
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

static void cmd_summon(const char *path) {
    path = skip_ws(path);
    int rc = urun(path);
    if (rc < 0) {
        uprint("ush: could not summon ");
        uprint(path);
        uprint("\n");
    }
}

static void cmd_loose(const char *path) {
    path = skip_ws(path);
    int pid = uspawn(path);
    if (pid < 0) {
        uprint("ush: could not loose ");
        uprint(path);
        uprint("\n");
    } else {
        uprint("[");
        uprint_dec((uint32_t) pid);
        uprint("] loose in background\n");
    }
}

static void cmd_peek(const char *path) {
    path = skip_ws(path);
    char buf[256];
    int n = ureadfile(path, buf, (int) sizeof(buf) - 1);
    if (n < 0) {
        uprint("ush: cannot peek ");
        uprint(path);
        uprint("\n");
        return;
    }
    buf[n] = '\0';
    uprint(buf);
    if (n == 0 || buf[n - 1] != '\n') uprint("\n");
}

static void cmd_stash(const char *args) {
    args = skip_ws(args);
    char name[24];
    int i = 0;
    while (args[i] && args[i] != ' ' && i < 23) {
        name[i] = args[i];
        i++;
    }
    name[i] = '\0';
    if (!name[0]) {
        uprint("ush: stash FILE TEXT\n");
        return;
    }
    const char *text = skip_ws(args + i);
    int len = 0;
    while (text[len]) len++;
    if (uwritefile(name, text, len) < 0)
        uprint("ush: stash failed\n");
    else
        uprint("stashed.\n");
}

static void cmd_folio(void) {
    if (urun("folio.elf") < 0)
        uprint("ush: could not summon folio.elf\n");
}

static void cmd_note(const char *path) {
    path = skip_ws(path);
    if (!path[0]) {
        uprint("ush: note FILE\n");
        return;
    }
    char name[24];
    int i = 0;
    while (path[i] && path[i] != ' ' && i < 23) {
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    char probe[2];
    if (ureadfile(name, probe, 1) < 0) {
        if (uwritefile(name, "\n", 1) < 0) {
            uprint("ush: cannot create note\n");
            return;
        }
    }
    cmd_scribe(name);
}

static void cmd_scrap(const char *path) {
    path = skip_ws(path);
    if (!path[0]) {
        uprint("ush: scrap FILE\n");
        return;
    }
    char name[24];
    int i = 0;
    while (path[i] && path[i] != ' ' && i < 23) {
        name[i] = path[i];
        i++;
    }
    name[i] = '\0';
    if (udelete(name) < 0) {
        uprint("ush: cannot scrap ");
        uprint(name);
        uprint("\n");
    } else {
        uprint("scrapped.\n");
    }
}

static void prompt(void) {
    if (mode == MODE_DECK) {
        uprint("ush> ");
    } else {
        uprint("ush[");
        uprint(mode_name());
        uprint("]> ");
    }
}

int main(void) {
    uprint("ush - chaOS lexicon shell (pid ");
    uprint_dec((uint32_t) ugetpid());
    uprint("). Type 'lexicon' or 'attune'.\n");

    char line[128];
    for (;;) {
        prompt();
        ureadline(line, (int) sizeof(line));

        if (line[0] == '\0') continue;

        if (!verb_allowed(line)) {
            uprint("ush: '");
            /* print first token only */
            int i = 0;
            while (line[i] && line[i] != ' ') {
                char t[2] = { line[i], 0 };
                uprint(t);
                i++;
            }
            uprint("' not in ");
            uprint(mode_name());
            uprint(" mode - attune deck|notes|code\n");
            continue;
        }

        if (streq(line, "banish") || streq(line, "exit")) break;
        else if (streq(line, "lexicon") || streq(line, "help")) cmd_lexicon();
        else if (streq(line, "attune") || streq(line, "mode")) cmd_attune("");
        else if (starts_with(line, "attune ")) cmd_attune(line + 7);
        else if (starts_with(line, "mode "))   cmd_attune(line + 5);
        else if (streq(line, "roster") || streq(line, "ls")) cmd_roster();
        else if (streq(line, "who") || streq(line, "pid")) {
            uprint_dec((uint32_t) ugetpid());
            uprint("\n");
        }
        else if (streq(line, "pulse") || streq(line, "uptime")) {
            uprint_dec(uuptime());
            uprint(" ticks\n");
        }
        else if (streq(line, "slumber") || streq(line, "doze")) {
            uprint("slumber (key or mouse to wake)...\n");
            udoze();
            uprint("awake.\n");
        }
        else if (starts_with(line, "summon ")) cmd_summon(line + 7);
        else if (starts_with(line, "ignite ")) cmd_summon(line + 7);
        else if (starts_with(line, "run "))    cmd_summon(line + 4);
        else if (starts_with(line, "loose "))  cmd_loose(line + 6);
        else if (starts_with(line, "bg "))     cmd_loose(line + 3);
        else if (streq(line, "folio"))         cmd_folio();
        else if (starts_with(line, "note "))   cmd_note(line + 5);
        else if (starts_with(line, "scrap "))  cmd_scrap(line + 6);
        else if (starts_with(line, "scribe ")) cmd_scribe(line + 7);
        else if (starts_with(line, "peek "))   cmd_peek(line + 5);
        else if (starts_with(line, "stash "))  cmd_stash(line + 6);
        else {
            uprint("ush: unknown verb: ");
            uprint(line);
            uprint("\nType 'lexicon' for verbs.\n");
        }
    }
    return 0;
}
