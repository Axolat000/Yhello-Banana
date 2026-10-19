/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "tz.h"
#include "rtc.h"
#include "kstring.h"
#include "settings.h"
#include "fs.h"

/* west to east; the first is UTC (the default: the clock read as it is) */
static const tz_zone_t ZONES[] = {
    { "UTC",                            "UTC (no time zone)",                     0,    TZR_NONE },
    { "Pacific/Honolulu",               "Honolulu",                            -600,    TZR_NONE },
    { "America/Anchorage",              "Alaska",                              -540,    TZR_US },
    { "America/Los_Angeles",            "Los Angeles, Vancouver (Pacific)",    -480,    TZR_US },
    { "America/Phoenix",                "Arizona",                             -420,    TZR_NONE },
    { "America/Denver",                 "Denver, Calgary (Mountain)",          -420,    TZR_US },
    { "America/Chicago",                "Chicago, Winnipeg (Central)",         -360,    TZR_US },
    { "America/Mexico_City",            "Mexico City",                         -360,    TZR_NONE },
    { "America/New_York",               "New York, Toronto (Eastern)",         -300,    TZR_US },
    { "America/Bogota",                 "Bogota, Lima, Quito",                 -300,    TZR_NONE },
    { "America/Halifax",                "Halifax (Atlantic)",                  -240,    TZR_US },
    { "America/Caracas",                "Caracas, La Paz",                     -240,    TZR_NONE },
    { "America/Santiago",               "Santiago",                            -240,    TZR_CL },
    { "America/St_Johns",               "Newfoundland",                        -210,    TZR_US },
    { "America/Sao_Paulo",              "Sao Paulo, Rio de Janeiro",           -180,    TZR_NONE },
    { "America/Argentina/Buenos_Aires", "Buenos Aires, Montevideo",            -180,    TZR_NONE },
    { "Atlantic/Azores",                "Azores",                               -60,    TZR_EU },
    { "Atlantic/Reykjavik",             "Reykjavik, Dakar, Accra",                0,    TZR_NONE },
    { "Europe/London",                  "London, Dublin, Lisbon",                 0,    TZR_EU },
    { "Africa/Lagos",                   "Lagos, Kinshasa, Algiers",              60,    TZR_NONE },
    { "Africa/Casablanca",              "Casablanca",                            60,    TZR_NONE },
    { "Europe/Paris",                   "Paris, Brussels, Amsterdam",            60,    TZR_EU },
    { "Europe/Zurich",                  "Zurich, Geneva, Bern",                  60,    TZR_EU },
    { "Europe/Berlin",                  "Berlin, Vienna, Stockholm, Oslo",       60,    TZR_EU },
    { "Europe/Rome",                    "Rome, Madrid, Prague, Warsaw",          60,    TZR_EU },
    { "Africa/Johannesburg",            "Johannesburg, Harare",                 120,    TZR_NONE },
    { "Africa/Cairo",                   "Cairo",                                120,    TZR_NONE },
    { "Europe/Athens",                  "Athens, Bucharest, Sofia",             120,    TZR_EU },
    { "Europe/Helsinki",                "Helsinki, Kyiv, Riga, Tallinn",        120,    TZR_EU },
    { "Europe/Istanbul",                "Istanbul",                             180,    TZR_NONE },
    { "Europe/Moscow",                  "Moscow, Minsk",                        180,    TZR_NONE },
    { "Asia/Riyadh",                    "Riyadh, Baghdad, Kuwait",              180,    TZR_NONE },
    { "Africa/Nairobi",                 "Nairobi, Addis Ababa",                 180,    TZR_NONE },
    { "Asia/Tehran",                    "Tehran",                               210,    TZR_NONE },
    { "Asia/Dubai",                     "Dubai, Abu Dhabi, Baku",               240,    TZR_NONE },
    { "Asia/Kabul",                     "Kabul",                                270,    TZR_NONE },
    { "Asia/Karachi",                   "Karachi, Tashkent",                    300,    TZR_NONE },
    { "Asia/Kolkata",                   "India (Kolkata, Mumbai, Delhi)",       330,    TZR_NONE },
    { "Asia/Kathmandu",                 "Kathmandu",                            345,    TZR_NONE },
    { "Asia/Dhaka",                     "Dhaka, Almaty",                        360,    TZR_NONE },
    { "Asia/Yangon",                    "Yangon",                               390,    TZR_NONE },
    { "Asia/Bangkok",                   "Bangkok, Hanoi, Jakarta",              420,    TZR_NONE },
    { "Asia/Shanghai",                  "Beijing, Shanghai, Hong Kong",         480,    TZR_NONE },
    { "Asia/Singapore",                 "Singapore, Kuala Lumpur, Manila",      480,    TZR_NONE },
    { "Australia/Perth",                "Perth",                                480,    TZR_NONE },
    { "Asia/Tokyo",                     "Tokyo, Seoul",                         540,    TZR_NONE },
    { "Australia/Darwin",               "Darwin",                               570,    TZR_NONE },
    { "Australia/Adelaide",             "Adelaide",                             570,    TZR_AU },
    { "Australia/Brisbane",             "Brisbane",                             600,    TZR_NONE },
    { "Australia/Sydney",               "Sydney, Melbourne, Canberra",          600,    TZR_AU },
    { "Pacific/Noumea",                 "Noumea, Solomon Islands",              660,    TZR_NONE },
    { "Pacific/Auckland",               "Auckland, Wellington",                 720,    TZR_NZ },
    { "Pacific/Fiji",                   "Fiji",                                 720,    TZR_NONE },
    { "Pacific/Tongatapu",              "Nuku'alofa",                           780,    TZR_NONE },
};
#define NZONES ((int)(sizeof(ZONES) / sizeof(ZONES[0])))

static int g_zone;

/* /etc/timezone: the zone's name, where Linux programs (Java) look for it */
static void write_etc(void) {
    char line[48];
    ksnprintf(line, sizeof(line), "%s\n", ZONES[g_zone].id);
    fs_write_path("/etc/timezone", line, (uint32_t)strlen(line));
}

int tz_count(void) { return NZONES; }
const tz_zone_t* tz_get(int i) { return i >= 0 && i < NZONES ? &ZONES[i] : NULL; }

int tz_find(const char* id) {
    if (!id) return -1;
    for (int i = 0; i < NZONES; i++)
        if (!strcasecmp(ZONES[i].id, id)) return i;
    return -1;
}

/* ── summer time ─────────────────────────────────────────────────── */

static int leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int mdays(int y, int m) {
    static const uint8_t D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    return D[m - 1] + (m == 2 && leap(y));
}

/* 00:00 of the n-th Sunday of the month (n = -1: the last), as seconds since
 * 1970 counted without a zone */
static uint32_t sunday(int y, int m, int n) {
    uint32_t first = rtc_make_time(y, m, 1, 0, 0, 0);
    int wd = (int)((first / 86400u + 4u) % 7u);          /* 0: Sunday (1970-01-01 was a Thursday) */
    int day = 1 + (7 - wd) % 7;
    if (n > 0) day += 7 * (n - 1);
    else while (day + 7 <= mdays(y, m)) day += 7;
    return first + (uint32_t)(day - 1) * 86400u;
}

static int summer(int rule, int std, uint32_t utc) {
    if (rule == TZR_NONE) return 0;
    rtc_datetime_t dt;
    rtc_split_time((uint32_t)((int64_t)utc + std), &dt);
    int y = dt.year;
    int64_t u = utc;
    switch (rule) {
    case TZR_EU:
        return u >= (int64_t)sunday(y, 3, -1) + 3600 && u < (int64_t)sunday(y, 10, -1) + 3600;
    case TZR_US:
        return u >= (int64_t)sunday(y, 3, 2) + 7200 - std && u < (int64_t)sunday(y, 11, 1) + 7200 - (std + 3600);
    case TZR_AU:                                             /* (summer at the turn of the year) */
        return !(u >= (int64_t)sunday(y, 4, 1) + 10800 - (std + 3600) && u < (int64_t)sunday(y, 10, 1) + 7200 - std);
    case TZR_NZ:
        return u < (int64_t)sunday(y, 4, 1) + 10800 - (std + 3600) || u >= (int64_t)sunday(y, 9, -1) + 7200 - std;
    case TZR_CL:
        return u < (int64_t)sunday(y, 4, 1) + 10800 || u >= (int64_t)sunday(y, 9, 1) + 14400;
    }
    return 0;
}

int tz_is_summer(int i, uint32_t utc) {
    const tz_zone_t* z = tz_get(i);
    return z ? summer(z->rule, z->std_min * 60, utc) : 0;
}

int tz_offset_at(int i, uint32_t utc) {
    const tz_zone_t* z = tz_get(i);
    if (!z) return 0;
    int std = z->std_min * 60;
    return std + (summer(z->rule, std, utc) ? 3600 : 0);
}

uint32_t tz_to_local(uint32_t utc) {
    return (uint32_t)((int64_t)utc + tz_offset_at(g_zone, utc));
}

uint32_t tz_to_utc(uint32_t local) {
    const tz_zone_t* z = tz_get(g_zone);
    int std = z ? z->std_min * 60 : 0;
    uint32_t w = (uint32_t)((int64_t)local - std);           /* read as winter time */
    if (z && z->rule != TZR_NONE) {
        uint32_t s = (uint32_t)((int64_t)local - std - 3600);
        if (tz_offset_at(g_zone, s) == std + 3600) return s; /* (in the hour repeated in autumn: summer's) */
    }
    return w;
}

void tz_format_offset(int secs, char* out, int cap) {
    int neg = secs < 0;
    if (neg) secs = -secs;
    ksnprintf(out, (size_t)cap, "UTC%c%02d:%02d", neg ? '-' : '+', secs / 3600, secs / 60 % 60);
}

/* ── the zone in use ─────────────────────────────────────────────── */

int tz_current(void) { return g_zone; }

int tz_use(int i) {
    int was = g_zone;
    if (i >= 0 && i < NZONES) g_zone = i;
    return was;
}

int tz_set(int i) {
    if (i < 0 || i >= NZONES) return -1;
    uint32_t utc = rtc_now_utc();
    g_zone = i;
    write_etc();
    settings_set("timezone", ZONES[i].id);          /* (saved to the disk with it) */
    if (utc) rtc_zone_changed(utc);                          /* (a clock that keeps local time: written again) */
    return 0;
}

void tz_boot(void) {
    char v[48];
    if (settings_get("timezone", v, sizeof(v))) {
        int i = tz_find(v);
        if (i >= 0) g_zone = i;
    }
}
