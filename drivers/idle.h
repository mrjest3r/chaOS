#ifndef IDLE_H
#define IDLE_H

#include <stdint.h>

/* P4.3 — display blank / sleep; wake on keyboard or mouse. */

void idle_init(void);

/* Call on user activity (key/mouse). Wakes if blanked. */
void idle_touch(void);

/* If blanked, wake and return 1 (caller should swallow the event). Else 0. */
int idle_wake_if_blank(void);

/* Blank the display without waiting (also used by AUTOTEST). */
void idle_blank(void);

/* Blank now; wait (IF=1) until a key/mouse wakes the display. */
void idle_doze(void);

/* From the kernel idle loop: auto-blank after timeout with no input. */
void idle_poll(void);

int idle_is_blank(void);

/* Auto-blank after this many PIT ticks of silence (0 = off). Default ~30s @ 50Hz. */
void idle_set_timeout(uint32_t ticks);
uint32_t idle_get_timeout(void);

#endif
