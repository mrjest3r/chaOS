#ifndef FB_H
#define FB_H

#include <stdint.h>

/* Linear-framebuffer graphics (roadmap P1.2). The bootloader's real-mode VBE
 * stub (boot/vbe.asm) leaves a compact info block at 0x5000; fb_init maps the
 * LFB into the kernel at FB_VIRT_BASE and optionally takes over the text
 * console so kprint keeps working after VGA text mode is gone. */

#define FB_INFO_ADDR  0x5000u
#define FB_VIRT_BASE  0xE0000000u
#define FB_MAGIC      0x42464843u  /* 'CHFB' */

typedef struct {
    uint32_t magic;
    uint16_t width;
    uint16_t height;
    uint8_t  bpp;
    uint8_t  ok;
    uint16_t pitch;
    uint32_t phys_base;
} __attribute__((packed)) fb_boot_info_t;

/* Returns 0 on success (graphics mode active), -1 to keep VGA text mode. */
int  fb_init(void);
int  fb_available(void);

uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch(void);
uint8_t *fb_ptr(void);                 /* virtual address of the LFB */

/* Primitives clip to the screen and keep the mouse cursor coherent
 * themselves; callers need no mouse_fb_cleared() / hide / show. */
void fb_put_pixel(int x, int y, uint32_t rgba);
uint32_t fb_get_pixel(int x, int y);
void fb_fill_rect(int x, int y, int w, int h, uint32_t rgba);
/* Copy a w×h block of 0x00RRGGBB pixels from 'src' (row stride in pixels). */
void fb_blit(int x, int y, int w, int h, const uint32_t *src, int src_pitch_px);
void fb_clear(uint32_t rgba);
void fb_draw_char(int x, int y, char c, uint32_t fg, uint32_t bg);
void fb_draw_string(int x, int y, const char *s, uint32_t fg, uint32_t bg);

/* Soft VGA-style text console rendered onto the framebuffer. Used by screen.c
 * when graphics mode is active so the existing shell keeps working. */
void fb_console_clear(void);
void fb_console_putc(char c);
void fb_console_write(const char *s);
void fb_console_scroll_view(int lines); /* PgUp/PgDn: >0 older, <0 toward live */
int  fb_console_cols(void);
int  fb_console_rows(void);
void fb_console_get_pos(int *col, int *row);
void fb_console_set_pos(int col, int row);
void fb_console_mark_input_origin(void); /* prompt end = start of editable line */
void fb_console_get_input_origin(int *col, int *row);
void fb_console_caret_show(int on);
void fb_console_redraw_input(const char *line, int caret);
/* Pixel click → text cell. Returns 1 if on the live input line (col set),
 * 0 if scrollback was adjusted, -1 if ignored. */
int  fb_console_click(int px, int py, int *out_col);
void fb_console_full_redraw(void); /* restore soft console after GUI takeover */

void fb_draw_char_scaled(int x, int y, char c, uint32_t fg, uint32_t bg, int scale);

/* Share the LFB page-directory entries into a new address space so ring-0
 * kprint still works while a user CR3 is loaded. */
void fb_share_into(uint32_t *pd);
int  fb_is_shared_pde(int pdi);

#endif
