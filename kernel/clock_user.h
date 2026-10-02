#ifndef CLOCK_USER_H
#define CLOCK_USER_H

#include <stdint.h>

/* Wall clock from the CMOS RTC, filled by SYS_CLOCK. Local time, no timezone. */
typedef struct {
    uint32_t year;
    uint32_t month; /* 1–12 */
    uint32_t day;   /* 1–31 */
    uint32_t hour;  /* 0–23 */
    uint32_t min;
    uint32_t sec;
} uclock_t;

#endif
