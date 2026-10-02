#include "ulib.h"
#include "ugfx.h"
#include "utext.h"
#include "attune.h"

/* Roadmap P1.5 / G2–G6 — chaos compositor (userspace).
 * Askew floating windows, drag, focus, chaos jitter. No grid snap.
 *
 * G2: every frame is composed into a SYS_SURFACE back buffer and presented
 *     with a single blit.
 * G3: input arrives through SYS_EVENT_WAIT; the loop sleeps between events.
 * G4: windows can host client programs. A client runs as its own process
 *     with its console on two pipes (SYS_SPAWNIO); the compositor renders
 *     its output into a text grid and forwards keys to the focused one.
 *     The client never touches the screen — skew stays here.
 * G5: `t` summons terminal (2× glyphs). Children inherit the window
 *     pipes. PgUp/PgDn scroll the focused grid. Up/Down do that from
 *     the line editor; a client editor receives them raw. `2` toggles 1×/2×.
 *     q and F1 do not leave the desk or open the kernel shell.
 * G6: interactive boot execs this as the desk. Palette follows the shared
 *     `attune` file (deck / notes / code). The top bar cycles modes
 *     (click a chip, or `a`). Programs live on the desk as icons
 *     (`desk_icons`); terminal is the first one.
 *
 * Keys (command mode, or when the focused window has no client):
 *   a attune   t terminal   s dummy   b banish   n next
 *   2 scale   +/- chaos   Esc back to the app
 * Keys (app mode): everything goes to the focused client, including
 *   Up/Down. Esc = command mode (not forwarded). Ctrl+letter arrives as
 *   a control byte so a client editor can save and quit.
 * Mouse: desk icons, toolbar chips, click focus / drag title bar.
 * Byte 0x01 is not a key. The test harness injects it to end the desk. */

#define MAX_WIN      8
#define TITLE_H      18
#define BAR_H        26
#define JITTER_TICKS 4      /* ~12 frames/s of wiggle while chaos > 0     */
#define CLIENT_TICKS 2      /* poll client pipes this often               */
#define TERM_COLS    44
#define TERM_ROWS    16
#define TERM_PAD     4
#define STDIN_HIGHWATER 900 /* drop keys rather than block on a full pipe */
#define QUIT_GRACE   150    /* ticks to wait for clients at quit          */

enum {
    CHIP_DECK = ATTUNE_DECK,
    CHIP_NOTES = ATTUNE_NOTES,
    CHIP_CODE = ATTUNE_CODE
};

/* Desk icons. Append a row to give another program an icon.
 * glyph is the short mark in the plate; prog is the summon line;
 * scale is the window glyph scale (1 or 2). */
typedef struct {
    const char *label;
    const char *prog;
    const char *glyph;
    int scale;
} desk_icon_t;

static const desk_icon_t desk_icons[] = {
    { "terminal", "terminal.elf", ">_", 2 },
    { "doom", "doom.elf", "D", 2 },
};
#define N_ICONS ((int) (sizeof desk_icons / sizeof desk_icons[0]))

#define ICON_PLATE_W 48
#define ICON_PLATE_H 36
#define ICON_HIT_W   72
#define ICON_HIT_H   52
#define ICON_ROWS    8

/* Not a scancode. make test injects this; the keyboard cannot produce it. */
#define HARNESS_HALT 1

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
static int chaos = 0;
static int cmd_mode = 1;
static int attune = ATTUNE_DECK;
static int scr_w, scr_h;
static int drag = -1, drag_ox, drag_oy;
static uint32_t prev_btns;
static usurf_t back;   /* full-screen back buffer */
static uint32_t pal_desk, pal_bar, pal_hint, pal_term_bg, pal_term_fg;
static uint32_t pal_chip, pal_chip_on;

#define MAX_CHIP 8
static struct {
    int x, w, kind;
    const char *lab;
} chips[MAX_CHIP];
static int n_chips;
static int n_summoned;

static void cpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void apply_palette(void) {
    if (attune == ATTUNE_NOTES) {
        pal_desk = 0x00201810u;
        pal_bar = 0x00382820u;
        pal_hint = 0x00E8D0B0u;
        pal_term_bg = 0x00241C14u;
        pal_term_fg = 0x00E8D8C0u;
        pal_chip = 0x00504030u;
        pal_chip_on = 0x00C87840u;
    } else if (attune == ATTUNE_CODE) {
        pal_desk = 0x00081410u;
        pal_bar = 0x00102018u;
        pal_hint = 0x00A0D8A8u;
        pal_term_bg = 0x000C1810u;
        pal_term_fg = 0x00B8E0B0u;
        pal_chip = 0x00203828u;
        pal_chip_on = 0x0030A060u;
    } else {
        pal_desk = 0x00101828u;
        pal_bar = 0x00202838u;
        pal_hint = 0x00C0C8D0u;
        pal_term_bg = 0x00141C24u;
        pal_term_fg = 0x00D8E0C0u;
        pal_chip = 0x00304050u;
        pal_chip_on = 0x00E0A040u;
    }
}

static void set_attune(int m) {
    if (m < ATTUNE_DECK || m > ATTUNE_CODE) m = ATTUNE_DECK;
    attune = m;
    attune_save(attune);
    apply_palette();
}

static void add_chip(int *x, int kind, const char *lab) {
    int i = 0;
    while (lab[i]) i++;
    int w = 8 + i * 8;
    if (n_chips >= MAX_CHIP) return;
    chips[n_chips].x = *x;
    chips[n_chips].w = w;
    chips[n_chips].kind = kind;
    chips[n_chips].lab = lab;
    n_chips++;
    *x += w + 4;
}

static void layout_bar(void) {
    n_chips = 0;
    int x = 6;
    add_chip(&x, CHIP_DECK, "deck");
    add_chip(&x, CHIP_NOTES, "notes");
    add_chip(&x, CHIP_CODE, "code");
}

/* Column down the left of the desk, then the next column. */
static void icon_place(int i, int *x, int *y) {
    int col = i / ICON_ROWS;
    int row = i % ICON_ROWS;
    *x = 16 + col * 88;
    *y = BAR_H + 16 + row * 68;
}

static void draw_icons(void) {
    for (int i = 0; i < N_ICONS; i++) {
        int x, y;
        icon_place(i, &x, &y);
        ugfx_fill(&back, x, y, ICON_PLATE_W, ICON_PLATE_H, pal_term_bg);
        ugfx_fill(&back, x, y, ICON_PLATE_W, 2, pal_chip_on);
        ugfx_fill(&back, x, y + ICON_PLATE_H - 2, ICON_PLATE_W, 2, pal_chip_on);
        ugfx_fill(&back, x, y, 2, ICON_PLATE_H, pal_chip_on);
        ugfx_fill(&back, x + ICON_PLATE_W - 2, y, 2, ICON_PLATE_H, pal_chip_on);
        ugfx_text(&back, x + 8, y + 10, desk_icons[i].glyph,
                  pal_term_fg, pal_term_bg, 1, 2);
        ugfx_text(&back, x, y + 40, desk_icons[i].label,
                  pal_hint, pal_desk, 1, 1);
    }
}

static int icon_named(const char *lab) {
    for (int i = 0; i < N_ICONS; i++) {
        const char *a = desk_icons[i].label;
        int k = 0;
        while (a[k] && a[k] == lab[k]) k++;
        if (a[k] == lab[k]) return i;
    }
    return -1;
}

static int icon_at(int mx, int my) {
    for (int i = 0; i < N_ICONS; i++) {
        int x, y;
        icon_place(i, &x, &y);
        if (mx >= x && mx < x + ICON_HIT_W && my >= y && my < y + ICON_HIT_H)
            return i;
    }
    return -1;
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
    int i = summon_win(title, x, y, bw, bh, sk, pal_term_bg, pal_chip);
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
                   pal_term_fg, pal_term_bg, sx, sx, focused && !cmd_mode && w->text.view == 0);
    } else if (focused) {
        ugfx_text(&back, x + sk + 8, y + th + 8, "focus", 0x00FFFFFF, w->body, 1, 2);
    } else {
        ugfx_text(&back, x + sk + 8, y + th + 8, "askew", 0x00E0E0E0, w->body, 1, 2);
    }
}

static void redraw(void) {
    ugfx_clear(&back, pal_desk);
    layout_bar();
    ugfx_fill(&back, 0, 0, scr_w, BAR_H, pal_bar);
    draw_icons();
    for (int i = 0; i < n_chips; i++) {
        int on = (chips[i].kind == attune);
        uint32_t bg = on ? pal_chip_on : pal_chip;
        uint32_t fg = on ? 0x00101010u : pal_hint;
        ugfx_fill(&back, chips[i].x, 4, chips[i].w, 18, bg);
        ugfx_text(&back, chips[i].x + 4, 6, chips[i].lab, fg, bg, 1, 2);
    }
    char lvl[] = "chaos:0";
    lvl[6] = (char) ('0' + (chaos > 9 ? 9 : chaos));
    ugfx_text(&back, scr_w - 72, 6, lvl, pal_hint, pal_bar, 1, 2);
    if (!(cmd_mode || !focused_client()))
        ugfx_text(&back, scr_w - 200, 6, "esc", pal_hint, pal_bar, 1, 2);

    for (int i = 0; i < MAX_WIN; i++)
        if (wins[i].state == W_ALIVE && i != focus) draw_win(i);
    if (focus >= 0 && wins[focus].state == W_ALIVE) draw_win(focus);
    ugfx_present(&back, 0, 0);
}

/* ---- input --------------------------------------------------------------- */

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

static int same_str(const char *a, const char *b) {
    int i = 0;
    while (a[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

static void launch_desk_icon(int i) {
    if (i < 0 || i >= N_ICONS) return;
    /* Doom draws on the framebuffer, which the desk would paint over.
     * Leave the desk, let the kernel run it, then come back. */
    if (same_str(desk_icons[i].prog, "doom.elf")) {
        char one[1];
        one[0] = '1';
        uwritefile("launchdoom", one, 1);
        quit();
    }
    summon_client(desk_icons[i].prog, desk_icons[i].label,
                  ++n_summoned, desk_icons[i].scale);
}

static int toolbar_click(int mx, int my) {
    if (my < 0 || my >= BAR_H) return 0;
    layout_bar();
    for (int i = 0; i < n_chips; i++) {
        if (mx < chips[i].x || mx >= chips[i].x + chips[i].w) continue;
        set_attune(chips[i].kind);
        redraw();
        return 1;
    }
    return 1; /* eat bar clicks so they do not hit windows underneath */
}

static void handle_command(char c) {
    switch (c) {
        case HARNESS_HALT: quit(); break;
        case 'a': case 'A': set_attune((attune + 1) % 3); break;
        case 't': case 'T': launch_desk_icon(icon_named("terminal")); break;
        case 's': case 'S': if (attune == ATTUNE_DECK) summon_dummy(); break;
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
    unsigned char u = (unsigned char) c;
    /* Key 0 is a repaint poke, not a typed character. */
    if (c == 0) { redraw(); return; }
    /* Page keys scroll the window in either mode. They are not forwarded,
     * so an editor keeps Up/Down for its own cursor. */
    if ((u == UEV_KEY_PGUP || u == UEV_KEY_PGDN) &&
        focus >= 0 && wins[focus].state != W_FREE) {
        utext_scroll(&wins[focus].text, u == UEV_KEY_PGUP ? 10 : -10);
        redraw();
        return;
    }
    /* Command mode keeps Up/Down for history. App mode forwards them. */
    if (cmd_mode && focus >= 0 && has_client(&wins[focus]) &&
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
        if (toolbar_click(m->x, m->y)) return;
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
        } else {
            int ix = icon_at(m->x, m->y);
            if (ix >= 0) {
                launch_desk_icon(ix);
                redraw();
            }
        }
    } else if (down && drag >= 0 && wins[drag].state == W_ALIVE) {
        wins[drag].x = m->x - drag_ox;
        wins[drag].y = m->y - drag_oy;
        /* Soft clamp — still free-floating, no snap. */
        if (wins[drag].x < -40) wins[drag].x = -40;
        if (wins[drag].y < BAR_H) wins[drag].y = BAR_H;
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

    attune = attune_load();
    apply_palette();
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
