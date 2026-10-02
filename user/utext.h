#ifndef UTEXT_H
#define UTEXT_H

#include <stdint.h>
#include "ugfx.h"

/* Text-grid widget (GUI G4/G5): a small terminal screen that consumes a byte
 * stream and draws itself into a surface with the shared 8x8 font. No heap
 * in userspace, so cells are fixed-size.
 * Bytes: '\n', '\r', '\b', tab, printable ASCII, '\f' (clear the live grid
 * and home the caret; history stays), and ESC [ row ; col H to place the
 * caret (1-based). ESC [ n D / C move the caret without erasing. ESC [ n S
 * looks back through history and ESC [ n T toward the live view. Rows that
 * scroll off the top go into a history ring; utext_scroll does the same. */

#define UTEXT_MAX_COLS 64
#define UTEXT_MAX_ROWS 32
#define UTEXT_HIST     48   /* lines remembered above the live view         */

typedef struct {
    int  cols, rows;
    int  cx, cy;                                   /* caret cell           */
    int  hist_n;                                   /* lines in history     */
    int  view;                                     /* 0 = live; >0 scrolled*/
    char cell[UTEXT_MAX_ROWS][UTEXT_MAX_COLS];
    char hist[UTEXT_HIST][UTEXT_MAX_COLS];
    int  esc_st;   /* 0 none, 1 saw ESC, 2 saw ESC [ */
    int  esc_a, esc_b;
    int  esc_semi;
} utext_t;

void utext_init(utext_t *t, int cols, int rows);  /* clamps to the maxima  */
void utext_clear(utext_t *t);
void utext_feed(utext_t *t, const char *buf, int n);
/* +delta looks further back; -delta toward the live view. */
void utext_scroll(utext_t *t, int delta);
/* Draw at pixel (x, y) in 's', each cell 8*sx by 8*sy; caret as a block. */
void utext_draw(const utext_t *t, usurf_t *s, int x, int y,
                uint32_t fg, uint32_t bg, int sx, int sy, int show_caret);

#endif
