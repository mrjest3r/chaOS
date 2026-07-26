#include "screen.h"
#include "../cpu/ports.h"
#include "../libc/mem.h"
#include "../kernel/kheap.h"
#include "serial.h"
#include <stdint.h>

/* Declaration of private functions */
int get_cursor_offset();
void set_cursor_offset(int offset);
int print_char(char c, int col, int row, char attr);
int get_offset(int col, int row);
int get_offset_row(int offset);
int get_offset_col(int offset);

/* ---- scrollback history --------------------------------------------------
 * A ring buffer of the last HISTORY_LINES lines that scrolled off the top of
 * the screen (characters + attributes, one VGA row each). It lives on the
 * kernel heap, so scrollback only starts working after screen_history_init();
 * before that the driver behaves exactly like it always did. */
#define HISTORY_LINES 200
#define ROW_BYTES     (MAX_COLS * 2)

static uint8_t *history   = 0;  /* HISTORY_LINES rows of ROW_BYTES           */
static int hist_head      = 0;  /* next slot to overwrite                    */
static int hist_count     = 0;  /* how many rows are valid                   */
static int view_offset    = 0;  /* 0 = live screen, N = scrolled up N lines  */
static uint8_t *saved_live = 0; /* copy of the live screen while scrolled    */

void screen_history_init() {
    history    = (uint8_t *) malloc(HISTORY_LINES * ROW_BYTES);
    saved_live = (uint8_t *) malloc(MAX_ROWS * ROW_BYTES);
    if (!history || !saved_live) { history = 0; saved_live = 0; }
}

/* Save the top row into the history ring (called just before it scrolls off). */
static void history_push_top_row() {
    if (!history) return;
    memory_copy((uint8_t *) VIDEO_ADDRESS, history + hist_head * ROW_BYTES, ROW_BYTES);
    hist_head = (hist_head + 1) % HISTORY_LINES;
    if (hist_count < HISTORY_LINES) hist_count++;
}

/* Redraw the viewport for the current view_offset. Visible lines are taken
 * from the tail of [history rows..., saved live rows...]. */
static void render_view() {
    for (int r = 0; r < MAX_ROWS; r++) {
        int li = hist_count - view_offset + r; /* index into the combined list */
        uint8_t *dst = (uint8_t *) VIDEO_ADDRESS + r * ROW_BYTES;
        if (li < hist_count) {
            int slot = (hist_head - hist_count + li + 2 * HISTORY_LINES) % HISTORY_LINES;
            memory_copy(history + slot * ROW_BYTES, dst, ROW_BYTES);
        } else {
            memory_copy(saved_live + (li - hist_count) * ROW_BYTES, dst, ROW_BYTES);
        }
    }
}

void screen_scroll_view(int lines) {
    if (!history) return;

    int new_offset = view_offset + lines;
    if (new_offset < 0) new_offset = 0;
    if (new_offset > hist_count) new_offset = hist_count;
    if (new_offset == view_offset) return;

    if (view_offset == 0) {
        /* Entering scrollback: remember what the live screen looks like. */
        memory_copy((uint8_t *) VIDEO_ADDRESS, saved_live, MAX_ROWS * ROW_BYTES);
    }

    view_offset = new_offset;

    if (view_offset == 0) {
        /* Back to live output. */
        memory_copy(saved_live, (uint8_t *) VIDEO_ADDRESS, MAX_ROWS * ROW_BYTES);
        return;
    }

    render_view();
}

/* New output always lands on the live screen, so leave scrollback first. */
static void snap_to_live() {
    if (view_offset > 0) screen_scroll_view(-view_offset);
}

/**********************************************************
 * Public Kernel API functions                            *
 **********************************************************/

/**
 * Print a message on the specified location
 * If col, row, are negative, we will use the current offset
 */
void kprint_at(char *message, int col, int row) {
    snap_to_live();

    /* Set cursor if col/row are negative */
    int offset;
    if (col >= 0 && row >= 0)
        offset = get_offset(col, row);
    else {
        offset = get_cursor_offset();
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }

    /* Loop through message and print it */
    int i = 0;
    while (message[i] != 0) {
        offset = print_char(message[i++], col, row, WHITE_ON_BLACK);
        /* Compute row/col for next iteration */
        row = get_offset_row(offset);
        col = get_offset_col(offset);
    }
}

void kprint(char *message) {
    /* Mirror all kernel output to the serial port for headless debugging. */
    serial_write(message);
    kprint_at(message, -1, -1);
}

void kprint_backspace() {
    snap_to_live();
    int offset = get_cursor_offset()-2;
    int row = get_offset_row(offset);
    int col = get_offset_col(offset);
    print_char(0x08, col, row, WHITE_ON_BLACK);
}


/**********************************************************
 * Private kernel functions                               *
 **********************************************************/


/**
 * Innermost print function for our kernel, directly accesses the video memory 
 *
 * If 'col' and 'row' are negative, we will print at current cursor location
 * If 'attr' is zero it will use 'white on black' as default
 * Returns the offset of the next character
 * Sets the video cursor to the returned offset
 */
int print_char(char c, int col, int row, char attr) {
    uint8_t *vidmem = (uint8_t*) VIDEO_ADDRESS;
    if (!attr) attr = WHITE_ON_BLACK;

    /* Error control: print a red 'E' if the coords aren't right */
    if (col >= MAX_COLS || row >= MAX_ROWS) {
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-2] = 'E';
        vidmem[2*(MAX_COLS)*(MAX_ROWS)-1] = RED_ON_WHITE;
        return get_offset(col, row);
    }

    int offset;
    if (col >= 0 && row >= 0) offset = get_offset(col, row);
    else offset = get_cursor_offset();

    if (c == '\n') {
        row = get_offset_row(offset);
        offset = get_offset(0, row+1);
    } else if (c == 0x08) { /* Backspace */
        vidmem[offset] = ' ';
        vidmem[offset+1] = attr;
    } else {
        vidmem[offset] = c;
        vidmem[offset+1] = attr;
        offset += 2;
    }

    /* Check if the offset is over screen size and scroll */
    if (offset >= MAX_ROWS * MAX_COLS * 2) {
        history_push_top_row(); /* keep the vanishing top line for scrollback */
        int i;
        for (i = 1; i < MAX_ROWS; i++) 
            memory_copy((uint8_t*)(get_offset(0, i) + VIDEO_ADDRESS),
                        (uint8_t*)(get_offset(0, i-1) + VIDEO_ADDRESS),
                        MAX_COLS * 2);

        /* Blank last line */
        char *last_line = (char*) (get_offset(0, MAX_ROWS-1) + (uint8_t*) VIDEO_ADDRESS);
        for (i = 0; i < MAX_COLS * 2; i++) last_line[i] = 0;

        offset -= 2 * MAX_COLS;
    }

    set_cursor_offset(offset);
    return offset;
}

int get_cursor_offset() {
    /* Use the VGA ports to get the current cursor position
     * 1. Ask for high byte of the cursor offset (data 14)
     * 2. Ask for low byte (data 15)
     */
    port_byte_out(REG_SCREEN_CTRL, 14);
    int offset = port_byte_in(REG_SCREEN_DATA) << 8; /* High byte: << 8 */
    port_byte_out(REG_SCREEN_CTRL, 15);
    offset += port_byte_in(REG_SCREEN_DATA);
    return offset * 2; /* Position * size of character cell */
}

void set_cursor_offset(int offset) {
    /* Similar to get_cursor_offset, but instead of reading we write data */
    offset /= 2;
    port_byte_out(REG_SCREEN_CTRL, 14);
    port_byte_out(REG_SCREEN_DATA, (uint8_t)(offset >> 8));
    port_byte_out(REG_SCREEN_CTRL, 15);
    port_byte_out(REG_SCREEN_DATA, (uint8_t)(offset & 0xff));
}

void clear_screen() {
    snap_to_live();
    int screen_size = MAX_COLS * MAX_ROWS;
    int i;
    uint8_t *screen = (uint8_t*) VIDEO_ADDRESS;

    for (i = 0; i < screen_size; i++) {
        screen[i*2] = ' ';
        screen[i*2+1] = WHITE_ON_BLACK;
    }
    set_cursor_offset(get_offset(0, 0));
}


int get_offset(int col, int row) { return 2 * (row * MAX_COLS + col); }
int get_offset_row(int offset) { return offset / (2 * MAX_COLS); }
int get_offset_col(int offset) { return (offset - (get_offset_row(offset)*2*MAX_COLS))/2; }
