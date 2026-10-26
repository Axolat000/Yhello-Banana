/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Bounded station-mode soft MAC: passive discovery, open-system auth,
 * association, WPA2-PSK/CCMP 4-way/group rekey and Ethernet adaptation.
 * No WEP/TKIP, WPA1, SAE/PMF, 4-address, fragmentation or A-MSDU.
 * Physical drivers must separately implement calibration, channel limits,
 * DMA/USB framing, ACK/rate handling and firmware loading. */
#include "wifi.h"
#include "wifi_crypto.h"
#include "aead.h"
#include "kstring.h"
#include "random.h"
#include "timer.h"

static const uint8_t rsn_ie[22]={48,20,1,0,0,15,172,4,1,0,0,15,172,4,1,0,0,15,172,2,0,0};
static const uint8_t snap[6]={0xaa,0xaa,3,0,0,0};
static uint16_t wle16(const uint8_t* b) { return b[0]|(uint16_t)b[1]<<8; }
static uint16_t wbe16(const uint8_t* b) { return (uint16_t)b[0]<<8|b[1]; }
static void wputle(uint8_t* b,uint16_t v) { b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8); }
static void wputbe(uint8_t* b,uint16_t v) { b[0]=(uint8_t)(v>>8);b[1]=(uint8_t)v; }
static uint64_t wbe64(const uint8_t* b) {
    uint64_t n=0; for(int i=0;i<8;i++) n=n<<8|b[i]; return n;
}
static void wput64(uint8_t* b,uint64_t n) { for(int i=7;i>=0;i--) { b[i]=(uint8_t)n;n>>=8; } }
static void wzero(void* p,uint32_t n) { volatile uint8_t* b=p;while(n--) *b++=0; }
static int suite(const uint8_t* b,uint8_t type) { return b[0]==0 && b[1]==15 && b[2]==172 && b[3]==type; }
/* Full bounds validation before using untrusted beacon/key-data RSN fields. */
static int rsn_ccmp(const uint8_t* p,uint32_t n) {
    if(n<18 || wle16(p)!=1 || !suite(p+2,4)) return 0;
    unsigned count=wle16(p+6);uint32_t at=8;int ccmp=0,psk=0;
    if(!count || count>(n-at)/4) return 0;
    for(unsigned i=0;i<count;i++,at+=4) if(suite(p+at,4)) ccmp=1;
    if(n-at<2) return 0;
    count=wle16(p+at);at+=2;
    if(!count || count>(n-at)/4) return 0;
    for(unsigned i=0;i<count;i++,at+=4) if(suite(p+at,2)) psk=1;
    if(n-at==1) return 0;
    if(n-at>=2) {
        if(wle16(p+at)&0x40) return 0; /* PMF required: do not falsely negotiate it */
        at+=2;
    }
    if(n-at) {
        if(n-at<2) return 0;
        count=wle16(p+at);at+=2;
        if(count>(n-at)/16) return 0;
        at+=count*16;
        if(n-at!=0 && n-at!=4) return 0;
    }
    return ccmp && psk;
}
static uint32_t header(wifi_radio_t* r,uint8_t subtype,int data,const uint8_t dst[6]) {
    uint8_t* h=r->tx;memset(h,0,24);h[0]=subtype;h[1]=data?1:0;
    memcpy(h+4,r->ap.bssid,6);memcpy(h+10,r->nd.mac,6);
    memcpy(h+16,data?dst:r->ap.bssid,6);
    wputle(h+22,(uint16_t)(r->sequence++<<4));return 24;
}
static int raw(wifi_radio_t* r,uint32_t n) {
    if(!r->nd.present || n>sizeof(r->tx)) return -1;
    return r->ops->send(r,r->tx,n);
}
static int auth(wifi_radio_t* r) {
    header(r,0xb0,0,NULL);memset(r->tx+24,0,6);wputle(r->tx+26,1);
    r->phase_ms=timer_ms();return raw(r,30);
}
static int assoc(wifi_radio_t* r) {
    header(r,0,0,NULL);
    uint16_t cap=1|(r->ap.capability&0x0420);
    if(!r->open) cap|=0x10;
    wputle(r->tx+24,cap);wputle(r->tx+26,10);
    uint32_t at=28;r->tx[at++]=0;r->tx[at++]=r->ap.ssid_len;
    memcpy(r->tx+at,r->ap.ssid,r->ap.ssid_len);at+=r->ap.ssid_len;
    unsigned first=r->ap.nrates<8?r->ap.nrates:8;
    r->tx[at++]=1;r->tx[at++]=(uint8_t)first;memcpy(r->tx+at,r->ap.rates,first);at+=first;
    if(r->ap.nrates>first) {
        r->tx[at++]=50;r->tx[at++]=r->ap.nrates-first;
        memcpy(r->tx+at,r->ap.rates+first,r->ap.nrates-first);at+=r->ap.nrates-first;
    }
    if(!r->open) { memcpy(r->tx+at,rsn_ie,sizeof(rsn_ie));at+=sizeof(rsn_ie); }
    r->phase_ms=timer_ms();return raw(r,at);
}
static int eapol_send(wifi_radio_t* r,uint16_t info,uint64_t replay,const uint8_t* nonce,
                      const uint8_t* extra,uint16_t n,const uint8_t ptk[48]) {
    if(n>256) return -1;
    header(r,8,1,r->ap.bssid);memcpy(r->tx+24,snap,6);
    r->tx[30]=0x88;r->tx[31]=0x8e;
    uint8_t* e=r->tx+32;memset(e,0,99+n);
    e[0]=2;e[1]=3;wputbe(e+2,95+n);e[4]=2;
    wputbe(e+5,info|2);wput64(e+9,replay);
    if(nonce) memcpy(e+17,nonce,32);
    wputbe(e+97,n);if(n) memcpy(e+99,extra,n);
    uint8_t mic[20];wifi_hmac_sha1(ptk,16,e,99+n,mic);memcpy(e+81,mic,16);
    wzero(mic,20);return raw(r,32+99+n);
}
static int mic_valid(const uint8_t key[16],const uint8_t* e,uint32_t n) {
    uint8_t data[512],mac[20];
    if(n>sizeof(data)) return 0;
    memcpy(data,e,n);memset(data+81,0,16);
    wifi_hmac_sha1(key,16,data,n,mac);
    int good=!ct_memcmp(mac,e+81,16);wzero(data,n);wzero(mac,20);return good;
}
static void install_key(wifi_key_t* k,const uint8_t key[16],uint64_t rsc) {
    /* A retransmitted M3/group rekey must never reset packet-number state
     * for an already installed key (KRACK/replay protection). */
    if(k->installed && !ct_memcmp(k->key,key,16)) return;
    wzero(k,sizeof(*k));memcpy(k->key,key,16);k->installed=1;
    for(int i=0;i<17;i++) k->rx_pn[i]=rsc;
}
static int group_data(wifi_radio_t* r,const uint8_t* e,uint32_t n,const uint8_t kek[16],int need_rsn) {
    uint8_t data[512],gtk[16];uint32_t len=wbe16(e+97);
    if(n!=99+len || !(wbe16(e+5)&0x1000)) return -1;
    int plain=wifi_key_unwrap(kek,e+99,len,data);
    if(plain<0) return -1;
    uint32_t at=0;int found=0,rsn=0,kid=0;
    while(at<(uint32_t)plain) {
        /* IEEE 802.11 key-data padding is DD followed by zero bytes. */
        if(data[at]==0xdd && (at+1==(uint32_t)plain || data[at+1]==0)) {
            for(uint32_t j=at+1;j<(uint32_t)plain;j++) if(data[j]) goto bad;
            break;
        }
        if((uint32_t)plain-at<2 || data[at+1]>(uint32_t)plain-at-2) goto bad;
        uint32_t elen=data[at+1];const uint8_t* p=data+at+2;
        if(data[at]==48) {
            if(rsn || !rsn_ccmp(p,elen)) goto bad;
            rsn=1;
        }
        if(data[at]==0xdd && elen>=4 && suite(p,1)) {
            if(found || elen!=22 || (p[4]&~7) || p[5]) goto bad;
            kid=p[4]&3;memcpy(gtk,p+6,16);found=1;
        }
        at+=2+elen;
    }
    if(!found || (need_rsn && !rsn)) goto bad;
    uint64_t pn=0;for(int i=5;i>=0;i--) pn=pn<<8|e[65+i];
    install_key(&r->group[kid],gtk,pn);wzero(data,sizeof(data));wzero(gtk,16);return 0;
bad:
    wzero(data,sizeof(data));wzero(gtk,16);return -1;
}
static void eapol(wifi_radio_t* r,const uint8_t* e,uint32_t available) {
    if(r->open || (r->state!=WIFI_KEYS && r->state!=WIFI_UP) || available<99 ||
       (e[0]!=1 && e[0]!=2) || e[1]!=3 || e[4]!=2) return;
    uint32_t n=4+wbe16(e+2);
    if(n<99 || n>available || n>512 || wbe16(e+97)!=n-99) return;
    uint16_t info=wbe16(e+5);
    if((info&7)!=2 || (info&0xec30) || !(info&0x80)) return; /* no requests/error/reserved/index */
    uint64_t counter=wbe64(e+9);
    if(r->have_replay && counter<r->replay) return;
    if((info&0x108)==8) { /* Pairwise M1: ACK, no MIC */
        if(info!=0x8a || wbe16(e+7)!=16) return;
        if(r->have_replay && counter==r->replay) {
            if(r->handshake==1 && !ct_memcmp(r->anonce,e+17,32))
                eapol_send(r,0x108,counter,r->snonce,rsn_ie,sizeof(rsn_ie),r->ptk);
            return;
        }
        uint8_t any=0;for(int i=0;i<32;i++) any|=e[17+i];if(!any) return;
        /* A newer retransmitted M1 with the same ANonce reuses SNonce. */
        if(!r->handshake || ct_memcmp(r->anonce,e+17,32)) random_bytes(r->snonce,32);
        memcpy(r->anonce,e+17,32);
        wifi_ptk(r->pmk,r->ap.bssid,r->nd.mac,r->anonce,r->snonce,r->ptk);
        r->handshake=1;r->replay=counter;r->have_replay=1;
        r->phase_ms=timer_ms();
        eapol_send(r,0x108,counter,r->snonce,rsn_ie,sizeof(rsn_ie),r->ptk);
        return;
    }
    if((info&0x108)==0x108) { /* pairwise M3 */
        if((info&0x13ca)!=0x13ca || wbe16(e+7)!=16 || !r->handshake ||
           ct_memcmp(r->anonce,e+17,32) || !mic_valid(r->ptk,e,n)) return;
        if(r->handshake==2 && counter==r->installed_replay) {
            if(eapol_send(r,0x308,counter,NULL,NULL,0,r->ptk)==0) {
                int newly_up=r->state!=WIFI_UP;
                r->state=WIFI_UP;r->beacon_ms=timer_ms();
                if(newly_up) net_irq_notify();
            }
            return;
        }
        if(counter<=r->replay || group_data(r,e,n,r->ptk+16,1)) return;
        install_key(&r->pair,r->ptk+32,0);
        memcpy(r->active_ptk,r->ptk,48);
        r->handshake=2;r->replay=r->installed_replay=counter;
        if(eapol_send(r,0x308,counter,NULL,NULL,0,r->ptk)<0) return;
        r->state=WIFI_UP;r->beacon_ms=timer_ms();net_irq_notify();return;
    }
    /* RSN group rekey. Both the old pairwise channel and valid KCK are
     * required; duplicate counters are acknowledged without key reinstall. */
    if(r->state!=WIFI_UP || (info&0x13ca)!=0x1382 ||
       (wbe16(e+7)!=0 && wbe16(e+7)!=16) || !mic_valid(r->active_ptk,e,n)) return;
    if(counter>r->replay) {
        if(group_data(r,e,n,r->active_ptk+16,0)) return;
        r->replay=counter;
    } else if(counter!=r->replay) return;
    eapol_send(r,0x300,counter,NULL,NULL,0,r->active_ptk);
}
static void beacon(wifi_radio_t* r,const uint8_t* b,uint32_t n,int rssi) {
    if(n<36 || (b[10]&1) || memcmp(b+10,b+16,6)) return;
    if(r->state>=WIFI_AUTH && !memcmp(b+10,r->ap.bssid,6)) r->beacon_ms=timer_ms();
    if(r->state!=WIFI_SCAN) return;
    wifi_ap_t ap;memset(&ap,0,sizeof(ap));memcpy(ap.bssid,b+10,6);
    ap.capability=wle16(b+34);ap.privacy=(ap.capability&0x10)!=0;
    ap.beacon_ms=wle16(b+32);ap.rssi=rssi;ap.channel=(uint8_t)r->channel;
    int have_ssid=0,have_rsn=0;
    for(uint32_t at=36;at<n;) {
        if(n-at<2 || b[at+1]>n-at-2) return;
        unsigned len=b[at+1];const uint8_t* p=b+at+2;
        if(b[at]==0) {
            if(have_ssid || len>32) return;
            have_ssid=1;ap.ssid_len=(uint8_t)len;memcpy(ap.ssid,p,len);
        } else if(b[at]==3) {
            if(len!=1 || p[0]!=r->channel) return;
            ap.channel=p[0];
        } else if(b[at]==1 || b[at]==50) {
            if(ap.nrates+len>sizeof(ap.rates)) return;
            memcpy(ap.rates+ap.nrates,p,len);ap.nrates+=(uint8_t)len;
        } else if(b[at]==48) {
            if(have_rsn) return;
            have_rsn=1;ap.rsn_len=(uint16_t)(len+2);memcpy(ap.rsn,b+at,len+2);
            ap.wpa2=ap.privacy && rsn_ccmp(p,len);
        }
        at+=len+2;
    }
    if(!have_ssid || !ap.ssid_len || !ap.nrates || !(ap.capability&1)) return;
    ap.seen_ms=timer_ms();
    int slot;
    for(slot=0;slot<r->nap;slot++) if(!memcmp(r->aps[slot].bssid,ap.bssid,6)) break;
    if(slot==WIFI_AP_MAX) return;
    if(slot==r->nap) r->nap++;
    r->aps[slot]=ap;
}
void wifi_rx(wifi_radio_t* r,const uint8_t* b,uint32_t n,int rssi) {
    if(!r->nd.present || n<24 || n>WIFI_FRAME_MAX || (b[0]&3) || (b[1]&4) || (b[22]&15)) return;
    if((b[0]&0x0c)==0) {
        if(b[1]&0x43) return;
        if(b[0]==0x80 || b[0]==0x50) { beacon(r,b,n,rssi);return; }
        if(r->state<WIFI_AUTH || memcmp(b+4,r->nd.mac,6) ||
           memcmp(b+10,r->ap.bssid,6) || memcmp(b+16,r->ap.bssid,6)) return;
        if(b[0]==0xb0 && r->state==WIFI_AUTH && n>=30) {
            if(wle16(b+24) || wle16(b+26)!=2 || wle16(b+28)) return;
            r->state=WIFI_ASSOC;r->retries=0;if(assoc(r)) wifi_disconnect(r);
        } else if(b[0]==0x10 && r->state==WIFI_ASSOC && n>=30) {
            if(wle16(b+26)) { wifi_disconnect(r);return; }
            r->aid=wle16(b+28)&0x3fff;
            if(!r->aid || r->aid>2007 || r->ops->station(r,r->ap.bssid,r->aid)) { wifi_disconnect(r);return; }
            r->state=r->open?WIFI_UP:WIFI_KEYS;r->phase_ms=r->beacon_ms=timer_ms();
            if(r->open) net_irq_notify();
        } else if((b[0]==0xc0 || b[0]==0xa0) && n>=26) wifi_disconnect(r);
        return;
    }
    if((b[0]&0x0c)!=8 || (b[1]&3)!=2 || (b[1]&0x80) ||
       (b[0]&0x40) || memcmp(b+10,r->ap.bssid,6) ||
       (!(b[4]&1) && memcmp(b+4,r->nd.mac,6))) return;
    uint32_t hlen=b[0]&0x80?26:24;
    if(n<hlen || (hlen==26 && (b[24]&0x80))) return; /* no A-MSDU */
    unsigned tid=hlen==26?b[24]&15:16;
    const uint8_t* payload=b+hlen;uint32_t plen=n-hlen;
    int protected=(b[1]&0x40)!=0;
    if(protected) {
        if(r->state!=WIFI_UP || r->open || plen<16 || plen>WIFI_FRAME_MAX-hlen) return;
        unsigned kid=payload[3]>>6;
        if(payload[2] || (payload[3]&0x3f)!=0x20) return;
        wifi_key_t* key=(b[4]&1)?&r->group[kid]:&r->pair;
        if(!(b[4]&1) && kid) return;
        uint64_t pn=0;
        for(int i=7;i>=4;i--) pn=pn<<8|payload[i];
        pn=pn<<8|payload[1];pn=pn<<8|payload[0];
        if(!key->installed || pn<=key->rx_pn[tid]) return;
        plen-=16;memcpy(r->rx,payload+8,plen);
        if(wifi_ccmp_open(key->key,b,hlen,pn,r->rx,plen,payload+8+plen)) return;
        key->rx_pn[tid]=pn;payload=r->rx;
    }
    if(plen<8 || memcmp(payload,snap,6)) return;
    uint16_t type=wbe16(payload+6);
    if(type==0x888e) { eapol(r,payload+8,plen-8);return; }
    if(r->state!=WIFI_UP || (!r->open && !protected) || plen-8>1500) return;
    memcpy(r->ethernet,b+4,6);memcpy(r->ethernet+6,b+16,6);
    memcpy(r->ethernet+12,payload+6,2);memcpy(r->ethernet+14,payload+8,plen-8);
    r->nd.rx_packets++;r->nd.rx_bytes+=plen+6;
    net_rx(&r->nd,r->ethernet,plen+6);
}
void wifi_disconnect(wifi_radio_t* r) {
    int was_up=r->state==WIFI_UP;
    r->state=WIFI_DOWN;r->handshake=r->have_replay=0;
    r->replay=r->installed_replay=0;r->aid=0;
    wzero(r->pmk,sizeof(r->pmk));wzero(r->ptk,sizeof(r->ptk));
    wzero(r->active_ptk,sizeof(r->active_ptk));wzero(r->anonce,32);wzero(r->snonce,32);
    wzero(&r->pair,sizeof(r->pair));wzero(r->group,sizeof(r->group));
    if(r->ops->station) r->ops->station(r,NULL,0);
    if(was_up) net_irq_notify();
}
static int wifi_carrier(netdev_t* nd) { return ((wifi_radio_t*)nd->priv)->state==WIFI_UP; }
static int wifi_send(netdev_t* nd,const void* frame,uint32_t n) {
    wifi_radio_t* r=nd->priv;const uint8_t* e=frame;
    if(r->state!=WIFI_UP || n<14 || n>ETH_FRAME_MAX) return -1;
    header(r,8,1,e);
    uint32_t at=24;
    if(!r->open) {
        if(!r->pair.installed || r->pair.tx_pn>=(((uint64_t)1<<48)-1)) return -1;
        uint64_t pn=++r->pair.tx_pn;r->tx[1]|=0x40;
        uint8_t* iv=r->tx+at;memset(iv,0,8);iv[3]=0x20;
        iv[0]=(uint8_t)pn;iv[1]=(uint8_t)(pn>>8);
        for(int i=0;i<4;i++) iv[4+i]=(uint8_t)(pn>>(16+8*i));
        at+=8;
    }
    memcpy(r->tx+at,snap,6);memcpy(r->tx+at+6,e+12,2);
    memcpy(r->tx+at+8,e+14,n-14);uint32_t size=n-6;
    if(!r->open) {
        if(wifi_ccmp_seal(r->pair.key,r->tx,24,r->pair.tx_pn,r->tx+at,size,r->tx+at+size)) return -1;
        size+=8;
    }
    if(raw(r,at+size)) { nd->tx_errors++;return -1; }
    nd->tx_packets++;nd->tx_bytes+=n;return 0;
}
static int wifi_poll(netdev_t* nd) {
    wifi_radio_t* r=nd->priv;
    if(!nd->present) return 0;
    int received=r->ops->poll(r);uint32_t now=timer_ms();
    if(r->state==WIFI_SCAN && now-r->phase_ms>=300) {
        if(++r->channel>11) r->state=WIFI_DOWN;
        else if(r->ops->channel(r,r->channel)) r->state=WIFI_DOWN;
        r->phase_ms=now;
    } else if((r->state==WIFI_AUTH || r->state==WIFI_ASSOC) && now-r->phase_ms>=1000) {
        if(r->retries++>=3 || (r->state==WIFI_AUTH?auth(r):assoc(r))) wifi_disconnect(r);
    } else if(r->state==WIFI_KEYS && now-r->phase_ms>=10000) wifi_disconnect(r);
    else if(r->state==WIFI_UP && now-r->beacon_ms>=30000) wifi_disconnect(r);
    return received;
}
wifi_radio_t* wifi_from_device(netdev_t* nd) {
    return nd && nd->is_wifi && nd->poll==wifi_poll ? nd->priv : NULL;
}
int wifi_register(wifi_radio_t* r) {
    if(!r || !r->ops || !r->ops->poll || !r->ops->send || !r->ops->channel || !r->ops->station) return -1;
    r->state=WIFI_DOWN;r->nap=0;
    r->nd.is_wifi=1;r->nd.priv=r;
    r->nd.poll=wifi_poll;r->nd.send=wifi_send;r->nd.link_up=wifi_carrier;
    net_register_device(&r->nd);return r->nd.present?0:-1;
}
void wifi_unregister(wifi_radio_t* r) { wifi_disconnect(r);net_unregister_device(&r->nd); }
int wifi_scan(wifi_radio_t* r) {
    if(!r || !r->nd.present || r->state!=WIFI_DOWN) return -1;
    r->nap=0;r->channel=1;
    if(r->ops->channel(r,1)) return -1;
    r->state=WIFI_SCAN;r->phase_ms=timer_ms();return 0;
}
int wifi_connect(wifi_radio_t* r,const uint8_t* ssid,uint32_t n,const char* pass,int open) {
    if(!r || !r->nd.present || r->state!=WIFI_DOWN || !n || n>32) return -1;
    wifi_ap_t* best=NULL;
    for(int i=0;i<r->nap;i++) {
        wifi_ap_t* ap=&r->aps[i];
        if(ap->ssid_len!=n || memcmp(ap->ssid,ssid,n) || timer_ms()-ap->seen_ms>30000 ||
           (open?ap->privacy:!ap->wpa2)) continue;
        int supported=1;
        for(unsigned j=0;j<ap->nrates;j++) {
            uint8_t rate=ap->rates[j]&0x7f;
            if((ap->rates[j]&0x80) && rate!=2 && rate!=4 && rate!=11 && rate!=22 &&
               rate!=12 && rate!=18 && rate!=24 && rate!=36 && rate!=48 && rate!=72 && rate!=96 && rate!=108) supported=0;
        }
        if(supported && (!best || ap->rssi>best->rssi)) best=ap;
    }
    if(!best || (!open && (!pass || wifi_pmk(pass,ssid,n,r->pmk)))) return -1;
    r->ap=*best;r->open=open;r->retries=0;r->handshake=r->have_replay=0;
    if(r->ops->channel(r,r->ap.channel) || r->ops->station(r,r->ap.bssid,0)) { wifi_disconnect(r);return -1; }
    r->channel=r->ap.channel;r->state=WIFI_AUTH;r->beacon_ms=timer_ms();
    if(auth(r)) { wifi_disconnect(r);return -1; }
    return 0;
}
const char* wifi_state_name(const wifi_radio_t* r) {
    static const char* names[]={"disconnected","scanning","authenticating","associating","WPA2 handshake","connected"};
    return (unsigned)r->state<6?names[r->state]:"invalid";
}
