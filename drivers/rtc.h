#ifndef RTC_H
#define RTC_H

#include "../kernel/clock_user.h"

/* Read the CMOS real-time clock. 0 on a sane result, -1 otherwise.
 * Not for IRQ context: it spins briefly while an update is in progress. */
int rtc_read(uclock_t *out);

#endif
