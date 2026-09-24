#include "ugfx.h"
#include "ulib.h"
#include "../kernel/font8x8.h"

int ugfx_create(usurf_t *s, int w, int h) {
    s->px = 0;
    s->w = s->h = 0;
    if (w <= 0 || h <= 0) return 0;
    uint32_t *p = usurface(w, h);
    if (!p) return 0;
    s->px = p;
    s->w = w;
    s->h = h;
    return 1;
}

void ugfx_destroy(usurf_t *s) {
    if (s->px) usurface_free(s->px);
    s->px = 0;
    s->w = s->h = 0;
}

static int clip(const usurf_t *s, int *x, int *y, int *w, int *h) {
    if (!s->px || *w <= 0 || *h <= 0) return 0;
    long long x0 = *x, y0 = *y, x1 = x0 + *w, y1 = y0 + *h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x0 >= x1 || y0 >= y1) return 0;
    *x = (int) x0; *y = (int) y0;
    *w = (int) (x1 - x0); *h = (int) (y1 - y0);
    return 1;
}

void ugfx_clear(usurf_t *s, uint32_t color) {
    if (!s->px) return;
    uint32_t n = (uint32_t) s->w * (uint32_t) s->h;
    uint32_t *p = s->px;
    color &= 0x00FFFFFFu;
    for (uint32_t i = 0; i < n; i++) p[i] = color;
}

void ugfx_fill(usurf_t *s, int x, int y, int w, int h, uint32_t color) {
    if (!clip(s, &x, &y, &w, &h)) return;
    color &= 0x00FFFFFFu;
    uint32_t *row = s->px + (uint32_t) y * (uint32_t) s->w + (uint32_t) x;
    for (int j = 0; j < h; j++, row += s->w)
        for (int i = 0; i < w; i++) row[i] = color;
}

static void glyph(usurf_t *s, int x, int y, char c, uint32_t fg, uint32_t bg,
                  int sx, int sy) {
    unsigned char uc = (unsigned char) c;
    const uint8_t *g = (uc < 32 || uc > 126) ? font8x8[0] : font8x8[uc - 32];
    int cw = 8 * sx, ch = 8 * sy;
    int inside = x >= 0 && y >= 0 && x + cw <= s->w && y + ch <= s->h;
    if (inside) {
        uint32_t *row = s->px + (uint32_t) y * (uint32_t) s->w + (uint32_t) x;
        for (int r = 0; r < 8; r++) {
            uint8_t bits = g[r];
            for (int dy = 0; dy < sy; dy++, row += s->w) {
                uint32_t *d = row;
                for (int col = 0; col < 8; col++) {
                    uint32_t v = (bits & (1u << col)) ? fg : bg;
                    for (int dx = 0; dx < sx; dx++) *d++ = v;
                }
            }
        }
        return;
    }
    for (int r = 0; r < 8; r++) {
        uint8_t bits = g[r];
        for (int col = 0; col < 8; col++)
            ugfx_fill(s, x + col * sx, y + r * sy, sx, sy,
                      (bits & (1u << col)) ? fg : bg);
    }
}

int ugfx_text(usurf_t *s, int x, int y, const char *str,
              uint32_t fg, uint32_t bg, int sx, int sy) {
    if (!s->px || !str || sx < 1 || sy < 1) return 0;
    int x0 = x;
    fg &= 0x00FFFFFFu;
    bg &= 0x00FFFFFFu;
    while (*str) {
        glyph(s, x, y, *str++, fg, bg, sx, sy);
        x += 8 * sx;
    }
    return x - x0;
}

int ugfx_present(const usurf_t *s, int x, int y) {
    if (!s->px) return -1;
    return ufbblit(x, y, s->w, s->h, s->px, s->w);
}

int ugfx_present_rect(const usurf_t *s, int ox, int oy, int x, int y, int w, int h) {
    if (!clip(s, &x, &y, &w, &h)) return -1;
    return ufbblit(ox + x, oy + y, w, h,
                   s->px + (uint32_t) y * (uint32_t) s->w + (uint32_t) x, s->w);
}
