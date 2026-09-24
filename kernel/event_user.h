#ifndef EVENT_USER_H
#define EVENT_USER_H

#include <stdint.h>

/* Shared kernel/user input event (SYS_EVENT_WAIT). GUI G3, ABI v6.
 *
 * The first task to call SYS_EVENT_WAIT becomes the input listener: from then
 * on keys typed while a program has focus land in its queue instead of the
 * SYS_GETCHAR ring, and every mouse packet is reported to it. Consecutive
 * mouse events with the same button state are coalesced (position updated in
 * place), so a slow consumer never overflows the queue with motion. */

#define UEV_NONE   0
#define UEV_KEY    1   /* key = character or UEV_KEY_* navigation code        */
#define UEV_MOUSE  2   /* x, y = pointer position; buttons = current state   */

/* Navigation keys arrive as these control bytes (same values the SYS_GETCHAR
 * ring uses for Left/Right/Home/End; Up/Down are event-only). */
#define UEV_KEY_LEFT  0x11
#define UEV_KEY_RIGHT 0x12
#define UEV_KEY_HOME  0x13
#define UEV_KEY_END   0x14
#define UEV_KEY_UP    0x15
#define UEV_KEY_DOWN  0x16

/* Timeout argument: 0 = poll and return at once, UEV_FOREVER = block. */
#define UEV_FOREVER 0xFFFFFFFFu

typedef struct {
    uint32_t type;      /* UEV_*                                          */
    int32_t  x, y;      /* UEV_MOUSE: pointer position                    */
    uint32_t key;       /* UEV_KEY: character / UEV_KEY_* code            */
    uint32_t buttons;   /* UEV_MOUSE: bit0=left, bit1=right, bit2=middle  */
} uevent_t;

#endif
