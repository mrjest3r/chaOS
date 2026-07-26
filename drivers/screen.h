#ifndef SCREEN_H
#define SCREEN_H

#define VIDEO_ADDRESS 0xb8000
#define MAX_ROWS 25
#define MAX_COLS 80
#define WHITE_ON_BLACK 0x0f
#define RED_ON_WHITE 0xf4

/* Screen i/o ports */
#define REG_SCREEN_CTRL 0x3d4
#define REG_SCREEN_DATA 0x3d5

/* Public kernel API */
void clear_screen();
void kprint_at(char *message, int col, int row);
void kprint(char *message);
void kprint_backspace();

/* ---- scrollback -----------------------------------------------------------
 * Lines that scroll off the top are kept in a heap-allocated history buffer.
 * PgUp/PgDn (see the keyboard driver) move a viewport over that history;
 * any new output snaps the view back to the live screen. */
void screen_history_init();          /* call once the kernel heap is up      */
void screen_scroll_view(int lines);  /* >0 scrolls up (older), <0 back down  */

#endif
