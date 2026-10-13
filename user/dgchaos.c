#include "ulib.h"
#include "../kernel/fb_user.h"

/* Doom's platform hooks. The engine owns DG_ScreenBuffer (640×400, 0x00RRGGBB)
 * and calls these. Key-up comes from SYS_KEYEVT; the character ring has no release. */

#define DG_W 640
#define DG_H 400

#define KEY_RIGHTARROW 0xae
#define KEY_LEFTARROW  0xac
#define KEY_UPARROW    0xad
#define KEY_DOWNARROW  0xaf
#define KEY_USE        0xa2
#define KEY_FIRE       0xa3
#define KEY_ESCAPE     27
#define KEY_ENTER      13
#define KEY_TAB        9
#define KEY_BACKSPACE  0x7f
#define KEY_RSHIFT     (0x80 + 0x36)

extern uint32_t *DG_ScreenBuffer;

static int scr_w = 800;
static int scr_h = 600;

/* Set-1 scancode to the key byte Doom's input layer expects. */
static unsigned char map_scan(unsigned char sc) {
    static const char letters[] = {
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0x7f, 9,
        'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 13, 0,
        'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\',
        'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, 0, 0, ' '
    };
    if (sc == 0x1D) return (unsigned char) KEY_FIRE;
    if (sc == 0x2A || sc == 0x36) return (unsigned char) KEY_RSHIFT;
    if (sc == 0x39) return (unsigned char) KEY_USE;
    if (sc == 0x48) return (unsigned char) KEY_UPARROW;
    if (sc == 0x50) return (unsigned char) KEY_DOWNARROW;
    if (sc == 0x4B) return (unsigned char) KEY_LEFTARROW;
    if (sc == 0x4D) return (unsigned char) KEY_RIGHTARROW;
    if (sc < sizeof letters && letters[sc]) return (unsigned char) letters[sc];
    return 0;
}

void DG_Init(void) {
    fb_req_t r;
    unsigned char *p = (unsigned char *) &r;
    for (unsigned i = 0; i < sizeof r; i++) p[i] = 0;
    r.op = FBOP_INFO;
    if (ufb(&r) == 0 && r.ok) {
        scr_w = (int) r.width;
        scr_h = (int) r.height;
    }
}

static int mouse_seen;
static int mouse_x, mouse_y, mouse_btn;

static int frame_x, frame_y;

static void publish_frame(void) {
    frame_req_t r;
    unsigned char *p = (unsigned char *) &r;
    for (unsigned i = 0; i < sizeof r; i++) p[i] = 0;
    r.op = FRAME_PUBLISH;
    r.ptr = (uint32_t) DG_ScreenBuffer;
    r.w = DG_W;
    r.h = DG_H;
    uframe(&r);
    r.op = FRAME_QUERY;
    if (uframe(&r) == 0) {
        frame_x = r.x;
        frame_y = r.y;
    }
}

static void paint_cursor(void) {
    int x = mouse_x - frame_x;
    int y = mouse_y - frame_y;
    if (x < 2 || y < 2 || x >= DG_W - 2 || y >= DG_H - 2) return;
    for (int i = -4; i <= 4; i++) {
        DG_ScreenBuffer[y * DG_W + (x + i)] = 0x00FFFFFFu;
        DG_ScreenBuffer[(y + i) * DG_W + x] = 0x00FFFFFFu;
    }
    DG_ScreenBuffer[y * DG_W + x] = 0x00FF3030u;
}

void DG_DrawFrame(void) {
    if (!DG_ScreenBuffer) return;
    publish_frame();
    paint_cursor();
}

int DG_GetMouse(int *buttons, int *dx, int *dy) {
    umouse_t m;
    if (umouse(&m) != 0) return 0;
    if (!mouse_seen) {
        mouse_seen = 1;
        mouse_x = m.x;
        mouse_y = m.y;
        mouse_btn = (int) (m.buttons & 7u);
        return 0;
    }
    int mx = m.x - mouse_x;
    int my = m.y - mouse_y;
    int btn = (int) (m.buttons & 7u);
    mouse_x = m.x;
    mouse_y = m.y;
    int inside = mouse_x >= frame_x && mouse_y >= frame_y &&
                 mouse_x < frame_x + DG_W && mouse_y < frame_y + DG_H;
    if (!inside) btn = 0;
    int edge = btn != mouse_btn;
    mouse_btn = btn;
    if (!inside && !edge) return 0;
    if (!mx && !my && !edge) return 0;
    if (buttons) *buttons = btn;
    if (dx) *dx = inside ? mx * 3 : 0;
    if (dy) *dy = inside ? -my * 3 : 0;
    return 1;
}

void DG_SleepMs(uint32_t ms) {
    /* The timer is 50 Hz, so one tick is 20 ms. */
    int ticks = (int) (ms / 20u);
    if (ms > 0 && ticks < 1) ticks = 1;
    if (ticks > 0) usleep(ticks);
}

uint32_t DG_GetTicksMs(void) {
    return uuptime() * 20u;
}

int DG_GetKey(int *pressed, unsigned char *key) {
    for (;;) {
        int ev = ukeyevt();
        if (ev < 0) return 0;
        unsigned char k = map_scan((unsigned char) ev);
        if (!k) continue;
        if (pressed) *pressed = (ev >> 8) & 1;
        if (key) *key = k;
        return 1;
    }
}

void DG_SetWindowTitle(const char *title) {
    (void) title;
}
