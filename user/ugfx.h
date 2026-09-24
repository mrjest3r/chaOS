#ifndef UGFX_H
#define UGFX_H

#include <stdint.h>

/* Userspace software rendering into a SYS_SURFACE buffer (GUI G2).
 * Draw locally, then present once with ugfx_present() -> one FBOP_BLIT. */

typedef struct {
    uint32_t *px;   /* w*h pixels, 0x00RRGGBB, row-major, pitch == w */
    int w, h;
} usurf_t;

/* Allocate a w×h surface from the kernel. Returns 0 on failure. */
int  ugfx_create(usurf_t *s, int w, int h);
void ugfx_destroy(usurf_t *s);

void ugfx_clear(usurf_t *s, uint32_t color);
void ugfx_fill(usurf_t *s, int x, int y, int w, int h, uint32_t color);
/* 8x8 glyphs scaled sx*sy; returns advance in px. */
int  ugfx_text(usurf_t *s, int x, int y, const char *str,
               uint32_t fg, uint32_t bg, int sx, int sy);
/* Copy the whole surface to the screen at (x, y). */
int  ugfx_present(const usurf_t *s, int x, int y);
/* Copy a sub-rectangle of the surface to the same screen offset (+ox,+oy). */
int  ugfx_present_rect(const usurf_t *s, int ox, int oy, int x, int y, int w, int h);

#endif
