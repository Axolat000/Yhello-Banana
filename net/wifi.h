/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WIFI_H
#define WIFI_H
#include "netdev.h"
#define WIFI_AP_MAX 32
#define WIFI_FRAME_MAX 2048
enum { WIFI_DOWN, WIFI_SCAN, WIFI_AUTH, WIFI_ASSOC, WIFI_KEYS, WIFI_UP };
typedef struct {
    uint8_t bssid[6],ssid[32],ssid_len,channel,rates[16],nrates;
    uint8_t rsn[257]; uint16_t rsn_len,capability,beacon_ms;
    uint32_t seen_ms;
    int rssi,wpa2,privacy;
} wifi_ap_t;
typedef struct wifi_radio wifi_radio_t;
typedef struct {
    /* poll delivers FCS-stripped raw 802.11 frames to wifi_rx. */
    int (*poll)(wifi_radio_t*);
    /* send must copy the frame before returning; software CCMP is already applied. */
    int (*send)(wifi_radio_t*,const uint8_t*,uint32_t);
    int (*channel)(wifi_radio_t*,unsigned);
    /* NULL BSSID removes station context; aid=0 prepares authentication. */
    int (*station)(wifi_radio_t*,const uint8_t* bssid,unsigned aid);
} wifi_ops_t;
typedef struct {
    uint8_t key[16];
    uint64_t rx_pn[17],tx_pn;
    int installed;
} wifi_key_t;
struct wifi_radio {
    netdev_t nd;
    const wifi_ops_t* ops;
    void* hw;
    wifi_ap_t aps[WIFI_AP_MAX],ap;
    int nap,state,channel,retries,open,handshake;
    uint16_t sequence,aid;
    uint32_t phase_ms,beacon_ms;
    uint8_t pmk[32],anonce[32],snonce[32],ptk[48],active_ptk[48];
    uint64_t replay,installed_replay;
    int have_replay;
    wifi_key_t pair,group[4];
    uint8_t tx[WIFI_FRAME_MAX],rx[WIFI_FRAME_MAX],ethernet[ETH_FRAME_MAX];
};
int wifi_register(wifi_radio_t*);
void wifi_unregister(wifi_radio_t*);
int wifi_scan(wifi_radio_t*); /* passive channels 1..11; poll advances asynchronously */
int wifi_connect(wifi_radio_t*,const uint8_t* ssid,uint32_t len,const char* pass,int open);
void wifi_disconnect(wifi_radio_t*);
void wifi_rx(wifi_radio_t*,const uint8_t*,uint32_t len,int rssi);
const char* wifi_state_name(const wifi_radio_t*);
#endif
