#include "ulib.h"
#include "ugfx.h"
#include "utext.h"

/* Roadmap P1.5 / G2 / G3 / G4 / G5 — chaos compositor (userspace).
 * Askew floating windows, drag, focus, chaos jitter. No grid snap.
 *
 * G2: every frame is composed into a SYS_SURFACE back buffer and presented
 *     with a single blit.
 * G3: input arrives through SYS_EVENT_WAIT; the loop sleeps between events.
 * G4: windows can host client programs. A client runs as its own process
 *     with its console on two pipes (SYS_SPAWNIO); the compositor renders
 *     its output into a text grid and forwards keys to the focused one.
 *     The client never touches the screen — skew stays here.
 * G5: `u` summons ush (2× glyphs). Children inherit the window pipes, so
 *     folio/scribe from ush stay in the same window. Up/Down scroll the
 *     grid's history. `2` toggles 1×/2×. F1 (kernel) drops back to the
 *     text console without killing chaoswm.
 *
 * Keys (command mode, or when the focused window has no client):
 *   u ush   f folio   e scribe   s dummy   b banish   n next
 *   2 scale   +/- chaos   q quit   Esc back to the app
 * Keys (app mode): everything goes to the focused client; Esc = command mode.
 *   Up/Down always scroll the focused client's history.
 * Mouse: click focus / drag title bar. */

#define MAX_WIN      8
#define TITLE_H      18
#define DESK_BG      0x00101828u
#define BAR_BG       0x00202838u
#define HINT_FG      0x00C0C8D0u
#define TERM_BG      0x00141C24u
#define TERM_FG      0x00D8E0C0u
#define JITTER_TICKS 4      /* ~12 frames/s of wiggle while chaos > 0     */
#define CLIENT_TICKS 2      /* poll client pipes this often               */
#define TERM_COLS    44
#define TERM_ROWS    16
#define TERM_PAD     4
#define STDIN_HIGHWATER 400 /* drop keys rather than block on a full pipe */
#define QUIT_GRACE   150    /* ticks to wait for clients at quit          */

enum { W_FREE = 0, W_ALIVE, W_CLOSING };

typedef struct {
    int state;
    int x, y, w, h;
    int skew;          /* intentional misalignment (px) */
    uint32_t body;
    uint32_t title_bg;
    char title[24];
    /* client (G4): pid and the two console pipes; -1 = none */
    int pid, in, out;
    int sx;            /* glyph scale 1 or 2 (G5) */
    utext_t text;
} win_t;

static win_t wins[MAX_WIN];
static int focus = -1;
static int chaos = 2;
static int cmd_mode = 1;
static int scr_w, scr_h;
static int drag = -1, drag_ox, drag_oy;
static uint32_t prev_btns;
static usurf_t back;   /* full-screen back buffer */

static void cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int fb_info(void) {
    fb_req_t r;
    r.op = FBOP_INFO;
    if (ufb(&r) != 0 || !r.ok) return -1;
    scr_w = (int) r.width;
    scr_h = (int) r.height;
    return 0;
}

static void fb_restore(void) {
    fb_req_t r;
    r.op = FBOP_RESTORE;
    ufb(&r);
}

static int jitter(int seed) {
    /* Cheap deterministic wiggle from uptime + seed; scales with chaos. */
    uint32_t t = uuptime() + (uint32_t) (seed * 17);
    int j = (int) ((t ^ (t >> 3)) & 7u) - 3;
    return j * chaos / 2;
}

static int has_client(const win_t *w) {
    return w->state == W_ALIVE && w->in >= 0;
}

static int focused_client(void) {
    return focus >= 0 && has_client(&wins[focus]);
}

static int alloc_slot(void) {
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].state == W_FREE) return i;
    return -1;
}

static void pick_focus(void) {
    focus = -1;
    for (int i = MAX_WIN - 1; i >= 0; i--) {
        if (wins[i].state == W_ALIVE) { focus = i; break; }
    }
}

static void next_focus(void) {
    for (int off = 1; off <= MAX_WIN; off++) {
        int i = (focus + off) % MAX_WIN;
        if (i < 0) i += MAX_WIN;
        if (wins[i].state == W_ALIVE) { focus = i; return; }
    }
}

static int summon_win(const char *title, int x, int y, int w, int h,
                      int skew, uint32_t body, uint32_t tbg) {
    int i = alloc_slot();
    if (i < 0) return -1;
    wins[i].state = W_ALIVE;
    wins[i].x = x;
    wins[i].y = y;
    wins[i].w = w;
    wins[i].h = h;
    wins[i].skew = skew;
    wins[i].body = body;
    wins[i].title_bg = tbg;
    wins[i].pid = wins[i].in = wins[i].out = -1;
    wins[i].sx = 1;
    cpy(wins[i].title, title, (int) sizeof(wins[i].title));
    focus = i;
    return i;
}

/* ---- clients ------------------------------------------------------------- */

/* Read whatever a pipe holds and throw it away until EOF frees the slot.
 * Only called once the writer is gone, so this cannot block. */
static void drain_free(int id) {
    char junk[64];
    for (int guard = 0; guard < 64; guard++) {
        int n = upipepoll(id);
        if (n > 0) { upiperead(id, junk, (int) sizeof(junk)); continue; }
        if (n < 0) { upiperead(id, junk, 1); return; }   /* observe EOF */
        return;                                          /* open, empty: leave */
    }
}

static void client_geom(int sx, int *bw, int *bh) {
    if (sx < 1) sx = 1;
    if (sx > 2) sx = 2;
    *bw = TERM_COLS * 8 * sx + TERM_PAD * 2;
    *bh = TERM_ROWS * 8 * sx + TERM_PAD * 2 + TITLE_H;
}

static int summon_client(const char *cmdline, const char *title, int n, int sx) {
    if (sx < 1) sx = 1;
    if (sx > 2) sx = 2;
    int in = upipe();
    if (in < 0) return -1;
    int out = upipe();
    if (out < 0) { upipeclose(in); drain_free(in); return -1; }

    int x = 60 + (n * 53) % (scr_w / 3);
    int y = 40 + (n * 31) % (scr_h / 4);
    int sk = 6 + (n * 7) % 16;
    if (n & 1) sk = -sk;
    int bw, bh;
    client_geom(sx, &bw, &bh);
    int i = summon_win(title, x, y, bw, bh, sk, TERM_BG, 0x00405870u);
    if (i < 0) {
        upipeclose(in); drain_free(in);
        upipeclose(out); drain_free(out);
        return -1;
    }
    int pid = uspawnio(cmdline, in, out);
    if (pid < 0) {
        wins[i].state = W_FREE;
        upipeclose(in); drain_free(in);
        upipeclose(out); drain_free(out);
        pick_focus();
        return -1;
    }
    wins[i].pid = pid;
    wins[i].in = in;
    wins[i].out = out;
    wins[i].sx = sx;
    utext_init(&wins[i].text, TERM_COLS, TERM_ROWS);
    cmd_mode = 0;   /* the user summoned it to type into it */
    return i;
}

/* Hang up the client's stdin. The kernel ends the program the next time it
 * reads (EOF), which closes its stdout; drain_clients() then reclaims. */
static void hangup(win_t *w) {
    if (w->in >= 0) {
        upipeclose(w->in);
    }
}

static void reclaim(win_t *w) {
    char junk;
    if (w->out >= 0) { upiperead(w->out, &junk, 1); w->out = -1; } /* EOF frees */
    if (w->in >= 0) { drain_free(w->in); w->in = -1; }
    w->pid = -1;
    w->state = W_FREE;
}

/* Pull pending output from every client into its text grid. Returns 1 if
 * anything changed on screen. */
static int drain_clients(void) {
    int dirty = 0;
    char buf[256];
    for (int i = 0; i < MAX_WIN; i++) {
        win_t *w = &wins[i];
        if (w->state == W_FREE || w->out < 0) continue;
        for (int rounds = 0; rounds < 8; rounds++) {
            int n = upipepoll(w->out);
            if (n == 0) break;
            if (n < 0) {
                /* Client exited: its stdout is closed and drained. */
                int was_visible = (w->state == W_ALIVE);
                hangup(w);
                reclaim(w);
                if (was_visible) {
                    if (focus == i) { pick_focus(); cmd_mode = 1; }
                    dirty = 1;
                }
                break;
            }
            if (n > (int) sizeof(buf)) n = (int) sizeof(buf);
            n = upiperead(w->out, buf, n);
            if (n <= 0) break;
            if (w->state == W_ALIVE) {
                utext_feed(&w->text, buf, n);
                dirty = 1;
            }
        }
    }
    return dirty;
}

static int clients_pending(void) {
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].state != W_FREE && wins[i].out >= 0) return 1;
    return 0;
}

static void send_key(win_t *w, char c) {
    if (w->in < 0) return;
    int q = upipepoll(w->in);
    if (q < 0 || q >= STDIN_HIGHWATER) return;
    upipewrite(w->in, &c, 1);
}

static void banish_focus(void) {
    if (focus < 0 || wins[focus].state != W_ALIVE) return;
    win_t *w = &wins[focus];
    if (w->in >= 0) {
        hangup(w);
        w->state = W_CLOSING;   /* hidden until stdout reaches EOF */
    } else {
        w->state = W_FREE;
    }
    pick_focus();
    cmd_mode = 1;
}

static void quit(void) {
    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].state != W_FREE) { hangup(&wins[i]); wins[i].state = W_CLOSING; }
    uint32_t start = uuptime();
    while (clients_pending() && uuptime() - start < QUIT_GRACE) {
        drain_clients();
        usleep(1);
    }
    ugfx_destroy(&back);
    fb_restore();
    uexit();
}

/* ---- drawing ------------------------------------------------------------- */

static int hit_test(int mx, int my, int *title_hit) {
    *title_hit = 0;
    for (int i = MAX_WIN - 1; i >= 0; i--) {
        if (wins[i].state != W_ALIVE) continue;
        int jx = jitter(i);
        int jy = jitter(i + 3);
        int x = wins[i].x + jx;
        int y = wins[i].y + jy;
        int sk = wins[i].skew;
        /* Title spans both skewed edges. */
        int x0 = x < x + sk ? x : x + sk;
        int x1 = x + wins[i].w;
        if (x + sk + wins[i].w > x1) x1 = x + sk + wins[i].w;
        if (mx < x0 || mx >= x1 || my < y || my >= y + wins[i].h) continue;
        if (my < y + TITLE_H) *title_hit = 1;
        return i;
    }
    return -1;
}

static void draw_win(int i) {
    win_t *w = &wins[i];
    int jx = jitter(i);
    int jy = jitter(i + 3);
    int x = w->x + jx;
    int y = w->y + jy;
    int sk = w->skew;
    int tw = w->w;
    int th = TITLE_H;
    int bh = w->h - TITLE_H;
    int focused = (i == focus);
    uint32_t border = focused ? 0x00FFE080u : 0x00405060u;

    /* Shadow — deliberately offset, not a grid. */
    ugfx_fill(&back, x + sk + 6, y + 6, tw, w->h, 0x00000000);

    /* Askew title vs body: body shifted by skew. */
    ugfx_fill(&back, x - 2, y - 2, tw + 4, th + 4, border);
    ugfx_fill(&back, x, y, tw, th, w->title_bg);
    ugfx_text(&back, x + 6, y + 2, w->title, 0x00FFFFFF, w->title_bg, 1, 2);

    ugfx_fill(&back, x + sk - 2, y + th - 2, tw + 4, bh + 4, border);
    ugfx_fill(&back, x + sk, y + th, tw, bh, w->body);

    if (has_client(w)) {
        int sx = w->sx < 1 ? 1 : w->sx;
        utext_draw(&w->text, &back, x + sk + TERM_PAD, y + th + TERM_PAD,
                   TERM_FG, TERM_BG, sx, sx, focused && !cmd_mode && w->text.view == 0);
    } else if (focused) {
        ugfx_text(&back, x + sk + 8, y + th + 8, "focus", 0x00FFFFFF, w->body, 1, 2);
    } else {
        ugfx_text(&back, x + sk + 8, y + th + 8, "askew", 0x00E0E0E0, w->body, 1, 2);
    }
}

static void redraw(void) {
    ugfx_clear(&back, DESK_BG);
    ugfx_fill(&back, 0, 0, scr_w, 22, BAR_BG);
    int command = cmd_mode || !focused_client();
    ugfx_text(&back, 8, 4,
              command ? "chaoswm [cmd] u:ush f:folio e:scribe s:win b n 2:scale +/- q  F1:console"
                      : "chaoswm [app] keys to the window - Esc commands  Up/Dn scroll  F1:console",
              HINT_FG, BAR_BG, 1, 2);

    char lvl[] = "chaos:0";
    lvl[6] = (char) ('0' + (chaos > 9 ? 9 : chaos));
    ugfx_text(&back, scr_w - 72, 4, lvl, HINT_FG, BAR_BG, 1, 2);

    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].state == W_ALIVE && i != focus) draw_win(i);
    if (focus >= 0 && wins[focus].state == W_ALIVE) draw_win(focus);
    ugfx_present(&back, 0, 0);
}

/* ---- input --------------------------------------------------------------- */

static int n_summoned;

static void summon_dummy(void) {
    int n = ++n_summoned;
    int x = 40 + (n * 37) % (scr_w / 2);
    int y = 50 + (n * 29) % (scr_h / 3);
    int sk = 8 + (n * 5) % 24;
    if (n & 1) sk = -sk;
    char title[16];
    title[0] = 'w'; title[1] = 'i'; title[2] = 'n';
    title[3] = (char) ('0' + (n % 10));
    title[4] = '\0';
    uint32_t cols[] = { 0x00CC3355u, 0x0033AACCu, 0x00DDAA22u,
                        0x0055CC66u, 0x009966CCu };
    summon_win(title, x, y, 160 + (n % 3) * 20, 100 + (n % 2) * 20,
               sk, cols[n % 5], cols[n % 5] - 0x00202020u);
}

static void toggle_scale(void) {
    if (focus < 0 || !has_client(&wins[focus])) return;
    win_t *w = &wins[focus];
    w->sx = (w->sx == 2) ? 1 : 2;
    client_geom(w->sx, &w->w, &w->h);
}

static void handle_command(char c) {
    switch (c) {
        case 'q': case 'Q': quit(); break;
        case 'u': case 'U': summon_client("ush.elf", "ush", ++n_summoned, 2); break;
        case 'f': case 'F': summon_client("folio.elf", "folio", ++n_summoned, 1); break;
        case 'e': case 'E': summon_client("scribe.elf notes.txt", "scribe", ++n_summoned, 1); break;
        case 's': case 'S': summon_dummy(); break;
        case 'b': case 'B': banish_focus(); break;
        case 'n': case 'N': case '\t': next_focus(); break;
        case '2':           toggle_scale(); break;
        case '+': case '=': if (chaos < 9) chaos++; break;
        case '-': case '_': if (chaos > 0) chaos--; break;
        case 27:            if (focused_client()) cmd_mode = 0; break;
        default: return;
    }
    redraw();
}

static void handle_key(char c) {
    /* Up/Down always scroll the focused client's history (G5). */
    if (focus >= 0 && has_client(&wins[focus]) &&
        (c == (char) UEV_KEY_UP || c == (char) UEV_KEY_DOWN)) {
        utext_scroll(&wins[focus].text, c == (char) UEV_KEY_UP ? 3 : -3);
        redraw();
        return;
    }
    if (cmd_mode || !focused_client()) {
        handle_command(c);
        return;
    }
    if (c == 27) {
        cmd_mode = 1;
        redraw();
        return;
    }
    send_key(&wins[focus], c);
}

static void handle_mouse(const uevent_t *m) {
    int down = (int) (m->buttons & 1u);
    int prev = (int) (prev_btns & 1u);
    int th = 0;

    if (down && !prev) {
        int hit = hit_test(m->x, m->y, &th);
        if (hit >= 0) {
            focus = hit;
            cmd_mode = has_client(&wins[hit]) ? 0 : 1;
            if (th) {
                drag = focus;
                drag_ox = m->x - wins[focus].x;
                drag_oy = m->y - wins[focus].y;
            }
            redraw();
        }
    } else if (down && drag >= 0 && wins[drag].state == W_ALIVE) {
        wins[drag].x = m->x - drag_ox;
        wins[drag].y = m->y - drag_oy;
        /* Soft clamp — still free-floating, no snap. */
        if (wins[drag].x < -40) wins[drag].x = -40;
        if (wins[drag].y < 22) wins[drag].y = 22;
        if (wins[drag].x > scr_w - 40) wins[drag].x = scr_w - 40;
        if (wins[drag].y > scr_h - 40) wins[drag].y = scr_h - 40;
        redraw();
    } else if (!down) {
        drag = -1;
    }
    prev_btns = m->buttons;
}

void main(void) {
    if (fb_info() != 0) {
        uprint("chaoswm: no framebuffer\n");
        uexit();
    }
    if (!ugfx_create(&back, scr_w, scr_h)) {
        uprint("chaoswm: no surface (SYS_SURFACE failed)\n");
        uexit();
    }

    for (int i = 0; i < MAX_WIN; i++) wins[i].state = W_FREE;

    /* Two seed windows — deliberately askew, not aligned. */
    summon_win("scrap", 48, 64, 200, 120, 18, 0x00CC3355u, 0x00AA2244u);
    summon_win("rift", 220, 100, 180, 110, -22, 0x0033AACCu, 0x002288AAu);
    focus = 1;

    redraw();

    uint32_t last_jitter = uuptime();
    for (;;) {
        uevent_t ev;
        uint32_t timeout = UEV_FOREVER;
        if (clients_pending()) timeout = CLIENT_TICKS;
        else if (chaos > 0) timeout = JITTER_TICKS;

        int r = uevent_wait(&ev, timeout);
        if (r < 0) {
            ugfx_destroy(&back);
            fb_restore();
            uprint("chaoswm: SYS_EVENT_WAIT failed\n");
            uexit();
        }
        if (r == 1) {
            if (ev.type == UEV_KEY) handle_key((char) ev.key);
            else if (ev.type == UEV_MOUSE) handle_mouse(&ev);
        }

        int dirty = drain_clients();
        uint32_t now = uuptime();
        if (chaos > 0 && now - last_jitter >= JITTER_TICKS) {
            last_jitter = now;
            dirty = 1;
        }
        if (dirty) redraw();
    }
}
