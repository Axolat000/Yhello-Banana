#include "banana_platform.h"
#include "kheap.h"
#include "rtc.h"
#include "timer.h"
#include "mbedtls/entropy.h"
#include "mbedtls/platform_util.h"

void* banana_tls_calloc(size_t count, size_t size) {
    if (!count || !size || count > __SIZE_MAX__ / size) return NULL;
    return kzalloc(count * size);
}

void banana_tls_free(void* ptr) { kfree(ptr); }

int64_t banana_tls_time(int64_t* out) {
    uint32_t utc = rtc_now_utc();
    int64_t t = utc ? (int64_t)utc : -1;
    if (out) *out = t;
    return t;
}

struct tm* mbedtls_platform_gmtime_r(const int64_t* t, struct tm* out) {
    if (!t || !out || *t < 0 || (uint64_t)*t > 0xffffffffu) return NULL;
    rtc_datetime_t dt;
    rtc_split_time((uint32_t)*t, &dt);
    memset(out, 0, sizeof(*out));
    out->tm_sec = dt.second;
    out->tm_min = dt.minute;
    out->tm_hour = dt.hour;
    out->tm_mday = dt.day;
    out->tm_mon = dt.month - 1;
    out->tm_year = dt.year - 1900;
    out->tm_wday = ((uint32_t)*t / 86400u + 4u) % 7u;
    out->tm_yday = ((uint32_t)*t - rtc_make_time(dt.year, 1, 1, 0, 0, 0)) / 86400u;
    return out;
}

int mbedtls_hardware_poll(void* ctx, unsigned char* out, size_t len, size_t* olen) {
    (void)ctx;
    if (!olen) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = 0;
    if (!out || banana_tls_seed(out, len)) return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
    *olen = len;
    return 0;
}

/* Mbed TLS uses this only for relative timing in the selected TLS profile. */
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)timer_ms64();
}
