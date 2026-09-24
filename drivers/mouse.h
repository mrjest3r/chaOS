#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/* PS/2 mouse (IRQ12) + optional framebuffer cursor. Roadmap P1.3. */

typedef struct {
    int32_t  x;
    int32_t  y;
    uint32_t buttons; /* bit0=left, bit1=right, bit2=middle */
} mouse_state_t;

void init_mouse(void);

/* Call after fb_init so the software cursor can attach to the LFB. */
void mouse_enable_cursor(void);

/* After a full FB clear, underlay is gone — force cursor redraw. */
void mouse_fb_cleared(void);

/* Hide/show XOR cursor around partial FB paints (line redraw, etc.). */
void mouse_cursor_hide(void);
void mouse_cursor_show(void);
/* 1 if the drawn cursor intersects the rect (fb primitives use this). */
int  mouse_cursor_overlaps(int x, int y, int w, int h);

void mouse_get(mouse_state_t *out);

/* Test hook: feed a raw 3-byte PS/2 packet (same layout as hardware). */
void mouse_inject_packet(uint8_t b0, uint8_t b1, uint8_t b2);

int mouse_available(void);

#endif
