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
    t->esc_st = 0;
    t->esc_a = t->esc_b = 0;
    t->esc_semi = 0;
}

static void clear_live(utext_t *t) {
    for (int r = 0; r < t->rows; r++)
        for (int c = 0; c < t->cols; c++) t->cell[r][c] = ' ';
    t->cx = t->cy = 0;
    t->view = 0;
    t->esc_st = 0;
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

static void move_cells(utext_t *t, int delta) {
    int pos = t->cy * t->cols + t->cx + delta;
    int max = t->rows * t->cols - 1;
    if (pos < 0) pos = 0;
    if (pos > max) pos = max;
    t->cy = pos / t->cols;
    t->cx = pos % t->cols;
}

static void newline(utext_t *t) {
    t->cx = 0;
    if (++t->cy >= t->rows) {
        scroll_up(t);
        t->cy = t->rows - 1;
    }
}

static void feed_byte(utext_t *t, unsigned char c) {
    if (t->esc_st == 1) {
        if (c == '[') {
            t->esc_st = 2;
            t->esc_a = t->esc_b = 0;
            t->esc_semi = 0;
        } else {
            t->esc_st = 0;
        }
        return;
    }
    if (t->esc_st == 2) {
        if (c >= '0' && c <= '9') {
            int *d = t->esc_semi ? &t->esc_b : &t->esc_a;
            *d = *d * 10 + (int) (c - '0');
            if (*d > 99) *d = 99;
            return;
        }
        if (c == ';') {
            t->esc_semi = 1;
            return;
        }
        if (c == 'H' || c == 'f') {
            int row = t->esc_a > 0 ? t->esc_a - 1 : 0;
            int col = t->esc_b > 0 ? t->esc_b - 1 : 0;
            if (row >= t->rows) row = t->rows - 1;
            if (row < 0) row = 0;
            if (col >= t->cols) col = t->cols - 1;
            if (col < 0) col = 0;
            t->cy = row;
            t->cx = col;
            t->view = 0;
        } else if (c == 'J' && t->esc_a == 2) {
            clear_live(t);
        } else if (c == 'D' || c == 'C') {
            int n = t->esc_a > 0 ? t->esc_a : 1;
            move_cells(t, c == 'C' ? n : -n);
        } else if (c == 'S' || c == 'T') {
            int n = t->esc_a > 0 ? t->esc_a : 1;
            utext_scroll(t, c == 'S' ? n : -n);
        }
        t->esc_st = 0;
        return;
    }
    if (c == 27) {
        t->esc_st = 1;
        return;
    }
    if (c == '\f') {
        clear_live(t);
        return;
    }
    if (c == '\n') {
        t->view = 0;
        newline(t);
    } else if (c == '\r') {
        t->view = 0;
        t->cx = 0;
    } else if (c == '\b') {
        t->view = 0;
        if (t->cx > 0) t->cx--;
        else if (t->cy > 0) { t->cy--; t->cx = t->cols - 1; }
        t->cell[t->cy][t->cx] = ' ';
    } else if (c == '\t') {
        int next = (t->cx + 4) & ~3;
        while (t->cx < next && t->cx < t->cols) t->cell[t->cy][t->cx++] = ' ';
        if (t->cx >= t->cols) newline(t);
    } else if (c >= 32 && c <= 126) {
        t->view = 0;
        t->cell[t->cy][t->cx++] = c;
        if (t->cx >= t->cols) newline(t);
    }
}

void utext_feed(utext_t *t, const char *buf, int n) {
    for (int i = 0; i < n; i++) feed_byte(t, (unsigned char) buf[i]);
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
