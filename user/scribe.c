#include "ulib.h"

/* scribe — terminal text editor.
 * Usage: scribe.elf FILE
 *
 * The screen is a 40-column window inside the terminal grid. Arrows, Home,
 * and End move. Enter splits a line. Backspace deletes. Ctrl-O saves,
 * Ctrl-X quits (and asks when there are unsaved changes), Ctrl-K cuts the
 * current line, Ctrl-G shows help. A missing file starts empty and is
 * created on save. Esc stays with the desk (command mode) and is not used. */

#define MAX_LINES 40
#define LINE_LEN  72
#define VIEW      12
#define WIDTH     40
#define BUF_MAX   (MAX_LINES * (LINE_LEN + 1))
#define NAME_MAX  24

#define K_HELP  0x07 /* Ctrl-G */
#define K_CUT   0x0B /* Ctrl-K */
#define K_SAVE  0x0F /* Ctrl-O */
#define K_LEFT  0x11
#define K_RIGHT 0x12
#define K_HOME  0x13
#define K_END   0x14
#define K_UP    0x15
#define K_DOWN  0x16
#define K_QUIT  0x18 /* Ctrl-X */

static char lines[MAX_LINES][LINE_LEN];
static uint8_t raw[BUF_MAX];
static int nlines;
static int row, col, pref;
static int dirty;
static int help;
static int ask;
static char fname[NAME_MAX];
static char note[24];

static int slen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void scopy(char *d, const char *s) {
    int i = 0;
    while (s[i] && i < LINE_LEN - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = '\0';
}

static void set_note(const char *s) {
    int i = 0;
    while (s[i] && i < (int) sizeof(note) - 1) {
        note[i] = s[i];
        i++;
    }
    note[i] = '\0';
}

static void cpy_name(char *dst, const char *src) {
    int i = 0;
    while (src[i] && src[i] != ' ' && i < NAME_MAX - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void put_dec(char *b, int *n, int v) {
    char tmp[4];
    int i = 0;
    if (v < 1) v = 1;
    if (v > 99) v = 99;
    do {
        tmp[i++] = (char) ('0' + (v % 10));
        v /= 10;
    } while (v && i < 3);
    while (i) b[(*n)++] = tmp[--i];
}

static void cup(int row1, int col1) {
    char b[16];
    int n = 0;
    b[n++] = 27;
    b[n++] = '[';
    put_dec(b, &n, row1);
    b[n++] = ';';
    put_dec(b, &n, col1);
    b[n++] = 'H';
    b[n] = '\0';
    uprint(b);
}

static void emit_cap(const char *s, int max) {
    char b[WIDTH + 1];
    int i = 0;
    if (max > WIDTH) max = WIDTH;
    while (s[i] && i < max) {
        b[i] = s[i];
        i++;
    }
    b[i] = '\0';
    if (i) uprint(b);
}

static void load_file(void) {
    int cut = 0;
    nlines = 1;
    lines[0][0] = '\0';
    row = col = pref = 0;
    dirty = 0;
    int n = ureadfile(fname, raw, BUF_MAX);
    if (n < 0) {
        set_note("new file");
        return;
    }
    if (n > BUF_MAX) n = BUF_MAX;
    if (n == BUF_MAX) cut = 1;
    int line = 0;
    int at = 0;
    lines[0][0] = '\0';
    for (int i = 0; i < n; i++) {
        unsigned char c = raw[i];
        if (c == '\r') continue;
        if (c == '\n') {
            lines[line][at] = '\0';
            if (line + 1 >= MAX_LINES) {
                cut = 1;
                break;
            }
            line++;
            at = 0;
            lines[line][0] = '\0';
            continue;
        }
        if (c < 32 || c > 126) c = '?';
        if (at < LINE_LEN - 1) lines[line][at++] = (char) c;
        else cut = 1;
    }
    lines[line][at] = '\0';
    nlines = line + 1;
    if (n > 0 && raw[n - 1] == '\n' && nlines > 1 && lines[nlines - 1][0] == '\0')
        nlines--;
    if (cut) set_note("truncated");
    else if (n == 0) set_note("empty");
    else note[0] = '\0';
}

static int save_file(void) {
    int n = 0;
    for (int i = 0; i < nlines; i++) {
        int L = slen(lines[i]);
        if (n + L + 1 > BUF_MAX) return -1;
        for (int j = 0; j < L; j++) raw[n++] = (uint8_t) lines[i][j];
        raw[n++] = '\n';
    }
    if (uwritefile(fname, raw, n) < 0) return -1;
    dirty = 0;
    set_note("wrote");
    return n;
}

static void insert_char(char ch) {
    int n = slen(lines[row]);
    if (col > n) col = n;
    if (n >= LINE_LEN - 1) {
        set_note("line full");
        return;
    }
    for (int i = n; i >= col; i--) lines[row][i + 1] = lines[row][i];
    lines[row][col] = ch;
    col++;
    pref = col;
    dirty = 1;
    note[0] = '\0';
}

static void split_line(void) {
    int n = slen(lines[row]);
    if (col > n) col = n;
    if (nlines >= MAX_LINES) {
        set_note("buffer full");
        return;
    }
    for (int i = nlines; i > row + 1; i--) scopy(lines[i], lines[i - 1]);
    scopy(lines[row + 1], lines[row] + col);
    lines[row][col] = '\0';
    nlines++;
    row++;
    col = 0;
    pref = 0;
    dirty = 1;
    note[0] = '\0';
}

static void backspace(void) {
    int n = slen(lines[row]);
    if (col > n) col = n;
    if (col > 0) {
        for (int i = col - 1; i < n; i++) lines[row][i] = lines[row][i + 1];
        col--;
        pref = col;
        dirty = 1;
        note[0] = '\0';
        return;
    }
    if (row == 0) return;
    int prev = slen(lines[row - 1]);
    if (prev + n >= LINE_LEN) {
        set_note("line full");
        return;
    }
    for (int i = 0; i <= n; i++) lines[row - 1][prev + i] = lines[row][i];
    for (int i = row; i < nlines - 1; i++) scopy(lines[i], lines[i + 1]);
    nlines--;
    row--;
    col = prev;
    pref = col;
    dirty = 1;
    note[0] = '\0';
}

static void cut_line(void) {
    if (nlines == 1) {
        lines[0][0] = '\0';
    } else {
        for (int i = row; i < nlines - 1; i++) scopy(lines[i], lines[i + 1]);
        nlines--;
        if (row >= nlines) row = nlines - 1;
    }
    col = 0;
    pref = 0;
    dirty = 1;
    note[0] = '\0';
}

static void move_vert(int dir) {
    int next = row + dir;
    int n;
    if (next < 0 || next >= nlines) return;
    row = next;
    col = pref;
    n = slen(lines[row]);
    if (col > n) col = n;
}

static void draw(void) {
    static int top = 0;
    if (row < top) top = row;
    if (row >= top + VIEW) top = row - VIEW + 1;
    if (top < 0) top = 0;

    uprint("\f");
    if (help) {
        emit_cap("scribe", WIDTH); uprint("\n");
        emit_cap("arrows home end  backspace", WIDTH); uprint("\n");
        emit_cap("enter splits the line", WIDTH); uprint("\n");
        emit_cap("^O save   ^X quit", WIDTH); uprint("\n");
        emit_cap("^K cut line   ^G help", WIDTH); uprint("\n");
        emit_cap("any key closes help", WIDTH); uprint("\n");
        for (int i = 6; i < VIEW; i++) uprint("\n");
    } else {
        int off = 0;
        if (col >= WIDTH) off = col - (WIDTH - 1);
        for (int i = 0; i < VIEW; i++) {
            int li = top + i;
            if (li < nlines) {
                if (li == row) emit_cap(lines[li] + off, WIDTH);
                else emit_cap(lines[li], WIDTH);
            }
            uprint("\n");
        }
    }

    char st[WIDTH + 1];
    int n = 0;
    int name_max = 14;
    for (int i = 0; fname[i] && n < name_max; i++) st[n++] = fname[i];
    st[n++] = ' ';
    st[n++] = 'L';
    put_dec(st, &n, row + 1);
    st[n++] = ' ';
    st[n++] = 'C';
    put_dec(st, &n, col + 1);
    if (dirty && n < WIDTH - 2) {
        st[n++] = ' ';
        st[n++] = '*';
    }
    if (note[0] && n < WIDTH - 1) {
        st[n++] = ' ';
        for (int i = 0; note[i] && n < WIDTH; i++) st[n++] = note[i];
    }
    st[n] = '\0';
    emit_cap(st, WIDTH);
    uprint("\n");
    if (ask) emit_cap("save changes? y/n", WIDTH);
    else emit_cap("^O save  ^X quit  ^K cut  ^G help", WIDTH);

    if (!help) {
        int off = 0;
        if (col >= WIDTH) off = col - (WIDTH - 1);
        int sy = (row - top) + 1;
        int sx = (col - off) + 1;
        if (sy < 1) sy = 1;
        if (sx < 1) sx = 1;
        cup(sy, sx);
    }
}

static void leave(void) {
    uprint("\033[16;1H\n");
}

int main(int argc, char **argv) {
    if (argc < 2 || !argv[1] || !argv[1][0]) {
        uprint("scribe: file name required\n");
        return 1;
    }
    cpy_name(fname, argv[1]);
    int nlen = slen(fname);
    if (nlen > 0 && fname[nlen - 1] == '/') {
        uprint("scribe: is a directory\n");
        return 1;
    }
    load_file();

    for (;;) {
        draw();
        unsigned char c = (unsigned char) ugetchar();
        if (c == 0) continue;
        if (ask) {
            ask = 0;
            if (c == 'y' || c == 'Y') {
                if (save_file() < 0) {
                    set_note("cannot write");
                    continue;
                }
                break;
            }
            if (c == 'n' || c == 'N') break;
            continue;
        }
        if (help) {
            help = 0;
            continue;
        }
        if (c == K_QUIT) {
            if (dirty) ask = 1;
            else break;
            continue;
        }
        if (c == K_SAVE) {
            if (save_file() < 0) set_note("cannot write");
            continue;
        }
        if (c == K_HELP) {
            help = 1;
            continue;
        }
        if (c == K_CUT) {
            cut_line();
            continue;
        }
        if (c == K_LEFT) {
            if (col > 0) col--;
            else if (row > 0) {
                row--;
                col = slen(lines[row]);
            }
            pref = col;
            continue;
        }
        if (c == K_RIGHT) {
            if (col < slen(lines[row])) col++;
            else if (row + 1 < nlines) {
                row++;
                col = 0;
            }
            pref = col;
            continue;
        }
        if (c == K_HOME) {
            col = 0;
            pref = 0;
            continue;
        }
        if (c == K_END) {
            col = slen(lines[row]);
            pref = col;
            continue;
        }
        if (c == K_UP) {
            move_vert(-1);
            continue;
        }
        if (c == K_DOWN) {
            move_vert(1);
            continue;
        }
        if (c == '\b') {
            backspace();
            continue;
        }
        if (c == '\n') {
            split_line();
            continue;
        }
        if (c >= 32 && c <= 126) insert_char((char) c);
    }
    leave();
    return 0;
}
