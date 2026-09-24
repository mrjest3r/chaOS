#include "utext.h"

static void copy_row(char *dst, const char *src, int cols) {
    for (int c = 0; c < cols; c++) dst[c] = src[c];
}

void utext_clear(utext_t *t) {
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++) t->cell[r][c] = ' ';
    t->cx = t->cy = 0;
    t->hist_n = 0;
    t->view = 0;
}

void utext_init(utext_t *t, int cols, int rows) {
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;
    if (cols > UTEXT_MAX_COLS) cols = UTEXT_MAX_COLS;
    if (rows > UTEXT_MAX_ROWS) rows = UTEXT_MAX_ROWS;
    t->cols = cols;
    t->rows = rows;
    utext_clear(t);
}

static void push_hist(utext_t *t) {
    if (t->hist_n < UTEXT_HIST) {
        copy_row(t->hist[t->hist_n], t->cell[0], t->cols);
        t->hist_n++;
    } else {
        for (int i = 1; i < UTEXT_HIST; i++)
            copy_row(t->hist[i - 1], t->hist[i], t->cols);
        copy_row(t->hist[UTEXT_HIST - 1], t->cell[0], t->cols);
    }
    t->view = 0; /* new output snaps back to the live view */
}

static void scroll_up(utext_t *t) {
    push_hist(t);
    for (int r = 1; r < t->rows; r++)
        copy_row(t->cell[r - 1], t->cell[r], t->cols);
    for (int c = 0; c < t->cols; c++) t->cell[t->rows - 1][c] = ' ';
}

static void newline(utext_t *t) {
    t->cx = 0;
    if (++t->cy >= t->rows) {
        scroll_up(t);
        t->cy = t->rows - 1;
    }
}

void utext_feed(utext_t *t, const char *buf, int n) {
    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            newline(t);
        } else if (c == '\r') {
            t->cx = 0;
        } else if (c == '\b') {
            if (t->cx > 0) t->cx--;
            else if (t->cy > 0) { t->cy--; t->cx = t->cols - 1; }
            t->cell[t->cy][t->cx] = ' ';
        } else if (c == '\t') {
            int next = (t->cx + 4) & ~3;
            while (t->cx < next && t->cx < t->cols) t->cell[t->cy][t->cx++] = ' ';
            if (t->cx >= t->cols) newline(t);
        } else if (c >= 32 && c <= 126) {
            t->cell[t->cy][t->cx++] = c;
            if (t->cx >= t->cols) newline(t);
        }
    }
}

void utext_scroll(utext_t *t, int delta) {
    int v = t->view + delta;
    if (v < 0) v = 0;
    if (v > t->hist_n) v = t->hist_n;
    t->view = v;
}

void utext_draw(const utext_t *t, usurf_t *s, int x, int y,
                uint32_t fg, uint32_t bg, int sx, int sy, int show_caret) {
    char line[UTEXT_MAX_COLS + 1];
    int cols = t->cols;
    /* view=0 → live cells. view=k → the top k visible rows come from hist. */
    int start = t->hist_n - t->view;
    for (int r = 0; r < t->rows; r++) {
        int src = start + r;
        const char *row;
        if (src < 0) {
            for (int c = 0; c < cols; c++) line[c] = ' ';
        } else if (src < t->hist_n) {
            row = t->hist[src];
            for (int c = 0; c < cols; c++) line[c] = row[c];
        } else {
            int cr = src - t->hist_n;
            if (cr >= 0 && cr < t->rows)
                for (int c = 0; c < cols; c++) line[c] = t->cell[cr][c];
            else
                for (int c = 0; c < cols; c++) line[c] = ' ';
        }
        line[cols] = '\0';
        ugfx_text(s, x, y + r * 8 * sy, line, fg, bg, sx, sy);
    }
    if (show_caret && t->view == 0)
        ugfx_fill(s, x + t->cx * 8 * sx, y + t->cy * 8 * sy + 6 * sy, 8 * sx, 2 * sy, fg);
}
