#include "rtc.h"
#include "../cpu/ports.h"

/* MC146818 CMOS. Bit 7 of the index port keeps NMI disabled; this kernel
 * does not use NMI. Registers: 0 seconds, 2 minutes, 4 hours, 7 day,
 * 8 month, 9 year, 0x32 century. Status A bit 7 is update-in-progress.
 * Status B bit 1 is 24-hour, bit 2 is binary (otherwise BCD). */

#define CMOS_INDEX 0x70
#define CMOS_DATA  0x71

static uint8_t cmos_read(uint8_t reg) {
    port_byte_out(CMOS_INDEX, (uint8_t) (reg | 0x80));
    return port_byte_in(CMOS_DATA);
}

static int from_bcd(uint8_t v, int binary) {
    if (binary) return (int) v;
    return (int) (v & 0x0F) + (int) ((v >> 4) * 10);
}

static void wait_ready(void) {
    for (int i = 0; i < 100000; i++) {
        if ((cmos_read(0x0A) & 0x80) == 0) return;
    }
}

typedef struct {
    int year, month, day, hour, min, sec;
} snap_t;

static void read_snap(snap_t *s) {
    uint8_t st = cmos_read(0x0B);
    int binary = (st & 0x04) != 0;
    int h24 = (st & 0x02) != 0;
    uint8_t raw_h = cmos_read(0x04);
    int pm = 0;
    if (!h24) {
        pm = (raw_h & 0x80) != 0;
        raw_h = (uint8_t) (raw_h & 0x7F);
    }
    s->sec = from_bcd(cmos_read(0x00), binary);
    s->min = from_bcd(cmos_read(0x02), binary);
    s->hour = from_bcd(raw_h, binary);
    if (!h24) {
        if (s->hour == 12) s->hour = pm ? 12 : 0;
        else if (pm) s->hour += 12;
    }
    s->day = from_bcd(cmos_read(0x07), binary);
    s->month = from_bcd(cmos_read(0x08), binary);
    int y = from_bcd(cmos_read(0x09), binary);
    int c = from_bcd(cmos_read(0x32), binary);
    if (c >= 19 && c <= 99) s->year = c * 100 + (y % 100);
    else if (y < 100) s->year = 2000 + y;
    else s->year = y;
}

static int same_snap(const snap_t *a, const snap_t *b) {
    return a->year == b->year && a->month == b->month && a->day == b->day &&
           a->hour == b->hour && a->min == b->min && a->sec == b->sec;
}

static int sane_snap(const snap_t *s) {
    if (s->year < 2000 || s->year > 2099) return 0;
    if (s->month < 1 || s->month > 12) return 0;
    if (s->day < 1 || s->day > 31) return 0;
    if (s->hour < 0 || s->hour > 23) return 0;
    if (s->min < 0 || s->min > 59) return 0;
    if (s->sec < 0 || s->sec > 59) return 0;
    return 1;
}

int rtc_read(uclock_t *out) {
    snap_t a, b;
    if (!out) return -1;
    wait_ready();
    read_snap(&a);
    wait_ready();
    read_snap(&b);
    if (!same_snap(&a, &b)) {
        wait_ready();
        read_snap(&b);
    }
    if (!sane_snap(&b)) return -1;
    out->year = (uint32_t) b.year;
    out->month = (uint32_t) b.month;
    out->day = (uint32_t) b.day;
    out->hour = (uint32_t) b.hour;
    out->min = (uint32_t) b.min;
    out->sec = (uint32_t) b.sec;
    return 0;
}
