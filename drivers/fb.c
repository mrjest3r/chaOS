#include "fb.h"
#include "../cpu/paging.h"
#include "../cpu/vmm.h"
#include "../cpu/ports.h"
#include "serial.h"
#include "mouse.h"
#include "../kernel/kheap.h"

#include "../kernel/font8x8.h"


#define FONT_SCALE_X  1          /* 8 px wide  — keep ~80–100 columns        */
#define FONT_SCALE_Y  2          /* 16 px tall — larger, more readable glyphs */
#define FB_HIST_LINES 200
#define FB_PDE_INDEX  (FB_VIRT_BASE >> 22)
#define CONSOLE_FG    0x00E0E0E0u
#define CONSOLE_BG    0x00101820u

static int      fb_ok = 0;
static uint32_t fb_w, fb_h, fb_p;
static uint8_t  fb_bytes_pp = 4;
static uint8_t *fb_base = 0;
static uint32_t fb_phys = 0;
static int      fb_pde_count = 0;

static int cons_cols, cons_rows, cons_x, cons_y;
static int cell_w, cell_h;
static char *cells = 0;
static char *hist  = 0;
static int hist_head = 0, hist_count = 0, view_off = 0;
static int input_origin_col = 0, input_origin_row = 0;
static int caret_on = 0;
static int caret_col = 0, caret_row = 0;
static int caret_drawn = 0;

static void map_lfb(uint32_t phys, uint32_t bytes) {
    uint32_t off = 0;
    while (off < bytes) {
        if (vmm_map((uint32_t) kernel_directory,
                    FB_VIRT_BASE + off, phys + off,
                    PAGE_PRESENT | PAGE_RW) != 0) {
            serial_write("[fb] map failed\n");
            return;
        }
        off += PAGE_SIZE;
    }
    fb_pde_count = (int) ((bytes + 0x3FFFFFu) / 0x400000u);
    if (fb_pde_count < 1) fb_pde_count = 1;
    asm volatile("mov %0, %%cr3" : : "r" (kernel_directory) : "memory");
}

/* ---- primitives ----------------------------------------------------------
 * Every rectangle primitive clips once against the screen, then writes whole
 * rows. It also keeps the XOR mouse cursor coherent: if the paint touches the
 * cursor's box the cursor is lifted first and put back after, with IRQs held
 * off for that span so a mouse packet cannot redraw it mid-paint. Callers no
 * longer need mouse_fb_cleared() / hide / show around their drawing. */

typedef struct { uint32_t eflags; int hid; } paint_t;

static paint_t paint_begin(int x, int y, int w, int h) {
    paint_t p = { 0, 0 };
    if (!mouse_cursor_overlaps(x, y, w, h)) return p;
    asm volatile("pushf; pop %0; cli" : "=r" (p.eflags) : : "memory");
    mouse_cursor_hide();
    p.hid = 1;
    return p;
}

static void paint_end(paint_t p) {
    if (!p.hid) return;
    mouse_cursor_show();
    if (p.eflags & 0x200u) asm volatile("sti");
}

/* Clip [x,x+w)×[y,y+h) to the screen. Returns 0 if nothing is left.
 * dx/dy receive how much the origin moved (for source offsets in blits). */
static int clip_rect(int *x, int *y, int *w, int *h, int *dx, int *dy) {
    if (*w <= 0 || *h <= 0) return 0;
    /* 64-bit edges: x/w straight from userspace may be anything. */
    long long x0 = *x, y0 = *y;
    long long x1 = x0 + *w, y1 = y0 + *h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (long long) fb_w) x1 = (long long) fb_w;
    if (y1 > (long long) fb_h) y1 = (long long) fb_h;
    if (x0 >= x1 || y0 >= y1) return 0;
    if (dx) *dx = (int) (x0 - *x);
    if (dy) *dy = (int) (y0 - *y);
    *x = (int) x0; *y = (int) y0;
    *w = (int) (x1 - x0); *h = (int) (y1 - y0);
    return 1;
}

static inline void put_raw(uint8_t *pix, uint32_t rgba) {
    pix[0] = (uint8_t) (rgba);
    pix[1] = (uint8_t) (rgba >> 8);
    pix[2] = (uint8_t) (rgba >> 16);
    if (fb_bytes_pp == 4) pix[3] = 0;
}

/* Fill one clipped row starting at 'row' (pixel pointer) for n pixels. */
static inline void fill_row(uint8_t *row, int n, uint32_t rgba) {
    if (fb_bytes_pp == 4) {
        uint32_t *p = (uint32_t *) row;
        uint32_t v = rgba & 0x00FFFFFFu;
        for (int i = 0; i < n; i++) p[i] = v;
    } else {
        for (int i = 0; i < n; i++, row += 3) put_raw(row, rgba);
    }
}

void fb_put_pixel(int x, int y, uint32_t rgba) {
    if (!fb_ok || x < 0 || y < 0 || (uint32_t) x >= fb_w || (uint32_t) y >= fb_h)
        return;
    put_raw(fb_base + (uint32_t) y * fb_p + (uint32_t) x * fb_bytes_pp, rgba);
}

uint32_t fb_get_pixel(int x, int y) {
    if (!fb_ok || x < 0 || y < 0 || (uint32_t) x >= fb_w || (uint32_t) y >= fb_h)
        return 0;
    uint8_t *pix = fb_base + (uint32_t) y * fb_p + (uint32_t) x * fb_bytes_pp;
    return (uint32_t) pix[0] | ((uint32_t) pix[1] << 8) | ((uint32_t) pix[2] << 16);
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t rgba) {
    if (!fb_ok) return;
    if (!clip_rect(&x, &y, &w, &h, 0, 0)) return;
    paint_t p = paint_begin(x, y, w, h);
    uint8_t *row = fb_base + (uint32_t) y * fb_p + (uint32_t) x * fb_bytes_pp;
    for (int j = 0; j < h; j++, row += fb_p) fill_row(row, w, rgba);
    paint_end(p);
}

void fb_blit(int x, int y, int w, int h, const uint32_t *src, int src_pitch_px) {
    int dx = 0, dy = 0;
    if (!fb_ok || !src || src_pitch_px < w) return;
    if (!clip_rect(&x, &y, &w, &h, &dx, &dy)) return;
    src += (uint32_t) dy * (uint32_t) src_pitch_px + (uint32_t) dx;
    paint_t p = paint_begin(x, y, w, h);
    uint8_t *row = fb_base + (uint32_t) y * fb_p + (uint32_t) x * fb_bytes_pp;
    for (int j = 0; j < h; j++, row += fb_p, src += src_pitch_px) {
        if (fb_bytes_pp == 4) {
            uint32_t *d = (uint32_t *) row;
            for (int i = 0; i < w; i++) d[i] = src[i] & 0x00FFFFFFu;
        } else {
            uint8_t *d = row;
            for (int i = 0; i < w; i++, d += 3) put_raw(d, src[i]);
        }
    }
    paint_end(p);
}

void fb_clear(uint32_t rgba) {
    if (!fb_ok) return;
    /* Fill every byte of every scanline (including pitch padding). Leaving
     * padding untouched is what produced the magenta strip on VirtualBox.
     * No cursor lift needed: the underlay is wiped, so the cursor is simply
     * re-stamped afterwards. */
    uint8_t *row = fb_base;
    int n = (fb_bytes_pp == 4) ? (int) (fb_p / 4) : (int) (fb_p / 3);
    for (uint32_t y = 0; y < fb_h; y++, row += fb_p) fill_row(row, n, rgba);
    mouse_fb_cleared();
}

static void fb_draw_char_xy(int x, int y, char c, uint32_t fg, uint32_t bg,
                            int sx, int sy) {
    if (!fb_ok || sx < 1 || sy < 1) return;
    unsigned char uc = (unsigned char) c;
    const uint8_t *g = (uc < 32 || uc > 126) ? font8x8[0] : font8x8[uc - 32];
    int cw = 8 * sx, ch = 8 * sy;
    int inside = x >= 0 && y >= 0 &&
                 x + cw <= (int) fb_w && y + ch <= (int) fb_h;
    paint_t p = paint_begin(x, y, cw, ch);
    if (inside && fb_bytes_pp == 4) {
        /* Fast path: whole cell on screen, one 32-bit store per pixel. */
        uint32_t f = fg & 0x00FFFFFFu, b = bg & 0x00FFFFFFu;
        uint8_t *row = fb_base + (uint32_t) y * fb_p + (uint32_t) x * 4u;
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            for (int dy = 0; dy < sy; dy++, row += fb_p) {
                uint32_t *d = (uint32_t *) row;
                for (int col = 0; col < 8; col++) {
                    uint32_t v = (bits & (1u << col)) ? f : b;
                    for (int dx = 0; dx < sx; dx++) *d++ = v;
                }
            }
        }
    } else {
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            for (int col = 0; col < 8; col++) {
                uint32_t color = (bits & (1u << col)) ? fg : bg;
                int px = x + col * sx;
                int py = y + r * sy;
                for (int dy = 0; dy < sy; dy++)
                    for (int dx = 0; dx < sx; dx++)
                        fb_put_pixel(px + dx, py + dy, color);
            }
        }
    }
    paint_end(p);
}

void fb_draw_char_scaled(int x, int y, char c, uint32_t fg, uint32_t bg, int scale) {
    fb_draw_char_xy(x, y, c, fg, bg, scale, scale);
}

void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg) {
    fb_draw_char_xy(x, y, c, fg, bg, 1, 1);
}

void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    if (!s) return;
    while (*s) {
        fb_draw_char_xy(x, y, *s++, fg, bg, FONT_SCALE_X, FONT_SCALE_Y);
        x += 8 * FONT_SCALE_X;
    }
}

static void hist_push_line(const char *line) {
    if (!hist) return;
    for (int i = 0; i < cons_cols; i++)
        hist[hist_head * cons_cols + i] = line[i];
    hist_head = (hist_head + 1) % FB_HIST_LINES;
    if (hist_count < FB_HIST_LINES) hist_count++;
}

static void visible_line(int r, char *out) {
    int li = hist_count - view_off + r;
    if (li < hist_count) {
        int slot = hist_head - hist_count + li;
        slot %= FB_HIST_LINES;
        if (slot < 0) slot += FB_HIST_LINES;
        for (int i = 0; i < cons_cols; i++) out[i] = hist[slot * cons_cols + i];
    } else {
        int lr = li - hist_count;
        if (lr < 0 || lr >= cons_rows) {
            for (int i = 0; i < cons_cols; i++) out[i] = ' ';
        } else {
            for (int i = 0; i < cons_cols; i++)
                out[i] = cells[lr * cons_cols + i];
        }
    }
}

static void caret_erase(void) {
    if (!caret_drawn || !fb_ok) return;
    char ch = ' ';
    if (cells && caret_row >= 0 && caret_row < cons_rows &&
        caret_col >= 0 && caret_col < cons_cols)
        ch = cells[caret_row * cons_cols + caret_col];
    if (!ch) ch = ' ';
    fb_draw_char_xy(caret_col * cell_w, caret_row * cell_h, ch,
                    CONSOLE_FG, CONSOLE_BG, FONT_SCALE_X, FONT_SCALE_Y);
    caret_drawn = 0;
}

static void caret_draw(void) {
    if (!caret_on || !fb_ok || view_off != 0) return;
    caret_erase();
    /* Underscore caret in the cell. */
    int x0 = caret_col * cell_w;
    int y0 = caret_row * cell_h + cell_h - 2;
    fb_fill_rect(x0, y0, cell_w - 1, 2, CONSOLE_FG);
    caret_drawn = 1;
}

static void console_redraw(void) {
    char line[160];
    if (!fb_ok || !cells) return;
    if (cons_cols > (int) sizeof(line)) return;
    caret_drawn = 0;
    /* Full clear so margins never keep pink/garbage from a bad LFB scroll. */
    fb_clear(CONSOLE_BG);
    for (int r = 0; r < cons_rows; r++) {
        visible_line(r, line);
        for (int c = 0; c < cons_cols; c++) {
            char ch = line[c] ? line[c] : ' ';
            if (ch == ' ') continue; /* bg already cleared */
            fb_draw_char_xy(c * cell_w, r * cell_h, ch, CONSOLE_FG, CONSOLE_BG,
                            FONT_SCALE_X, FONT_SCALE_Y);
        }
    }
    caret_draw();
}

static void snap_console_live(void) {
    if (view_off != 0) {
        view_off = 0;
        console_redraw();
    }
}

static void console_scroll_up_one(void) {
    hist_push_line(cells);
    for (int r = 1; r < cons_rows; r++)
        for (int c = 0; c < cons_cols; c++)
            cells[(r - 1) * cons_cols + c] = cells[r * cons_cols + c];
    for (int c = 0; c < cons_cols; c++)
        cells[(cons_rows - 1) * cons_cols + c] = ' ';

    /* Fast path: slide visible pixels up (no full-screen blink). Copy only
     * the visible width — pitch may include large right-side padding (VBox).
     * Byte-wide memory_copy over full pitch was the QEMU "long blink". */
    if (view_off == 0) {
        uint32_t shift = (uint32_t) cell_h;
        uint32_t keep  = (uint32_t) ((cons_rows - 1) * cell_h);
        uint32_t row_bytes = fb_w * fb_bytes_pp;
        uint8_t *dst = fb_base;
        uint8_t *src = fb_base + shift * fb_p;
        /* The slide moves whatever the cursor XORed into place; lift it. */
        paint_t p = paint_begin(0, 0, (int) fb_w, (int) fb_h);
        for (uint32_t y = 0; y < keep; y++) {
            uint32_t *s = (uint32_t *) (src + y * fb_p);
            uint32_t *d = (uint32_t *) (dst + y * fb_p);
            uint32_t n = row_bytes / 4;
            for (uint32_t i = 0; i < n; i++) d[i] = s[i];
        }
        fb_fill_rect(0, (cons_rows - 1) * cell_h, cons_cols * cell_w, cell_h, CONSOLE_BG);
        paint_end(p);
    } else {
        console_redraw();
    }
}

static int console_init(void) {
    cell_w = 8 * FONT_SCALE_X;
    cell_h = 8 * FONT_SCALE_Y;
    cons_cols = (int) (fb_w / (uint32_t) cell_w);
    cons_rows = (int) (fb_h / (uint32_t) cell_h);
    if (cons_cols < 1) cons_cols = 1;
    if (cons_rows < 1) cons_rows = 1;

    cells = (char *) malloc((uint32_t) (cons_cols * cons_rows));
    hist  = (char *) malloc((uint32_t) (FB_HIST_LINES * cons_cols));
    if (!cells || !hist) {
        cells = 0; hist = 0;
        serial_write("[fb] console alloc failed\n");
        return -1;
    }
    for (int i = 0; i < cons_cols * cons_rows; i++) cells[i] = ' ';
    hist_head = hist_count = view_off = 0;
    cons_x = cons_y = 0;
    fb_clear(CONSOLE_BG);
    return 0;
}

int fb_console_cols(void) { return cons_cols; }
int fb_console_rows(void) { return cons_rows; }

void fb_console_get_pos(int *col, int *row) {
    if (col) *col = cons_x;
    if (row) *row = cons_y;
}

void fb_console_set_pos(int col, int row) {
    if (!fb_ok) return;
    caret_erase();
    if (col < 0) col = 0;
    if (row < 0) row = 0;
    if (col >= cons_cols) col = cons_cols - 1;
    if (row >= cons_rows) row = cons_rows - 1;
    cons_x = col;
    cons_y = row;
}

void fb_console_mark_input_origin(void) {
    input_origin_col = cons_x;
    input_origin_row = cons_y;
    caret_col = cons_x;
    caret_row = cons_y;
}

void fb_console_get_input_origin(int *col, int *row) {
    if (col) *col = input_origin_col;
    if (row) *row = input_origin_row;
}

void fb_console_caret_show(int on) {
    if (!fb_ok) return;
    if (!on) {
        caret_erase();
        caret_on = 0;
        return;
    }
    caret_on = 1;
    caret_draw();
}

void fb_console_redraw_input(const char *line, int caret) {
    if (!fb_ok || !cells) return;
    snap_console_live();
    mouse_cursor_hide();
    caret_erase();
    if (!line) line = "";
    int len = 0;
    while (line[len]) len++;
    if (caret < 0) caret = 0;
    if (caret > len) caret = len;

    /* Clear from origin far enough to erase previous longer text. */
    int max_clear = cons_cols - input_origin_col;
    if (max_clear < len + 2) max_clear = len + 2;
    if (max_clear > cons_cols * 2) max_clear = cons_cols * 2;

    cons_x = input_origin_col;
    cons_y = input_origin_row;
    for (int i = 0; i < max_clear; i++) {
        if (cons_y >= cons_rows) break;
        cells[cons_y * cons_cols + cons_x] = ' ';
        fb_draw_char_xy(cons_x * cell_w, cons_y * cell_h, ' ',
                        CONSOLE_FG, CONSOLE_BG, FONT_SCALE_X, FONT_SCALE_Y);
        cons_x++;
        if (cons_x >= cons_cols) {
            cons_x = 0;
            cons_y++;
        }
    }

    cons_x = input_origin_col;
    cons_y = input_origin_row;
    for (int i = 0; i < len; i++) {
        char c = line[i];
        if (c < 32 || c > 126) c = '?';
        cells[cons_y * cons_cols + cons_x] = c;
        fb_draw_char_xy(cons_x * cell_w, cons_y * cell_h, c,
                        CONSOLE_FG, CONSOLE_BG, FONT_SCALE_X, FONT_SCALE_Y);
        cons_x++;
        if (cons_x >= cons_cols) {
            cons_x = 0;
            cons_y++;
            if (cons_y >= cons_rows) {
                console_scroll_up_one();
                cons_y = cons_rows - 1;
                input_origin_row--;
                if (input_origin_row < 0) input_origin_row = 0;
            }
        }
    }

    /* Caret screen position = origin + caret index. */
    int cc = input_origin_col + caret;
    int cr = input_origin_row;
    while (cc >= cons_cols) {
        cc -= cons_cols;
        cr++;
    }
    caret_col = cc;
    caret_row = cr;
    cons_x = cc;
    cons_y = cr;
    caret_on = 1;
    caret_draw();
    mouse_cursor_show();
}

int fb_console_click(int px, int py, int *out_col) {
    if (!fb_ok || !cells || cell_w < 1 || cell_h < 1) return -1;
    int col = px / cell_w;
    int row = py / cell_h;
    if (col < 0 || row < 0 || col >= cons_cols || row >= cons_rows) return -1;

    if (view_off != 0) {
        /* Click while scrolled: jump toward live by the row delta. */
        fb_console_scroll_view(-(cons_rows - 1 - row));
        return 0;
    }

    if (row == input_origin_row && col >= input_origin_col) {
        if (out_col) *out_col = col - input_origin_col;
        caret_erase();
        caret_col = col;
        caret_row = row;
        cons_x = col;
        cons_y = row;
        caret_on = 1;
        caret_draw();
        return 1;
    }

    /* Click above the input line → scroll history up a bit. */
    if (row < input_origin_row) {
        fb_console_scroll_view(input_origin_row - row);
        return 0;
    }
    return -1;
}

void fb_console_full_redraw(void) {
    if (!fb_ok) return;
    console_redraw();
}

void fb_console_scroll_view(int lines) {
    if (!fb_ok || !cells || !hist) return;
    int neu = view_off + lines;
    if (neu < 0) neu = 0;
    if (neu > hist_count) neu = hist_count;
    if (neu == view_off) return;
    view_off = neu;
    console_redraw();
}

void fb_console_clear(void) {
    if (!fb_ok) return;
    snap_console_live();
    caret_erase();
    if (cells)
        for (int i = 0; i < cons_rows * cons_cols; i++) cells[i] = ' ';
    cons_x = cons_y = 0;
    input_origin_col = input_origin_row = 0;
    fb_clear(CONSOLE_BG);
}

void fb_console_putc(char c) {
    static int cons_esc = 0;
    if (!fb_ok || !cells) return;
    snap_console_live();
    caret_erase();

    /* Screen editors speak form-feed and a small cursor sequence. Swallow
     * them here so the soft console does not paint them as glyphs. */
    if (cons_esc == 1) {
        cons_esc = (c == '[') ? 2 : 0;
        caret_draw();
        return;
    }
    if (cons_esc == 2) {
        if ((c >= '0' && c <= '9') || c == ';') {
            caret_draw();
            return;
        }
        cons_esc = 0;
        caret_draw();
        return;
    }
    if (c == 27) {
        cons_esc = 1;
        caret_draw();
        return;
    }
    if (c == '\f') {
        cons_esc = 0;
        fb_console_clear();
        caret_col = cons_x;
        caret_row = cons_y;
        caret_draw();
        return;
    }

    if (c == '\n') {
        cons_x = 0;
        cons_y++;
        if (cons_y >= cons_rows) {
            console_scroll_up_one();
            cons_y = cons_rows - 1;
        }
        caret_col = cons_x;
        caret_row = cons_y;
        caret_draw();
        return;
    }
    if (c == '\r') {
        cons_x = 0;
        caret_col = cons_x;
        caret_row = cons_y;
        caret_draw();
        return;
    }
    if (c == '\b') {
        if (cons_x > 0) {
            cons_x--;
            cells[cons_y * cons_cols + cons_x] = ' ';
            fb_draw_char_xy(cons_x * cell_w, cons_y * cell_h, ' ',
                            CONSOLE_FG, CONSOLE_BG, FONT_SCALE_X, FONT_SCALE_Y);
        }
        caret_col = cons_x;
        caret_row = cons_y;
        caret_draw();
        return;
    }
    if (c < 32 || c > 126) c = '?';
    cells[cons_y * cons_cols + cons_x] = c;
    fb_draw_char_xy(cons_x * cell_w, cons_y * cell_h, c,
                    CONSOLE_FG, CONSOLE_BG, FONT_SCALE_X, FONT_SCALE_Y);
    cons_x++;
    if (cons_x >= cons_cols) {
        cons_x = 0;
        cons_y++;
        if (cons_y >= cons_rows) {
            console_scroll_up_one();
            cons_y = cons_rows - 1;
        }
    }
    caret_col = cons_x;
    caret_row = cons_y;
    caret_draw();
}

void fb_console_write(const char *s) {
    if (!s) return;
    while (*s) fb_console_putc(*s++);
}

#define VBE_DISPI_INDEX 0x01CE
#define VBE_DISPI_DATA  0x01CF

static void dispi_write(uint16_t index, uint16_t value) {
    port_word_out(VBE_DISPI_INDEX, index);
    port_word_out(VBE_DISPI_DATA, value);
}

static uint16_t dispi_read(uint16_t index) {
    port_word_out(VBE_DISPI_INDEX, index);
    return port_word_in(VBE_DISPI_DATA);
}

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off) {
    uint32_t addr = 0x80000000u
                  | ((uint32_t) bus << 16)
                  | ((uint32_t) slot << 11)
                  | ((uint32_t) func << 8)
                  | (off & 0xFCu);
    port_dword_out(0xCF8, addr);
    return port_dword_in(0xCFC);
}

/* First display-class device's BAR0 (QEMU stdvga / VirtualBox VGA). */
static uint32_t pci_find_fb_bar(void) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        uint32_t id = pci_read(0, slot, 0, 0);
        if ((id & 0xFFFFu) == 0xFFFFu) continue;
        uint32_t classreg = pci_read(0, slot, 0, 0x08);
        if ((classreg >> 24) != 0x03) continue; /* display controller */
        uint32_t bar = pci_read(0, slot, 0, 0x10);
        if (bar & 1u) continue;                 /* I/O BAR */
        return bar & ~0xFu;
    }
    return 0;
}

/* VirtualBox: never use Bochs DISPI (breaks VBoxVGA / VMSVGA paint).
 * Detect via PCI vendor 0x80EE (VGA or Guest Additions) or CPUID HV leaf. */
static int pci_is_virtualbox(void) {
    for (uint8_t bus = 0; bus < 2; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_read(bus, slot, 0, 0);
            if ((id & 0xFFFFu) == 0x80EEu) return 1; /* InnoTek / Oracle */
        }
    }
    uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
    asm volatile("cpuid"
                 : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0x40000000u));
    /* "VBoxVBoxVBox" in ebx/ecx/edx */
    if (ebx == 0x786f4256u && ecx == 0x786f4256u && edx == 0x786f4256u)
        return 1;
    return 0;
}

static void serial_u32(uint32_t p) {
    char buf[16];
    int i = 0;
    if (p == 0) {
        buf[i++] = '0';
    } else {
        char tmp[12];
        int n = 0;
        while (p) { tmp[n++] = (char) ('0' + (p % 10)); p /= 10; }
        while (n) buf[i++] = tmp[--n];
    }
    buf[i] = 0;
    serial_write(buf);
}

static void serial_hex32(uint32_t p) {
    static const char *hexd = "0123456789abcdef";
    char buf[9];
    for (int i = 0; i < 8; i++)
        buf[i] = hexd[(p >> (28 - i * 4)) & 0xF];
    buf[8] = 0;
    serial_write(buf);
}

/* Force 800x600x32 via Bochs DISPI (QEMU / non-VBox only).
 * QEMU zeroes VIRT_WIDTH on ENABLE — set virt after enable.
 * VirtualBox VBoxVGA must NOT use DISPI: virt-width experiments caused
 * stretch, striping, and half-screen smash. Use BIOS VBE there. */
static int bochs_vbe_force_32(uint16_t w, uint16_t h) {
    uint16_t id = dispi_read(0);
    if (id < 0xB0C0 || id > 0xB0C5) return -1;

    dispi_write(4, 0);
    dispi_write(1, w);
    dispi_write(2, h);
    dispi_write(3, 32);
    dispi_write(4, 0x41);           /* enable | LFB */
    dispi_write(6, w);              /* virt = visible, after enable */
    dispi_write(7, h);
    dispi_write(8, 0);
    dispi_write(9, 0);

    uint16_t xr = dispi_read(1);
    uint16_t vh = dispi_read(2);
    uint16_t vw = dispi_read(6);
    if (xr >= 640 && xr <= 1920) w = xr;
    if (vh < h) vh = h;
    if (vw < w) vw = w;

    fb_w = w;
    fb_h = vh;
    fb_bytes_pp = 4;
    fb_p = (uint32_t) vw * 4;
    if (fb_p < fb_w * 4) fb_p = fb_w * 4;

    serial_write("[fb] DISPI 32bpp pitch=");
    serial_u32(fb_p);
    serial_write("\n");
    return 0;
}

static int fb_from_bios(const fb_boot_info_t *info) {
    if (!(info->magic == FB_MAGIC && info->ok && info->bpp >= 32 &&
          info->width && info->height && info->phys_base))
        return -1;
    fb_w = info->width;
    fb_h = info->height;
    fb_phys = info->phys_base;
    fb_bytes_pp = 4;
    fb_p = info->pitch ? info->pitch : (fb_w * 4);
    if (fb_p < fb_w * 4) fb_p = fb_w * 4;
    return 0;
}

int fb_init(void) {
    fb_boot_info_t *info = (fb_boot_info_t *) FB_INFO_ADDR;
    uint32_t pci_bar = pci_find_fb_bar();
    int vbox = pci_is_virtualbox();
    int used_dispi = 0;

    if (vbox) {
        serial_write("[fb] VirtualBox VGA — BIOS VBE only (no DISPI)\n");
        if (fb_from_bios(info) != 0) {
            serial_write("[fb] no 32bpp VBE — VGA text console\n");
            fb_ok = 0;
            return -1;
        }
    } else if (fb_from_bios(info) == 0) {
        serial_write("[fb] BIOS VBE 32bpp\n");
    } else if (bochs_vbe_force_32(800, 600) == 0) {
        used_dispi = 1;
        fb_phys = info->phys_base ? info->phys_base : 0;
        if (!fb_phys) fb_phys = pci_bar;
        if (!fb_phys) fb_phys = 0xE0000000u;
    } else {
        serial_write("[fb] VBE not available - staying in text mode\n");
        fb_ok = 0;
        return -1;
    }

    if (used_dispi && pci_bar && fb_phys != pci_bar) {
        fb_phys = pci_bar;
        serial_write("[fb] using PCI VGA BAR for LFB\n");
    }

    serial_write("[fb] ");
    serial_u32(fb_w);
    serial_write("x");
    serial_u32(fb_h);
    serial_write(" pitch=");
    serial_u32(fb_p);
    serial_write(" phys=0x");
    serial_hex32(fb_phys);
    serial_write("\n");

    fb_base = (uint8_t *) FB_VIRT_BASE;
    fb_ok = 1;
    {
        uint32_t need = fb_p * fb_h + fb_p * 16;
        if (need < 4u * 1024u * 1024u) need = 4u * 1024u * 1024u;
        map_lfb(fb_phys, need);
    }

    {
        uint32_t *first = (uint32_t *) fb_base;
        uint32_t *last  = (uint32_t *) (fb_base + (fb_h - 1) * fb_p);
        uint32_t a = 0xA5A5A5A5u, b = 0x5A5A5A5Au;
        *first = a;
        *last = b;
        if (*first != a || *last != b) {
            serial_write("[fb] LFB probe failed — VGA text console\n");
            fb_ok = 0;
            return -1;
        }
        *first = 0;
        *last = 0;
    }

    if (console_init() != 0) {
        fb_ok = 0;
        return -1;
    }
    serial_write("[fb] LFB ready\n");
    return 0;
}

int fb_available(void) { return fb_ok; }
uint32_t fb_width(void)  { return fb_w; }
uint32_t fb_height(void) { return fb_h; }
uint32_t fb_pitch(void)  { return fb_p; }
uint8_t *fb_ptr(void)    { return fb_base; }

void fb_share_into(uint32_t *pd) {
    if (!fb_ok || !pd) return;
    for (int i = 0; i < fb_pde_count; i++)
        pd[FB_PDE_INDEX + i] = kernel_directory[FB_PDE_INDEX + i];
}

int fb_is_shared_pde(int pdi) {
    if (!fb_ok) return 0;
    return pdi >= (int) FB_PDE_INDEX && pdi < (int) FB_PDE_INDEX + fb_pde_count;
}
