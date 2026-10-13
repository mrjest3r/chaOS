#ifndef ATTUNE_H
#define ATTUNE_H

/* Shared deck mode (G6). The file "attune" is "deck", "notes", or "code"
 * plus a newline. chaoswm reads and writes it — no new syscall. */

#define ATTUNE_FILE "sys/attune"

enum {
    ATTUNE_DECK = 0,
    ATTUNE_NOTES = 1,
    ATTUNE_CODE = 2
};

static const char *attune_name(int m) {
    if (m == ATTUNE_NOTES) return "notes";
    if (m == ATTUNE_CODE) return "code";
    return "deck";
}

static int attune_parse(const char *s, int n) {
    int i = 0;
    while (i < n && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r')) i++;
    if (i < n && s[i] == 'n') return ATTUNE_NOTES;
    if (i < n && s[i] == 'c') return ATTUNE_CODE;
    return ATTUNE_DECK;
}

static int attune_load(void) {
    char buf[16];
    int n = ureadfile(ATTUNE_FILE, buf, 15);
    if (n <= 0) return ATTUNE_DECK;
    buf[n < 15 ? n : 15] = '\0';
    return attune_parse(buf, n);
}

static void attune_save(int m) {
    const char *n = attune_name(m);
    char buf[8];
    int i = 0;
    while (n[i] && i < 6) {
        buf[i] = n[i];
        i++;
    }
    buf[i++] = '\n';
    uwritefile(ATTUNE_FILE, buf, i);
}

#endif
