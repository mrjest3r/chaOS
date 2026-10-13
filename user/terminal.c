#include "ulib.h"
#include "verbs.h"
#include "accounts.h"

/* terminal — deck shell. Names come from verbs.h. This file never matches
 * classic names (ls, rm, exit, sudo). System files (*.elf, attune,
 * accounts, session) stay closed until `ascend`. ascend checks the
 * logged-in user's password. With no account file the prompt is guest
 * so scripted tests can run before gate.elf has created anyone.
 *
 * A directory is a zero-byte file whose name ends in '/'. A file inside
 * it keeps the path in that same name ("home/ideas.txt"). wander / abode /
 * forge track the current directory here. summon of a bare name runs
 * the program in bin/.
 * Hidden names are leaves that start with '.'. conceal renames into
 * that form; unveil lists them. */

#define FILE_MAX   (32 * 512) /* matches FS_MAX_FILESIZE */
#define NAME_MAX   24
#define TICKS_HZ   50

static int ascended;
static int show_hidden;
static char who[ACCT_NAME];
/* No leading or trailing slash. Empty means the root. */
static char cwd[NAME_MAX];
static uint8_t filebuf[FILE_MAX];

static int streq(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static int ends_with(const char *s, const char *suf) {
    int n = 0, m = 0;
    while (s[n]) n++;
    while (suf[m]) m++;
    if (m > n) return 0;
    for (int i = 0; i < m; i++)
        if (s[n - m + i] != suf[i]) return 0;
    return 1;
}

static int plen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void pcopy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Last component. A trailing slash stays on the returned pointer. */
static const char *leaf_of(const char *full) {
    const char *leaf = full;
    int n = plen(full);
    int end = n;
    if (end > 0 && full[end - 1] == '/') end--;
    for (int i = 0; i < end; i++)
        if (full[i] == '/') leaf = full + i + 1;
    return leaf;
}

static void leaf_copy(const char *full, char *out) {
    const char *leaf = leaf_of(full);
    int i = 0;
    while (leaf[i] && leaf[i] != '/' && i < NAME_MAX - 1) {
        out[i] = leaf[i];
        i++;
    }
    out[i] = '\0';
}

static int is_system(const char *name) {
    char leaf[NAME_MAX];
    leaf_copy(name, leaf);
    if (streq(leaf, "attune")) return 1;
    if (streq(leaf, ACCOUNTS_FILE)) return 1;
    if (streq(leaf, SESSION_FILE)) return 1;
    return ends_with(leaf, ".elf");
}

static int is_hidden(const char *name) {
    char leaf[NAME_MAX];
    leaf_copy(name, leaf);
    return leaf[0] == '.';
}

static void take_name(const char *s, char *name) {
    int i = 0;
    while (s[i] && s[i] != ' ' && i < NAME_MAX - 1) {
        name[i] = s[i];
        i++;
    }
    name[i] = '\0';
}

static const char *skip_name(const char *s) {
    while (*s && *s != ' ') s++;
    while (*s == ' ') s++;
    return s;
}

static int starts_with(const char *s, const char *p) {
    int i = 0;
    while (p[i]) {
        if (s[i] != p[i]) return 0;
        i++;
    }
    return 1;
}

static void pop_comp(char *path) {
    int n = plen(path);
    if (n == 0) return;
    while (n > 0 && path[n - 1] != '/') n--;
    if (n > 0) n--;
    path[n] = '\0';
}

static int push_comp(char *path, const char *comp, int clen) {
    int n = plen(path);
    if (n + clen + (n ? 1 : 0) >= NAME_MAX) return 0;
    if (n) path[n++] = '/';
    for (int i = 0; i < clen; i++) path[n++] = comp[i];
    path[n] = '\0';
    return 1;
}

/* Token to a stored path with no leading or trailing slash.
 * Empty out means the root. Returns 0 if the path does not fit. */
static int normalize(const char *token, char *out) {
    char tmp[NAME_MAX];
    const char *p = token;
    if (p[0] == '/') {
        tmp[0] = '\0';
        p++;
    } else {
        pcopy(tmp, cwd, NAME_MAX);
    }
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *start = p;
        while (*p && *p != '/') p++;
        int len = (int) (p - start);
        if (len == 1 && start[0] == '.') continue;
        if (len == 2 && start[0] == '.' && start[1] == '.') {
            pop_comp(tmp);
            continue;
        }
        if (!push_comp(tmp, start, len)) return 0;
    }
    pcopy(out, tmp, NAME_MAX);
    return 1;
}

static int dir_marker(const char *path, char *out) {
    int n = plen(path);
    if (n == 0 || n + 1 >= NAME_MAX) return 0;
    pcopy(out, path, NAME_MAX);
    out[n] = '/';
    out[n + 1] = '\0';
    return 1;
}

static ufileinfo_t *list_buf;
static int list_n;

static ufileinfo_t *load_list(void) {
    if (!list_buf)
        list_buf = (ufileinfo_t *) umalloc(UFS_MAX_FILES * (uint32_t) sizeof(ufileinfo_t));
    if (!list_buf) { list_n = 0; return 0; }
    list_n = ulistfiles(list_buf, UFS_MAX_FILES);
    if (list_n < 0) list_n = 0;
    return list_buf;
}

static int name_exists(const char *name) {
    char b;
    return ureadfile(name, &b, 1) >= 0;
}

static int is_dir(const char *path) {
    char mark[NAME_MAX];
    if (!path[0] || !dir_marker(path, mark)) return 0;
    return name_exists(mark);
}

/* Parent of a path must already be a directory. Root has no parent. */
static int parent_ready(const char *full) {
    int slash = -1;
    for (int i = 0; full[i]; i++)
        if (full[i] == '/') slash = i;
    if (slash < 0) return 1;
    char parent[NAME_MAX];
    if (slash + 1 >= NAME_MAX) return 0;
    for (int i = 0; i <= slash; i++) parent[i] = full[i];
    parent[slash + 1] = '\0';
    return name_exists(parent);
}

static int has_child(const char *path) {
    char mark[NAME_MAX];
    if (!dir_marker(path, mark)) return 1;
    int mlen = plen(mark);
    ufileinfo_t *files = load_list();
    int n = list_n;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        if (!starts_with(files[i].name, mark)) continue;
        if (plen(files[i].name) > mlen) return 1;
    }
    return 0;
}

/* One component under cwd. Directories keep a trailing slash in storage. */
static int in_cwd(const char *full, char *leaf, int *is_directory) {
    const char *rest = full;
    if (cwd[0]) {
        int n = plen(cwd);
        for (int i = 0; i < n; i++)
            if (full[i] != cwd[i]) return 0;
        if (full[n] != '/') return 0;
        rest = full + n + 1;
    }
    int m = 0;
    while (rest[m]) {
        if (rest[m] == '/' && rest[m + 1]) return 0;
        m++;
    }
    if (m == 0) return 0;
    *is_directory = rest[m - 1] == '/';
    int copy = *is_directory ? m - 1 : m;
    if (copy <= 0 || copy >= NAME_MAX) return 0;
    for (int i = 0; i < copy; i++) leaf[i] = rest[i];
    leaf[copy] = '\0';
    return 1;
}

static void print_place(void) {
    uprint("/");
    if (cwd[0]) uprint(cwd);
    uprint("\n");
}

/* If dst is an existing directory, or the root, put src's leaf inside it. */
static int place_dest(const char *src, char *dst) {
    if (dst[0] && !is_dir(dst)) return 1;
    const char *leaf = leaf_of(src);
    int ll = 0;
    while (leaf[ll] && leaf[ll] != '/') ll++;
    if (ll <= 0 || ll >= NAME_MAX) return 0;
    if (!dst[0]) {
        for (int i = 0; i < ll; i++) dst[i] = leaf[i];
        dst[ll] = '\0';
        return 1;
    }
    int dl = plen(dst);
    if (dl + 1 + ll >= NAME_MAX) return 0;
    dst[dl] = '/';
    for (int i = 0; i < ll; i++) dst[dl + 1 + i] = leaf[i];
    dst[dl + 1 + ll] = '\0';
    return 1;
}

static int path_inside(const char *path, const char *dir) {
    int n = plen(dir);
    if (!n || !starts_with(path, dir)) return 0;
    return path[n] == '/';
}

static int rewrite_prefix(const char *name, const char *oldp, const char *newp,
                          char *out) {
    int ol = plen(oldp);
    if (!starts_with(name, oldp)) return 0;
    if (name[ol] != '/' && name[ol] != '\0') return 0;
    int nl = plen(newp);
    int rest = plen(name + ol);
    if (nl + rest >= NAME_MAX) return 0;
    for (int i = 0; i < nl; i++) out[i] = newp[i];
    for (int i = 0; i <= rest; i++) out[nl + i] = name[ol + i];
    return 1;
}

static int collect_tree(const char *path, char names[][NAME_MAX], int max) {
    char mark[NAME_MAX];
    if (!dir_marker(path, mark)) return -1;
    ufileinfo_t *files = load_list();
    int n = list_n;
    if (n < 0) n = 0;
    int c = 0;
    for (int i = 0; i < n; i++) {
        if (!starts_with(files[i].name, mark)) continue;
        if (c >= max) return -1;
        pcopy(names[c], files[i].name, NAME_MAX);
        c++;
    }
    return c;
}

static int load_file(const char *name, int *out_n);

/* Copy a directory tree. del removes the source after a full copy.
 * -2 means a system file blocked it. */
static int transfer_tree(const char *src, const char *dst, int del) {
    char from[32][NAME_MAX];
    int c = collect_tree(src, from, 32);
    if (c <= 0) return -1;
    if (!ascended) {
        for (int i = 0; i < c; i++)
            if (is_system(from[i])) return -2;
    }
    char to[NAME_MAX];
    for (int i = 0; i < c; i++) {
        if (!rewrite_prefix(from[i], src, dst, to)) return -1;
        if (name_exists(to)) return -1;
    }
    for (int i = 0; i < c; i++) {
        int n = 0;
        if (!rewrite_prefix(from[i], src, dst, to)) return -1;
        if (load_file(from[i], &n) < 0) return -1;
        if (uwritefile(to, filebuf, n) < 0) return -1;
    }
    if (del) {
        for (int i = 0; i < c; i++) udelete(from[i]);
    }
    return 0;
}

static int hidden_path(const char *path, char *out) {
    const char *leaf = leaf_of(path);
    if (leaf[0] == '.') return 0;
    int n = plen(path);
    int end = n;
    if (end > 0 && path[end - 1] == '/') end--;
    int slash = -1;
    for (int i = 0; i < end; i++)
        if (path[i] == '/') slash = i;
    int ll = 0;
    while (leaf[ll] && leaf[ll] != '/') ll++;
    int pl = slash < 0 ? 0 : slash;
    if (ll + 1 + (pl ? pl + 1 : 0) >= NAME_MAX) return 0;
    int o = 0;
    for (int i = 0; i < pl; i++) out[o++] = path[i];
    if (pl) out[o++] = '/';
    out[o++] = '.';
    for (int i = 0; i < ll; i++) out[o++] = leaf[i];
    out[o] = '\0';
    return 1;
}

static int take_path(const char *token, char *out) {
    if (!token[0] || !normalize(token, out) || !out[0]) return 0;
    return 1;
}

static int parse_u(const char *s, int *out) {
    if (!s[0]) return 0;
    int n = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == ' ') break;
        if (s[i] < '0' || s[i] > '9') return 0;
        n = n * 10 + (s[i] - '0');
    }
    *out = n;
    return 1;
}

static int contains(const char *s, int n, const char *w) {
    int m = 0;
    while (w[m]) m++;
    if (m == 0 || m > n) return 0;
    for (int i = 0; i <= n - m; i++) {
        int ok = 1;
        for (int j = 0; j < m; j++) {
            if (s[i + j] != w[j]) { ok = 0; break; }
        }
        if (ok) return 1;
    }
    return 0;
}

static void usage(const verb_t *v, const char *rest) {
    uprint("usage: ");
    uprint(v->verb);
    uprint(" ");
    uprint(rest);
    uprint("\n");
}

static void not_yet(const verb_t *v) {
    uprint(v->verb);
    uprint(": not in this OS yet\n");
}

static int deny_system(const char *name) {
    if (!is_system(name) || ascended) return 0;
    uprint("cannot access system file (ascend first)\n");
    return 1;
}

static int load_file(const char *name, int *out_n) {
    int n = ureadfile(name, filebuf, FILE_MAX);
    if (n < 0) return -1;
    *out_n = n;
    return 0;
}

static void cmd_lexicon(void) {
    uprint("verbs (edit user/verbs.h to rename; internal ids stay put)\n");
    for (int i = 0; i < verb_count(); i++) {
        if (!verbs[i].verb) continue;
        uprint("  ");
        uprint(verbs[i].verb);
        uprint("  - ");
        uprint(verbs[i].blurb);
        if (verbs[i].admin) uprint("  [admin]");
        if (!verbs[i].live) uprint("  [later]");
        uprint("\n");
    }
}

static void cmd_reveal(void) {
    ufileinfo_t *files = load_list();
    int n = list_n;
    int shown = 0;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        char leaf[NAME_MAX];
        int dir = 0;
        if (!in_cwd(files[i].name, leaf, &dir)) continue;
        if (is_hidden(files[i].name) && !show_hidden) continue;
        uprint("  ");
        uprint(leaf);
        if (dir) uprint("/");
        uprint("  (");
        uprint_dec(files[i].size);
        uprint(" bytes)");
        if (is_system(files[i].name)) uprint("  system");
        if (is_hidden(files[i].name)) uprint("  hidden");
        uprint("\n");
        shown++;
    }
    if (shown == 0) uprint("(empty)\n");
}

static void cmd_wander(const verb_t *v, const char *args) {
    char tok[NAME_MAX], path[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "DIR"); return; }
    if (!normalize(tok, path)) { uprint("name too long\n"); return; }
    if (path[0] && !is_dir(path)) {
        uprint("no such directory\n");
        return;
    }
    pcopy(cwd, path, NAME_MAX);
    print_place();
}

static void cmd_abode(void) {
    print_place();
}

static void cmd_forge(const verb_t *v, const char *args) {
    char tok[NAME_MAX], path[NAME_MAX], mark[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "DIR"); return; }
    if (!normalize(tok, path) || !path[0]) { uprint("forge: bad path\n"); return; }
    if (!parent_ready(path)) { uprint("no such directory\n"); return; }
    if (!dir_marker(path, mark)) { uprint("name too long\n"); return; }
    if (name_exists(mark)) { uprint("directory exists\n"); return; }
    if (name_exists(path)) { uprint("name taken\n"); return; }
    if (uwritefile(mark, filebuf, 0) < 0) uprint("forge failed\n");
    else uprint("forged.\n");
}

static int open_editor(const char *name) {
    char cmd[40];
    const char *pre = "bin/scribe.elf ";
    int i = 0;
    while (pre[i]) {
        cmd[i] = pre[i];
        i++;
    }
    for (int j = 0; name[j] && i < (int) sizeof(cmd) - 1; j++) cmd[i++] = name[j];
    cmd[i] = '\0';
    return urun(cmd);
}

static void cmd_scribe(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE [text]"); return; }
    if (!take_path(tok, name)) { uprint("scribe: bad path\n"); return; }
    if (deny_system(name)) return;
    if (is_dir(name)) { uprint("is a directory\n"); return; }
    if (!parent_ready(name)) { uprint("no such directory\n"); return; }
    const char *text = skip_name(args);
    /* Text on a new file is a one-shot write. Anything else opens the editor,
     * including text aimed at a file that already exists. */
    if (text[0] && !name_exists(name)) {
        int len = 0;
        while (text[len]) len++;
        if (uwritefile(name, text, len) < 0) uprint("scribe failed\n");
        else uprint("scribed.\n");
        return;
    }
    if (open_editor(name) < 0) uprint("cannot open scribe\n");
}

static void cmd_glance(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE"); return; }
    if (!take_path(tok, name)) { uprint("glance: bad path\n"); return; }
    if (deny_system(name)) return;
    if (is_dir(name)) { uprint("is a directory\n"); return; }
    int n = 0;
    if (load_file(name, &n) < 0) {
        uprint("cannot glance ");
        uprint(name);
        uprint("\n");
        return;
    }
    filebuf[n < FILE_MAX ? n : FILE_MAX - 1] = 0;
    if (n >= FILE_MAX) n = FILE_MAX - 1;
    uprint((char *) filebuf);
    if (n == 0 || filebuf[n - 1] != '\n') uprint("\n");
}

static void cmd_erase(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX], mark[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE"); return; }
    if (!take_path(tok, name)) { uprint("erase: bad path\n"); return; }
    if (is_dir(name)) {
        if (!dir_marker(name, mark)) { uprint("erase: bad path\n"); return; }
        if (deny_system(mark)) return;
        if (has_child(name)) { uprint("directory not empty\n"); return; }
        if (udelete(mark) < 0) uprint("cannot erase ");
        else { uprint("erased.\n"); return; }
        uprint(name);
        uprint("\n");
        return;
    }
    if (deny_system(name)) return;
    if (udelete(name) < 0) {
        uprint("cannot erase ");
        uprint(name);
        uprint("\n");
    } else {
        uprint("erased.\n");
    }
}

static int pair_paths(const verb_t *v, const char *args, const char *usage_rest,
                      char *src, char *dst) {
    char a[NAME_MAX], b[NAME_MAX];
    take_name(args, a);
    take_name(skip_name(args), b);
    if (!a[0] || !b[0]) { usage(v, usage_rest); return 0; }
    if (!normalize(a, src) || !src[0] || !normalize(b, dst)) {
        uprint("bad path\n");
        return 0;
    }
    if (!place_dest(src, dst) || !dst[0]) { uprint("bad path\n"); return 0; }
    if (streq(src, dst)) { uprint("same name\n"); return 0; }
    return 1;
}

static void cmd_duplicate(const verb_t *v, const char *args) {
    char src[NAME_MAX], dst[NAME_MAX];
    if (!pair_paths(v, args, "SRC DST", src, dst)) return;
    if (is_dir(src)) {
        if (path_inside(dst, src)) { uprint("cannot copy a directory into itself\n"); return; }
        if (!parent_ready(dst)) { uprint("no such directory\n"); return; }
        int rc = transfer_tree(src, dst, 0);
        if (rc == -2) uprint("cannot access system file (ascend first)\n");
        else if (rc < 0) uprint("duplicate failed\n");
        else uprint("duplicated.\n");
        return;
    }
    if (deny_system(src) || deny_system(dst)) return;
    if (is_dir(dst)) { uprint("is a directory\n"); return; }
    if (!parent_ready(dst)) { uprint("no such directory\n"); return; }
    int n = 0;
    if (load_file(src, &n) < 0) {
        uprint("cannot duplicate ");
        uprint(src);
        uprint("\n");
        return;
    }
    if (uwritefile(dst, filebuf, n) < 0) uprint("duplicate failed\n");
    else uprint("duplicated.\n");
}

static void cmd_relocate(const verb_t *v, const char *args) {
    char src[NAME_MAX], dst[NAME_MAX];
    if (!pair_paths(v, args, "SRC DST", src, dst)) return;
    if (is_dir(src)) {
        if (path_inside(dst, src)) { uprint("cannot move a directory into itself\n"); return; }
        if (!parent_ready(dst)) { uprint("no such directory\n"); return; }
        int rc = transfer_tree(src, dst, 1);
        if (rc == -2) uprint("cannot access system file (ascend first)\n");
        else if (rc < 0) uprint("relocate failed\n");
        else uprint("relocated.\n");
        return;
    }
    if (deny_system(src) || deny_system(dst)) return;
    if (is_dir(dst)) { uprint("is a directory\n"); return; }
    if (!parent_ready(dst)) { uprint("no such directory\n"); return; }
    int n = 0;
    if (load_file(src, &n) < 0) {
        uprint("cannot relocate ");
        uprint(src);
        uprint("\n");
        return;
    }
    if (uwritefile(dst, filebuf, n) < 0) {
        uprint("relocate failed\n");
        return;
    }
    if (udelete(src) < 0) uprint("relocate: copy kept, source remains\n");
    else uprint("relocated.\n");
}

static void cmd_seek(const verb_t *v, const char *args) {
    if (!args[0]) { usage(v, "WORD"); return; }
    ufileinfo_t *files = load_list();
    int n = list_n;
    int hits = 0;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        char leaf[NAME_MAX];
        int dir = 0;
        if (!in_cwd(files[i].name, leaf, &dir)) continue;
        if (is_hidden(files[i].name) && !show_hidden) continue;
        int name_hit = contains(leaf, plen(leaf), args);
        int body_hit = 0;
        if (!dir && (!is_system(files[i].name) || ascended)) {
            int len = 0;
            if (load_file(files[i].name, &len) == 0)
                body_hit = contains((char *) filebuf, len, args);
        }
        if (!name_hit && !body_hit) continue;
        uprint("  ");
        uprint(leaf);
        if (dir) uprint("/");
        if (name_hit) uprint("  name");
        if (body_hit) uprint("  text");
        uprint("\n");
        hits++;
    }
    if (hits == 0) uprint("(no matches)\n");
}

static int lookup_name(const char *stored, ufileinfo_t *info) {
    ufileinfo_t *files = load_list();
    int n = list_n;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) {
        if (!streq(files[i].name, stored)) continue;
        *info = files[i];
        return 1;
    }
    return 0;
}

static void cmd_discern(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE"); return; }
    if (!take_path(tok, name)) { uprint("discern: bad path\n"); return; }
    if (is_dir(name)) {
        uprint(name);
        uprint("/: directory");
        if (is_hidden(name)) uprint(", hidden");
        uprint("\n");
        return;
    }
    ufileinfo_t info;
    if (!lookup_name(name, &info)) {
        uprint("no such file\n");
        return;
    }
    uprint(name);
    uprint(": ");
    if (ends_with(leaf_of(name), ".elf")) uprint("program");
    else uprint("text");
    if (is_system(name)) uprint(", system");
    if (is_hidden(name)) uprint(", hidden");
    uprint("\n");
    (void) info;
}

static void cmd_inspect(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX], mark[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE"); return; }
    if (!take_path(tok, name)) { uprint("inspect: bad path\n"); return; }
    const char *stored = name;
    if (is_dir(name)) {
        if (!dir_marker(name, mark)) { uprint("inspect: bad path\n"); return; }
        stored = mark;
    }
    ufileinfo_t info;
    if (!lookup_name(stored, &info)) {
        uprint("no such file\n");
        return;
    }
    uprint(name);
    if (stored != name) uprint("/");
    uprint("  size=");
    uprint_dec(info.size);
    if (stored != name) uprint("  dir");
    if (is_system(stored)) uprint("  system");
    if (is_hidden(stored)) uprint("  hidden");
    uprint("\n");
}

static void cmd_conceal(const verb_t *v, const char *args) {
    char tok[NAME_MAX], name[NAME_MAX], hidden[NAME_MAX];
    take_name(args, tok);
    if (!tok[0]) { usage(v, "FILE"); return; }
    if (!take_path(tok, name)) { uprint("conceal: bad path\n"); return; }
    if (is_hidden(name)) { uprint("already hidden\n"); return; }
    if (!hidden_path(name, hidden)) { uprint("name too long to hide\n"); return; }
    if (is_dir(name)) {
        if (is_dir(hidden) || name_exists(hidden)) { uprint("name taken\n"); return; }
        int rc = transfer_tree(name, hidden, 1);
        if (rc == -2) uprint("cannot access system file (ascend first)\n");
        else if (rc < 0) uprint("conceal failed\n");
        else uprint("concealed.\n");
        return;
    }
    if (deny_system(name)) return;
    int n = 0;
    if (load_file(name, &n) < 0) {
        uprint("cannot conceal ");
        uprint(name);
        uprint("\n");
        return;
    }
    if (uwritefile(hidden, filebuf, n) < 0) { uprint("conceal failed\n"); return; }
    if (udelete(name) < 0) uprint("conceal: hidden copy kept, source remains\n");
    else uprint("concealed.\n");
}

static void cmd_summon(const verb_t *v, const char *args) {
    char prog[NAME_MAX];
    take_name(args, prog);
    if (!prog[0]) { usage(v, "PROGRAM [args]"); return; }
    /* A bare name is a program in bin/. An explicit path is used as given. */
    char path[NAME_MAX];
    pcopy(path, prog, NAME_MAX);
    int slash = 0;
    for (int i = 0; prog[i]; i++)
        if (prog[i] == '/') slash = 1;
    if (!slash && !name_exists(prog) && plen("bin/") + plen(prog) < NAME_MAX) {
        const char *pre = "bin/";
        int i = 0;
        while (pre[i]) { path[i] = pre[i]; i++; }
        pcopy(path + i, prog, NAME_MAX - i);
    }
    const char *rest = skip_name(args);
    char cmd[96];
    int n = 0;
    for (int j = 0; path[j] && n < (int) sizeof(cmd) - 1; j++) cmd[n++] = path[j];
    if (rest[0] && n < (int) sizeof(cmd) - 1) {
        cmd[n++] = ' ';
        for (int j = 0; rest[j] && n < (int) sizeof(cmd) - 1; j++) cmd[n++] = rest[j];
    }
    cmd[n] = '\0';
    int rc = urun(cmd);
    if (rc < 0) {
        uprint("cannot summon ");
        uprint(args);
        uprint("\n");
    }
}

static const char *proc_state(uint32_t s) {
    switch (s) {
        case PROC_READY:    return "ready";
        case PROC_RUNNING:  return "running";
        case PROC_SLEEPING: return "sleeping";
        case PROC_BLOCKED:  return "blocked";
        case PROC_FROZEN:   return "frozen";
        default:            return "other";
    }
}

static void cmd_observe(void) {
    uproc_t ps[16];
    int n = ulistprocs(ps, 16);
    if (n < 0) { uprint("observe failed\n"); return; }
    for (int i = 0; i < n; i++) {
        uprint("  ");
        uprint_dec((uint32_t) ps[i].id);
        uprint("  ");
        uprint(ps[i].name[0] ? ps[i].name : "?");
        uprint("\n");
    }
    if (n == 0) uprint("(none)\n");
}

static void cmd_oversee(void) {
    uproc_t ps[16];
    int n = ulistprocs(ps, 16);
    if (n < 0) { uprint("oversee failed\n"); return; }
    for (int i = 0; i < n; i++) {
        uprint("  ");
        uprint_dec((uint32_t) ps[i].id);
        uprint("  ");
        uprint(proc_state(ps[i].state));
        uprint(ps[i].user ? "  user    " : "  kernel  ");
        uprint(ps[i].name[0] ? ps[i].name : "?");
        uprint("\n");
    }
    if (n == 0) uprint("(none)\n");
}

static void cmd_ps(const verb_t *v, const char *args, int op,
                   const char *ok, const char *bad) {
    int pid = 0;
    if (!parse_u(args, &pid)) { usage(v, "PID"); return; }
    if (upsctl(pid, op) < 0) uprint(bad);
    else uprint(ok);
}

static void cmd_rebirth(void) {
    uprint("rebooting\n");
    upower(PWOP_REBOOT);
}

static void cmd_slumber(void) {
    uprint("shutting down\n");
    upower(PWOP_SHUTDOWN);
}

static void cmd_extinguish(void) {
    uprint("power off\n");
    upower(PWOP_OFF);
}

static void cmd_await(const verb_t *v, const char *args) {
    int pid = 0;
    if (!parse_u(args, &pid)) { usage(v, "PID"); return; }
    int rc = uwait(pid);
    if (rc < 0) uprint("await: not a child, or already gone\n");
    else uprint("await: done\n");
}

static void cmd_age(void) {
    uint32_t ticks = uuptime();
    uprint_dec(ticks);
    uprint(" ticks  (");
    uprint_dec(ticks / TICKS_HZ);
    uprint(" seconds)\n");
}

static void format_moment(const uclock_t *t, char *b) {
    int n = 0;
    uint32_t y = t->year;
    b[n++] = (char) ('0' + (y / 1000) % 10);
    b[n++] = (char) ('0' + (y / 100) % 10);
    b[n++] = (char) ('0' + (y / 10) % 10);
    b[n++] = (char) ('0' + y % 10);
    b[n++] = '-';
    b[n++] = (char) ('0' + (t->month / 10) % 10);
    b[n++] = (char) ('0' + t->month % 10);
    b[n++] = '-';
    b[n++] = (char) ('0' + (t->day / 10) % 10);
    b[n++] = (char) ('0' + t->day % 10);
    b[n++] = ' ';
    b[n++] = (char) ('0' + (t->hour / 10) % 10);
    b[n++] = (char) ('0' + t->hour % 10);
    b[n++] = ':';
    b[n++] = (char) ('0' + (t->min / 10) % 10);
    b[n++] = (char) ('0' + t->min % 10);
    b[n++] = ':';
    b[n++] = (char) ('0' + (t->sec / 10) % 10);
    b[n++] = (char) ('0' + t->sec % 10);
    b[n] = '\0';
}

static void cmd_moment(void) {
    uclock_t t;
    char b[24];
    if (uclock(&t) < 0) {
        uprint("no clock\n");
        return;
    }
    format_moment(&t, b);
    uprint(b);
    uprint("\n");
}

static void cmd_essence(void) {
    uprint("chaOS terminal\n");
    uprint("  pid ");
    uprint_dec((uint32_t) ugetpid());
    uprint("\n  user ");
    uprint(who);
    uprint(ascended ? "  admin\n" : "\n");
    uprint("  volume one table, paths allowed, 10000 names\n");
    uprint("  uptime ");
    uprint_dec(uuptime() / TICKS_HZ);
    uprint(" seconds\n  moment ");
    {
        uclock_t t;
        char b[24];
        if (uclock(&t) < 0) uprint("unavailable\n");
        else {
            format_moment(&t, b);
            uprint(b);
            uprint("\n");
        }
    }
}

static void cmd_provisions(void) {
    ufileinfo_t *files = load_list();
    int n = list_n;
    uint32_t bytes = 0;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) bytes += files[i].size;
    uprint("files ");
    uprint_dec((uint32_t) n);
    uprint(" / 32\nused ");
    uprint_dec(bytes);
    uprint(" bytes\none volume, names may hold a path\n");
}

static void cmd_purge(void) {
    for (int i = 0; i < 24; i++) uprint("\n");
}

static void cmd_ascend(void) {
    if (ascended) {
        uprint("already ascended\n");
        return;
    }
    if (acct_find(who) < 0) {
        uprint("no account for this session\n");
        return;
    }
    uprint("password: ");
    char pw[ACCT_PASS];
    int n = acct_read_secret(pw, ACCT_PASS);
    int ok = n > 0 && acct_check(who, pw);
    swipe(pw, ACCT_PASS);
    if (!ok) {
        uprint("denied\n");
        return;
    }
    ascended = 1;
    uprint("ascended. admin verbs are open.\n");
}

static void cmd_manifest(const verb_t *v, const char *args) {
    char name[NAME_MAX];
    take_name(args, name);
    if (!name[0]) { usage(v, "USER"); return; }
    if (!acct_name_ok(name)) {
        uprint("use letters, digits, or underscore\n");
        return;
    }
    if (acct_find(name) >= 0) {
        uprint("that user already exists\n");
        return;
    }
    if (acct_count() >= ACCT_MAX) {
        uprint("user list is full\n");
        return;
    }
    char hex[9];
    if (!acct_ask_pass(name, hex)) return;
    if (!acct_add(name, hex) || !acct_save()) uprint("manifest failed\n");
    else uprint("manifested.\n");
    swipe(hex, 9);
}

static void cmd_exile(const verb_t *v, const char *args) {
    char name[NAME_MAX];
    take_name(args, name);
    if (!name[0]) { usage(v, "USER"); return; }
    int i = acct_find(name);
    if (i < 0) {
        uprint("no such user\n");
        return;
    }
    if (streq(name, who)) {
        uprint("cannot exile the user you are\n");
        return;
    }
    if (acct_count() <= 1) {
        uprint("cannot exile the last user\n");
        return;
    }
    acct_drop(i);
    if (!acct_save()) uprint("exile failed\n");
    else uprint("exiled.\n");
}

static void cmd_key(const verb_t *v, const char *args) {
    char name[NAME_MAX];
    take_name(args, name);
    if (!name[0]) { usage(v, "USER"); return; }
    int i = acct_find(name);
    if (i < 0) {
        uprint("no such user\n");
        return;
    }
    char hex[9];
    if (!acct_ask_pass(name, hex)) return;
    scopy(accts[i].hash, hex, 9);
    swipe(hex, 9);
    if (!acct_save()) uprint("key failed\n");
    else uprint("key set.\n");
}

static void cmd_assume(const verb_t *v, const char *args) {
    char name[NAME_MAX];
    take_name(args, name);
    if (!name[0]) { usage(v, "USER"); return; }
    if (acct_find(name) < 0) {
        uprint("no such user\n");
        return;
    }
    scopy(who, name, ACCT_NAME);
    ascended = 0;
    if (!session_save(who)) uprint("assumed, session file unchanged\n");
    else {
        uprint("assumed ");
        uprint(who);
        uprint("\n");
    }
}

static void prompt(void) {
    uprint(who);
    uprint(ascended ? "# " : "> ");
}

int main(void) {
    acct_load();
    if (acct_count() == 0) {
        scopy(who, "guest", ACCT_NAME);
    } else if (!session_load(who)) {
        acct_login(who);
        session_save(who);
    }
    uprint("terminal ");
    uprint(who);
    uprint(" (pid ");
    uprint_dec((uint32_t) ugetpid());
    uprint("). lexicon lists verbs. ascend opens admin.\n");

    char line[128];
    for (;;) {
        prompt();
        ureadline(line, (int) sizeof(line));
        if (line[0] == '\0') continue;

        const char *args = 0;
        const verb_t *v = verb_lookup(line, &args);
        if (!v) {
            uprint("unknown verb. lexicon lists names.\n");
            continue;
        }
        if (v->admin && !ascended) {
            uprint("ascend first\n");
            continue;
        }
        if (!v->live) {
            not_yet(v);
            continue;
        }

        switch (v->id) {
            case VID_LEXICON:    cmd_lexicon(); break;
            case VID_BANISH:     return 0;
            case VID_SUMMON:     cmd_summon(v, args); break;
            case VID_REVEAL:     cmd_reveal(); break;
            case VID_WANDER:     cmd_wander(v, args); break;
            case VID_ABODE:      cmd_abode(); break;
            case VID_FORGE:      cmd_forge(v, args); break;
            case VID_SCRIBE:     cmd_scribe(v, args); break;
            case VID_GLANCE:     cmd_glance(v, args); break;
            case VID_DUPLICATE:  cmd_duplicate(v, args); break;
            case VID_RELOCATE:   cmd_relocate(v, args); break;
            case VID_ERASE:      cmd_erase(v, args); break;
            case VID_SEEK:       cmd_seek(v, args); break;
            case VID_DISCERN:    cmd_discern(v, args); break;
            case VID_INSPECT:    cmd_inspect(v, args); break;
            case VID_OBSERVE:    cmd_observe(); break;
            case VID_OVERSEE:    cmd_oversee(); break;
            case VID_SLAY:       cmd_ps(v, args, PSOP_SLAY, "slain.\n", "cannot slay\n"); break;
            case VID_OBLITERATE: cmd_ps(v, args, PSOP_OBLITERATE, "obliterated.\n", "cannot obliterate\n"); break;
            case VID_FREEZE:     cmd_ps(v, args, PSOP_FREEZE, "frozen.\n", "cannot freeze\n"); break;
            case VID_AWAKEN:     cmd_ps(v, args, PSOP_AWAKEN, "awake.\n", "cannot awaken\n"); break;
            case VID_REBIRTH:    cmd_rebirth(); break;
            case VID_SLUMBER:    cmd_slumber(); break;
            case VID_EXTINGUISH: cmd_extinguish(); break;
            case VID_AWAIT:      cmd_await(v, args); break;
            case VID_AGE:        cmd_age(); break;
            case VID_MOMENT:     cmd_moment(); break;
            case VID_ESSENCE:    cmd_essence(); break;
            case VID_PROVISIONS: cmd_provisions(); break;
            case VID_PURGE:      cmd_purge(); break;
            case VID_CONCEAL:    cmd_conceal(v, args); break;
            case VID_UNVEIL:
                show_hidden = !show_hidden;
                uprint(show_hidden ? "hidden files visible\n" : "hidden files concealed\n");
                break;
            case VID_MANIFEST:   cmd_manifest(v, args); break;
            case VID_EXILE:      cmd_exile(v, args); break;
            case VID_KEY:        cmd_key(v, args); break;
            case VID_ASSUME:     cmd_assume(v, args); break;
            case VID_ASCEND:     cmd_ascend(); break;
            default:             not_yet(v); break;
        }
    }
}
