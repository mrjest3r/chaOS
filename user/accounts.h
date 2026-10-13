#ifndef ACCOUNTS_H
#define ACCOUNTS_H

/* Shared account file for gate.elf and terminal.elf.
 * "accounts" holds name + verifier, one user per line: "name HASH".
 * HASH is 8 hex digits of a hash of the name and the password.
 * "session" is the logged-in name plus a newline.
 * The kernel does not enforce this file. terminal treats both names
 * as system files so glance/erase need ascend. */

#include "ulib.h"

#define ACCT_NAME     16
#define ACCT_PASS     32
#define ACCT_MAX      8
#define ACCOUNTS_FILE "sys/accounts"
#define SESSION_FILE  "sys/session"

typedef struct {
    char name[ACCT_NAME];
    char hash[9];
} acct_t;

static acct_t accts[ACCT_MAX];
static int naccts;

static int eqstr(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void scopy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void swipe(char *s, int n) {
    for (int i = 0; i < n; i++) s[i] = 0;
}

static int acct_name_ok(const char *s) {
    int n = 0;
    if (!s[0] || eqstr(s, "guest")) return 0;
    while (s[n]) {
        char c = s[n];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_';
        if (!ok) return 0;
        n++;
        if (n >= ACCT_NAME) return 0;
    }
    return 1;
}

static uint32_t mix_hash(const char *user, const char *pass) {
    uint32_t h = 2166136261u;
    for (const char *s = user; *s; s++) {
        h ^= (uint8_t) *s;
        h *= 16777619u;
    }
    h ^= 0xA5u;
    for (const char *s = pass; *s; s++) {
        h ^= (uint8_t) *s;
        h *= 16777619u;
    }
    return h;
}

static void hash_hex(const char *user, const char *pass, char out[9]) {
    uint32_t h = mix_hash(user, pass);
    const char *hexd = "0123456789abcdef";
    for (int i = 7; i >= 0; i--) {
        out[i] = hexd[h & 0xfu];
        h >>= 4;
    }
    out[8] = '\0';
}

static int acct_find(const char *name) {
    for (int i = 0; i < naccts; i++)
        if (eqstr(accts[i].name, name)) return i;
    return -1;
}

static char fold_ch(char c) {
    if (c >= 'A' && c <= 'Z') return (char) (c - 'A' + 'a');
    return c;
}

/* Login accepts ADA for the stored name ada. The stored spelling wins. */
static int acct_find_fold(const char *name) {
    for (int i = 0; i < naccts; i++) {
        int k = 0;
        while (accts[i].name[k] && fold_ch(accts[i].name[k]) == fold_ch(name[k]))
            k++;
        if (!accts[i].name[k] && !name[k]) return i;
    }
    return -1;
}

static int acct_count(void) { return naccts; }

static void acct_load(void) {
    naccts = 0;
    char buf[512];
    int n = ureadfile(ACCOUNTS_FILE, buf, (int) sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';
    int i = 0;
    while (i < n && naccts < ACCT_MAX) {
        while (buf[i] == '\n' || buf[i] == '\r') i++;
        if (i >= n) break;
        int ns = i;
        while (i < n && buf[i] != ' ' && buf[i] != '\n' && buf[i] != '\r') i++;
        int nlen = i - ns;
        if (nlen <= 0 || nlen >= ACCT_NAME || i >= n || buf[i] != ' ') {
            while (i < n && buf[i] != '\n') i++;
            continue;
        }
        i++;
        int hs = i;
        while (i < n && buf[i] != '\n' && buf[i] != '\r') i++;
        if (i - hs != 8) continue;
        for (int k = 0; k < nlen; k++) accts[naccts].name[k] = buf[ns + k];
        accts[naccts].name[nlen] = '\0';
        for (int k = 0; k < 8; k++) accts[naccts].hash[k] = buf[hs + k];
        accts[naccts].hash[8] = '\0';
        naccts++;
    }
}

static int acct_save(void) {
    char buf[512];
    int p = 0;
    for (int i = 0; i < naccts; i++) {
        int k = 0;
        while (accts[i].name[k] && p < (int) sizeof(buf) - 12) {
            buf[p++] = accts[i].name[k++];
        }
        buf[p++] = ' ';
        for (int h = 0; h < 8; h++) buf[p++] = accts[i].hash[h];
        buf[p++] = '\n';
    }
    return uwritefile(ACCOUNTS_FILE, buf, p) == 0;
}

static int acct_check(const char *name, const char *pass) {
    int i = acct_find(name);
    if (i < 0) return 0;
    char hex[9];
    hash_hex(name, pass, hex);
    int ok = eqstr(accts[i].hash, hex);
    swipe(hex, 9);
    return ok;
}

static int acct_read_secret(char *buf, int max) {
    int n = 0;
    for (;;) {
        char c = ugetchar();
        if (c == '\n' || c == '\r') {
            uprint("\n");
            break;
        }
        if (c == '\b' || c == 127) {
            if (n > 0) {
                n--;
                uprint("\b");
            }
            continue;
        }
        if (c < 32) continue;
        if (n < max - 1) {
            buf[n++] = c;
            uprint("*");
        }
    }
    buf[n] = '\0';
    return n;
}

/* Ask twice. On success write the verifier into hex[9] and return 1. */
static int acct_ask_pass(const char *user, char hex[9]) {
    char a[ACCT_PASS], b[ACCT_PASS];
    uprint("password: ");
    int n = acct_read_secret(a, ACCT_PASS);
    uprint("repeat: ");
    acct_read_secret(b, ACCT_PASS);
    int ok = n > 0 && eqstr(a, b);
    if (ok) hash_hex(user, a, hex);
    else uprint(n == 0 ? "empty password\n" : "passwords differ\n");
    swipe(a, ACCT_PASS);
    swipe(b, ACCT_PASS);
    return ok;
}

static int acct_add(const char *name, const char *hex) {
    if (naccts >= ACCT_MAX || acct_find(name) >= 0) return 0;
    scopy(accts[naccts].name, name, ACCT_NAME);
    scopy(accts[naccts].hash, hex, 9);
    naccts++;
    return 1;
}

static void __attribute__((unused)) acct_drop(int idx) {
    if (idx < 0 || idx >= naccts) return;
    for (int i = idx; i < naccts - 1; i++) accts[i] = accts[i + 1];
    naccts--;
    swipe(accts[naccts].name, ACCT_NAME);
    swipe(accts[naccts].hash, 9);
}

static int __attribute__((unused)) session_load(char *out) {
    char buf[ACCT_NAME];
    int n = ureadfile(SESSION_FILE, buf, ACCT_NAME - 1);
    if (n <= 0) return 0;
    int i = 0;
    while (i < n && buf[i] != '\n' && buf[i] != '\r') i++;
    buf[i] = '\0';
    if (acct_find(buf) < 0) return 0;
    scopy(out, buf, ACCT_NAME);
    return 1;
}

static int session_save(const char *who) {
    char buf[ACCT_NAME + 2];
    int i = 0;
    while (who[i] && i < ACCT_NAME - 1) {
        buf[i] = who[i];
        i++;
    }
    buf[i++] = '\n';
    buf[i] = '\0';
    return uwritefile(SESSION_FILE, buf, i) == 0;
}

/* Always ask for a name, then a password. Copy the stored name into who_out. */
static void acct_login(char *who_out) {
    for (;;) {
        if (naccts > 0) {
            uprint("users:");
            for (int i = 0; i < naccts; i++) {
                uprint(" ");
                uprint(accts[i].name);
            }
            uprint("\n");
        }
        uprint("user: ");
        char name[ACCT_NAME];
        ureadline(name, ACCT_NAME);
        int i = acct_find_fold(name);
        if (i < 0) {
            uprint("no such user\n");
            continue;
        }
        uprint("password: ");
        char pw[ACCT_PASS];
        int n = acct_read_secret(pw, ACCT_PASS);
        int ok = n > 0 && acct_check(accts[i].name, pw);
        swipe(pw, ACCT_PASS);
        if (!ok) {
            uprint("denied\n");
            continue;
        }
        scopy(who_out, accts[i].name, ACCT_NAME);
        return;
    }
}

static void __attribute__((unused)) acct_create_first(void) {
    uprint("No user yet. Name the first account.\n");
    for (;;) {
        uprint("user: ");
        char name[ACCT_NAME];
        ureadline(name, ACCT_NAME);
        if (!acct_name_ok(name)) {
            uprint("use letters, digits, or underscore\n");
            continue;
        }
        char hex[9];
        if (!acct_ask_pass(name, hex)) continue;
        acct_add(name, hex);
        if (!acct_save()) {
            naccts = 0;
            uprint("could not store the account\n");
            continue;
        }
        swipe(hex, 9);
        uprint("stored.\n");
        return;
    }
}

#endif
