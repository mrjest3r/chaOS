#include "ulib.h"

/* Roadmap P1.3 / G3: event-driven pointer demo. Blocks in SYS_EVENT_WAIT
 * (no polling, no CPU while idle) and prints each key / mouse event it gets.
 * Quits after MAX_EVENTS events, on 'q', or after IDLE_TICKS without input.
 * The tally is written to evdemo.txt so the AUTOTEST can check delivery. */

#define MAX_EVENTS 40
#define IDLE_TICKS 500   /* ~10 s */

static void print_pos(const uevent_t *ev) {
    uprint("  x=");
    uprint_dec((uint32_t) (ev->x < 0 ? 0 : ev->x));
    uprint(" y=");
    uprint_dec((uint32_t) (ev->y < 0 ? 0 : ev->y));
    uprint(" buttons=");
    uprint_dec(ev->buttons);
    uprint("\n");
}

static void tally(int mouse, int keys) {
    char buf[32];
    int n = 0;
    buf[n++] = 'm'; buf[n++] = '=';
    if (mouse > 9) mouse = 9;
    if (keys > 9) keys = 9;
    buf[n++] = (char) ('0' + mouse);
    buf[n++] = ' '; buf[n++] = 'k'; buf[n++] = '=';
    buf[n++] = (char) ('0' + keys);
    buf[n] = '\0';
    uwritefile("evdemo.txt", buf, n);
}

void main(void) {
    umouse_t m;
    if (umouse(&m) != 0) {
        uprint("mousedemo: mouse not available\n");
        uexit();
    }
    uprint("mousedemo: waiting for events (move/click, keys; q quits)\n");

    int mouse = 0, keys = 0;
    for (int i = 0; i < MAX_EVENTS; i++) {
        uevent_t ev;
        int r = uevent_wait(&ev, IDLE_TICKS);
        if (r < 0) {
            uprint("mousedemo: SYS_EVENT_WAIT failed\n");
            break;
        }
        if (r == 0) {
            uprint("mousedemo: idle, quitting\n");
            break;
        }
        if (ev.type == UEV_MOUSE) {
            mouse++;
            print_pos(&ev);
        } else if (ev.type == UEV_KEY) {
            keys++;
            uprint("  key=");
            uprint_dec(ev.key);
            uprint("\n");
            if (ev.key == 'q' || ev.key == 'Q') break;
        }
    }
    tally(mouse, keys);
    uprint("mousedemo: done\n");
    uexit();
}
