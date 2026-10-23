/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NTP_H
#define NTP_H
#include "net.h"
typedef struct {
    ip4_t server;
    uint32_t utc, roundtrip_ms;
    int64_t offset_ms; /* estimate: the local RTC API has whole-second resolution */
    uint8_t stratum;
} ntp_result_t;
/* A single overall timeout includes configuration, DNS and UDP retries. */
int ntp_query(const char* host, uint32_t timeout_ms, ntp_result_t* out);
#endif
