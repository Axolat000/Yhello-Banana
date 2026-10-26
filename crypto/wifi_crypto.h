/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIFI_CRYPTO_H
#define WIFI_CRYPTO_H
#include "types.h"
/* WPA2 interoperability only: SHA-1 is not used for new signature schemes. */
void wifi_hmac_sha1(const uint8_t* key,uint32_t keylen,const void* data,uint32_t n,uint8_t out[20]);
int wifi_pmk(const char* pass,const uint8_t* ssid,uint32_t ssidlen,uint8_t pmk[32]);
void wifi_ptk(const uint8_t pmk[32],const uint8_t aa[6],const uint8_t spa[6],
              const uint8_t anonce[32],const uint8_t snonce[32],uint8_t ptk[48]);
/* RFC 3394, returns unwrapped length or -1; zeroes output on integrity failure. */
int wifi_key_unwrap(const uint8_t kek[16],const uint8_t* in,uint32_t n,uint8_t* out);
/* 802.11 CCMP software crypto. Header is 24 or 26 bytes (no A4/HT control).
 * Nonce priority comes from QoS TID, or zero for non-QoS frames.
 * Buffers hold just the LLC/data payload, not the CCMP header. */
int wifi_ccmp_seal(const uint8_t tk[16],const uint8_t* hdr,uint32_t hlen,uint64_t pn,
                   uint8_t* data,uint32_t n,uint8_t mic[8]);
int wifi_ccmp_open(const uint8_t tk[16],const uint8_t* hdr,uint32_t hlen,uint64_t pn,
                   uint8_t* data,uint32_t n,const uint8_t mic[8]);
#endif
