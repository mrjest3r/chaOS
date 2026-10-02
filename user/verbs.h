#ifndef VERBS_H
#define VERBS_H

/* Deck command names for terminal.elf.
 *
 * The shell matches ONLY the `verb` column. `id` is the internal command.
 * Typing an internal or classic name (ls, cd, rm, exit, sudo, …) is rejected.
 * To rename a command, change `verb` and leave `id` alone.
 *
 * admin: refused until the session runs `ascend`.
 * live:  0 means the verb is recognized and answers "not in this OS yet".
 * A NULL verb is an internal command with no deck name yet.
 *
 * "ascend" checks the logged-in user's password. "empower" is the
 * package-upgrade stub.
 */

typedef enum {
    VID_NONE = 0,
    VID_LEXICON,
    VID_BANISH,
    VID_SUMMON,
    VID_REVEAL,
    VID_WANDER,
    VID_ABODE,
    VID_FORGE,
    VID_SCRIBE,
    VID_GLANCE,
    VID_DUPLICATE,
    VID_RELOCATE,
    VID_ERASE,
    VID_SEEK,
    VID_DISCERN,
    VID_INSPECT,
    VID_OBSERVE,
    VID_OVERSEE,
    VID_SLAY,
    VID_OBLITERATE,
    VID_FREEZE,
    VID_AWAKEN,
    VID_AWAIT,
    VID_PROBE,
    VID_RESOLVE,
    VID_FETCH,
    VID_VOYAGE,
    VID_TRANSPORT,
    VID_REBIRTH,
    VID_SLUMBER,
    VID_EXTINGUISH,
    VID_AGE,
    VID_MOMENT,
    VID_ESSENCE,
    VID_RESERVES,
    VID_PROVISIONS,
    VID_OMENS,
    VID_ACQUIRE,
    VID_RELEASE,
    VID_REFRESH,
    VID_PKG_UPGRADE, /* empower: upgrade packages. not built yet. */
    VID_POSSESSIONS,
    VID_PURGE,
    VID_EXPEL,
    VID_CONCEAL,
    VID_UNVEIL,
    VID_MANIFEST,
    VID_EXILE,
    VID_KEY,
    VID_ASSUME,
    VID_ASCEND
} verb_id;

typedef struct {
    const char *verb;
    verb_id id;
    int admin;
    int live;
    const char *blurb;
} verb_t;

static const verb_t verbs[] = {
    { "lexicon",     VID_LEXICON,     0, 1, "list commands" },
    { "banish",      VID_BANISH,      0, 1, "exit shell" },
    { "summon",      VID_SUMMON,      0, 1, "execute a program" },

    { "reveal",      VID_REVEAL,      0, 1, "list directory contents" },
    { "wander",      VID_WANDER,      0, 1, "change directory" },
    { "abode",       VID_ABODE,       0, 1, "show current directory" },

    { "forge",       VID_FORGE,       0, 1, "create directory" },
    { "scribe",      VID_SCRIBE,      0, 1, "edit or create a file" },
    { "glance",      VID_GLANCE,      0, 1, "read file" },
    { "duplicate",   VID_DUPLICATE,   0, 1, "copy" },
    { "relocate",    VID_RELOCATE,    0, 1, "move/rename" },
    { "erase",       VID_ERASE,       0, 1, "delete" },
    { "seek",        VID_SEEK,        0, 1, "search" },
    { "discern",     VID_DISCERN,     0, 1, "identify file" },
    { "inspect",     VID_INSPECT,     0, 1, "show metadata" },

    { "observe",     VID_OBSERVE,     0, 1, "list processes" },
    { "oversee",     VID_OVERSEE,     0, 1, "process monitor" },
    { "slay",        VID_SLAY,        1, 1, "kill process" },
    { "obliterate",  VID_OBLITERATE,  1, 1, "kill process and its children" },
    { "freeze",      VID_FREEZE,      1, 1, "suspend process" },
    { "awaken",      VID_AWAKEN,      1, 1, "resume process" },
    { "await",       VID_AWAIT,       0, 1, "wait" },

    { "probe",       VID_PROBE,       0, 0, "ping" },
    { "resolve",     VID_RESOLVE,     0, 0, "DNS lookup" },
    { "fetch",       VID_FETCH,       0, 0, "HTTP request" },
    { "voyage",      VID_VOYAGE,      0, 0, "SSH" },
    { "transport",   VID_TRANSPORT,   0, 0, "secure copy" },

    { "rebirth",     VID_REBIRTH,     1, 1, "reboot" },
    { "slumber",     VID_SLUMBER,     1, 1, "shut down" },
    { "extinguish",  VID_EXTINGUISH,  1, 1, "power off" },
    { "age",         VID_AGE,         0, 1, "uptime" },
    { "moment",      VID_MOMENT,      0, 1, "date and time" },
    { "essence",     VID_ESSENCE,     0, 1, "system information" },
    { "reserves",    VID_RESERVES,    0, 0, "memory information" },
    { "provisions",  VID_PROVISIONS,  0, 1, "disk information" },
    { "omens",       VID_OMENS,       0, 0, "kernel messages" },

    { "acquire",     VID_ACQUIRE,     1, 0, "install package" },
    { "release",     VID_RELEASE,     1, 0, "remove package" },
    { "refresh",     VID_REFRESH,     1, 0, "update package lists" },
    { "empower",     VID_PKG_UPGRADE, 1, 0, "upgrade packages" },
    { "possessions", VID_POSSESSIONS, 0, 0, "installed packages" },
    { "purge",       VID_PURGE,       0, 1, "clear the terminal" },
    { "expel",       VID_EXPEL,       1, 0, "clean cache" },
    { "conceal",     VID_CONCEAL,     0, 1, "hide file" },
    { "unveil",      VID_UNVEIL,      0, 1, "show hidden files" },

    { "manifest",    VID_MANIFEST,    1, 1, "create a user" },
    { "exile",       VID_EXILE,       1, 1, "delete a user" },
    { "key",         VID_KEY,         1, 1, "set or change a password" },
    { "assume",      VID_ASSUME,      1, 1, "change to a user" },
    { "ascend",      VID_ASCEND,      0, 1, "gain admin" },
};

static int verb_count(void) {
    return (int) (sizeof(verbs) / sizeof(verbs[0]));
}

/* Match the first word of `line` against `verb` only. `*args` is the rest. */
static const verb_t *verb_lookup(const char *line, const char **args) {
    int n = 0;
    while (line[n] && line[n] != ' ') n++;
    for (int i = 0; i < verb_count(); i++) {
        if (!verbs[i].verb) continue;
        int m = 0;
        while (verbs[i].verb[m]) m++;
        if (m != n) continue;
        int same = 1;
        for (int k = 0; k < n; k++) {
            if (line[k] != verbs[i].verb[k]) same = 0;
        }
        if (!same) continue;
        const char *rest = line + n;
        while (*rest == ' ') rest++;
        *args = rest;
        return &verbs[i];
    }
    return 0;
}

#endif
