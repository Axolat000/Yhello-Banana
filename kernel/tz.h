#ifndef TZ_H
#define TZ_H

#include "types.h"

/*
 * Time zones: a built-in table of places, each with its offset from UTC
 * and its summer-time rule (the EU's, the USA's...). The system keeps the
 * time in UTC (kernel/rtc.c); the local time - the clock in the taskbar,
 * `date`, files' dates, apps' localtime() - is the chosen zone's.
 */

#define TZR_NONE 0      /* no summer time */
#define TZR_EU   1      /* last Sunday of March - last Sunday of October, 01:00 UTC */
#define TZR_US   2      /* 2nd Sunday of March - 1st Sunday of November, 02:00 local */
#define TZR_AU   3      /* (south) 1st Sunday of October - 1st Sunday of April */
#define TZR_NZ   4      /* last Sunday of September - 1st Sunday of April */
#define TZR_CL   5      /* (Chile) 1st Sunday of September - 1st Sunday of April */

typedef struct {
    const char* id;          /* "Europe/Paris" (the IANA name) */
    const char* label;       /* "Paris, Berlin, Rome, Madrid" */
    int16_t     std_min;     /* minutes east of UTC, winter */
    uint8_t     rule;        /* TZR_*: +60 minutes while it applies */
} tz_zone_t;

int               tz_count(void);
const tz_zone_t*  tz_get(int i);
int               tz_find(const char* id);      /* its index, or -1 (case does not matter) */

int  tz_current(void);                          /* the zone in use (index; 0: UTC) */
/* the zone in use from now on (kept in the settings): 0, -1. The time
 * itself does not change - the local time does */
int  tz_set(int i);
void tz_boot(void);                             /* the saved zone (settings) */
/* the zone in use for now, not saved (selftest): the one before */
int  tz_use(int i);

/* seconds east of UTC at the instant utc (seconds since 1970), in zone i */
int      tz_offset_at(int i, uint32_t utc);
int      tz_is_summer(int i, uint32_t utc);
/* UTC to the current zone's local time, and back (a local time that does
 * not exist - the hour skipped in spring - is read as winter time) */
uint32_t tz_to_local(uint32_t utc);
uint32_t tz_to_utc(uint32_t local);
/* "UTC+01:00" / "UTC-03:30" for an offset in seconds */
void     tz_format_offset(int secs, char* out, int cap);

#endif
