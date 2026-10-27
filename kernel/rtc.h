#ifndef RTC_H
#define RTC_H

#include "types.h"

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_datetime_t;

void rtc_init(void);
void rtc_boot(void);                     /* the saved settings (whether the chip keeps UTC) */

/* the local time now (the time zone's, kernel/tz.c): -1 if there is no clock */
int rtc_read_datetime(rtc_datetime_t* out);
int rtc_read_hardware(rtc_datetime_t* out); /* CMOS value, without cached elapsed time or timezone conversion */

/* Seconds since 1970-01-01 00:00 of the local time (the dates of files are
 * kept in these, as FAT keeps them); rtc_now_utc(): of UTC. 0 if there is
 * no clock. The clock chip is read once, then counted with the timer. */
uint32_t rtc_now(void);
uint32_t rtc_now_utc(void);
uint32_t rtc_make_time(int year, int month, int day, int hour, int minute, int second);
void     rtc_split_time(uint32_t t, rtc_datetime_t* out);
/* sets the clock to t - local time (rtc_set_time) or UTC (rtc_set_utc),
 * seconds since 1970: 0, or -1 (outside 2000-2099) */
int      rtc_set_time(uint32_t t);
int      rtc_set_utc(uint32_t utc);
/* whether the clock chip keeps UTC (as Linux does) or the local time (as
 * Windows does: the default - a PC that also starts Windows shows the same
 * time in both); changing it keeps the time */
int      rtc_keeps_utc(void);
void     rtc_set_keeps_utc(int on);
/* (kernel/tz.c) the time zone changed: utc is the time now */
void     rtc_zone_changed(uint32_t utc);
/* "10/10/2026 16:45" (day/month/year), "" for 0 */
void     rtc_format(uint32_t t, char* out, int cap);

#endif
