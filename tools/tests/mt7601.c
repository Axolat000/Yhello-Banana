/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native driver, actual embedded firmware, independent model AP fixtures.
 * This is a register/protocol model, not physical RF validation. */
#include "../../net/wifi.h"
#include "../../usb/usbcore.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void* calloc(size_t,size_t);
extern void free(void*);
static unsigned checks,nalloc,registered,received,opened,submitted,controls;
static void* allocations[256];
static uint32_t now,fw_bytes,txlen;
static uint8_t regs[65536],cfg[65536],eeprom[512],rf[16][256],bbp[256],txcopy[8200],last_eth[1514];
static int short_read,bad_write,bad_ready,stuck_rf,stuck_bbp,stuck_efuse,ep_fail,stall_bulk,short_bulk;
static usb_xfer_t* pending;
static wifi_radio_t* radio;
static usb_device_t* active_dev;
#define CHECK(c) do { checks++; if(!(c)) { printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort(); } } while(0)
#include "station_vectors.h"
void* kmalloc(size_t n) { void* b=calloc(1,n);CHECK(nalloc<256);if(b) allocations[nalloc++]=b;return b; }
void* kzalloc(size_t n) { return kmalloc(n); }
void kfree(void* p) {
    for(unsigned i=0;i<nalloc;i++) if(allocations[i]==p) {
        free(p);allocations[i]=allocations[--nalloc];return;
    }
}
void* usb_dma_alloc(uint32_t n) { return kmalloc(n); }
uint32_t timer_ms(void) { return now; }
void usb_delay_ms(uint32_t n) { now+=n; }
void usb_wait(void) { now++; }
void klog(const char* f,...) { (void)f; }
void random_bytes(void* p,uint32_t n) { for(uint32_t i=0;i<n;i++) ((uint8_t*)p)[i]=(uint8_t)(64+i); }
void net_irq_notify(void) { }
void net_register_device(netdev_t* nd) { registered++;nd->present=1;radio=nd->priv; }
void net_unregister_device(netdev_t* nd) { nd->present=0; }
void net_rx(netdev_t* nd,const uint8_t* b,uint32_t n) {
    CHECK(nd==&radio->nd && n<=sizeof(last_eth));received++;memcpy(last_eth,b,n);
}
void usb_poll(void) { }
static void hc_poll(usb_hc_t* hc) { (void)hc; }
static const usb_hc_ops_t hc_ops={.poll=hc_poll};
static usb_hc_t hc={.ops=&hc_ops};
int usb_open_endpoint(usb_device_t* d,const usb_ep_desc_t* ep) {
    CHECK(d==active_dev && (ep->bEndpointAddress&15));opened++;return ep_fail?-1:0;
}
int usb_walk_config(usb_device_t* d,int c,int (*cb)(const uint8_t*,void*),void* arg) {
    const uint8_t* b=d->configs[c];if(!b) return -1;
    uint32_t n=b[2]|(uint32_t)b[3]<<8;
    for(uint32_t off=0;off<n;) {
        if(n-off<2 || b[off]<2 || b[off]>n-off) return -1;
        if(cb(b+off,arg)) return 0;
        off+=b[off];
    }
    return 0;
}
#include "../../net/mt7601u.c"

static void complete_register(unsigned reg) {
    uint32_t v=mt_le32(regs+reg);
    if(reg==MTW_RF_CSR && (v&MTW_RF_CSR_KICK)) {
        if(stuck_rf) return;
        unsigned bank=v>>MT7601_BANK_SHIFT&15,idx=v>>8&0xff;
        if(v&MTW_RF_CSR_WRITE) rf[bank][idx]=(uint8_t)v;
        mt_put32(regs+reg,(v&~(MTW_RF_CSR_KICK|0xffu))|rf[bank][idx]);
    }
    if(reg==MTW_BBP_CSR && (v&MTW_BBP_CSR_KICK)) {
        if(stuck_bbp) return;
        unsigned idx=v>>MTW_BBP_ADDR_SHIFT&0xff;
        if(v&MTW_BBP_CSR_READ) {
            if(idx==47) bbp[47]&=~0x10;
            if(idx==159 && bbp[158]==0x8c) bbp[159]=0x0c;
        } else bbp[idx]=(uint8_t)v;
        mt_put32(regs+reg,(v&~(MTW_BBP_CSR_KICK|0xffu))|bbp[idx]);
    }
    if(reg==MTW_EFUSE_CTRL && (v&MTW_EFSROM_KICK)) {
        if(stuck_efuse) return;
        unsigned offset=(v&MTW_EFSROM_AIN_MASK)>>MTW_EFSROM_AIN_SHIFT;
        CHECK(offset+16<=sizeof(eeprom));
        memcpy(regs+MTW_EFUSE_DATA0,eeprom+offset,16);
        mt_put32(regs+reg,v&~(MTW_EFSROM_KICK|MTW_EFUSE_AOUT_MASK));
    }
}
int usb_control(usb_device_t* d,uint8_t type,uint8_t req,uint16_t value,uint16_t index,void* b,uint16_t n) {
    CHECK(d==active_dev && d->present);controls++;
    if(type==(USB_TYPE_VENDOR|USB_DIR_IN)) {
        CHECK(req==MTW_READ_REGION_1 || req==MTW_READ_CFG);
        CHECK((uint32_t)index+n<=65536);
        memcpy(b,(req==MTW_READ_CFG?cfg:regs)+index,n);
        if(index==MTW_TX_STA_CNT0) memset(regs+index,0,n); /* counters clear on read */
        return short_read?n-1:n;
    }
    CHECK(type==USB_TYPE_VENDOR);
    if(bad_write) return -1;
    if(req==MTW_WRITE_2) {
        CHECK(n==0 && (uint32_t)index+2<=65536);
        mt_put16(regs+index,value);
        if((index&3)==2) complete_register(index&~3u);
        return 0;
    }
    if(req==MTW_WRITE_CFG) {
        CHECK(n==4 && (uint32_t)index+4<=65536);
        memcpy(cfg+index,b,4);return 4;
    }
    if(req==MTW_RESET && value==0x12) {
        CHECK(n==64 && !memcmp(b,mt7601_fw_start+32,64));
        CHECK(fw_bytes==mt_le32(mt7601_fw_start)-64);
        if(!bad_ready) mt_put32(cfg+MTW_MCU_DMA_ADDR,MTW_MCU_READY);
        return n;
    }
    CHECK(0);return -1;
}
int usb_submit(usb_xfer_t* x) {
    CHECK(x->dev==active_dev && x->dev->present);submitted++;
    x->status=USB_XFER_PENDING;x->actual=0;
    if(x->ep&0x80) return 0;
    if(stall_bulk) { pending=x;return 0; }
    CHECK(x->len<=sizeof(txcopy));txlen=x->len;memcpy(txcopy,x->buf,x->len);
    if(x->ep==1 && x->len>=8) {
        uint16_t flags=mt_le16(x->buf+2);
        if((flags&0x7800)==MTW_TXD_MCU) {
            unsigned size=mt_le16(x->buf);
            unsigned destination=mt_le32(cfg+MTW_MCU_DMA_ADDR);
            CHECK(size+8==x->len && destination>=0x40);
            CHECK(destination-0x40+size<=mt_le32(mt7601_fw_start)-64);
            CHECK(!memcmp(x->buf+4,mt7601_fw_start+96+destination-0x40,size));
            fw_bytes+=size;
        } else if((flags&0x7800)==(MTW_TXD_CMD|MTW_TXD_MCU)) {
            CHECK(mt_le16(x->buf)+8u==x->len);
        }
    }
    x->actual=x->len-(short_bulk && x->len?1:0);x->status=USB_XFER_OK;return 0;
}
static uint8_t config_desc[]={
    9,2,74,0,1,1,0,0x80,50,9,4,0,0,8,0xff,0xff,0xff,0,
    7,5,0x81,2,0,2,0,7,5,0x85,2,0,2,0,
    7,5,1,2,0,2,0,7,5,2,2,0,2,0,7,5,3,2,0,2,0,
    7,5,4,2,0,2,0,7,5,5,2,0,2,0,7,5,6,2,0,2,0
};
static void init(usb_device_t* d) {
    for(unsigned i=0;i<nalloc;i++) free(allocations[i]);
    nalloc=0;memset(regs,0,sizeof(regs));memset(cfg,0,sizeof(cfg));
    memset(eeprom,0,sizeof(eeprom));memset(rf,0,sizeof(rf));memset(bbp,0,sizeof(bbp));
    short_read=bad_write=bad_ready=stuck_rf=stuck_bbp=stuck_efuse=ep_fail=stall_bulk=short_bulk=0;
    fw_bytes=registered=received=opened=submitted=controls=0;pending=NULL;radio=NULL;
    memset(d,0,sizeof(*d));d->present=1;d->hc=&hc;d->speed=USB_SPEED_HIGH;
    d->desc.idVendor=0x148f;d->desc.idProduct=0x7601;d->configs[0]=config_desc;
    mt_put16(config_desc+2,sizeof(config_desc));active_dev=d;
    mt_put32(regs+MTW_ASIC_VER,0x76010001);mt_put32(regs+MTW_MAC_VER_ID,0x76010001);
    bbp[0]=0x31;bbp[49]=20;
    mt_put16(eeprom+MTW_EEPROM_CHIPID*2,0x7601);
    mt_put16(eeprom+MTW_EEPROM_ANTENNA*2,0x11);
    mt_put16(eeprom+MTW_EEPROM_MAC01*2,2);
    mt_put16(eeprom+MTW_EEPROM_MAC23*2,0);
    mt_put16(eeprom+MTW_EEPROM_MAC45*2,0x0100);
    for(int i=0;i<7;i++) {
        mt_put16(eeprom+(MTW_EEPROM_PWR2GHZ_BASE1+i)*2,0x0505);
        mt_put16(eeprom+(MTW_EEPROM_PWR2GHZ_BASE2+i)*2,0x0505);
    }
    for(int i=0;i<5;i++) mt_put32(eeprom+(MTW_EEPROM_RPWR+2*i)*2,0x11111111);
}
static void inject(const uint8_t* frame,uint32_t n,unsigned pad,uint32_t flags) {
    struct mtw_softc* sc=radio->hw;
    unsigned data=(sizeof(struct mtw_rxwi)+n+pad+3)&~3u;
    usb_xfer_t* x=&sc->rx[0];memset(x->buf,0,data+8);
    mt_put32(x->buf,data);mt_put32(x->buf+4,flags);
    mt_put16(x->buf+4+6,(uint16_t)n);
    uint8_t* body=x->buf+4+sizeof(struct mtw_rxwi);
    unsigned hlen=(frame[0]&0x80) && (frame[0]&0x0c)==8?26:24;
    memcpy(body,frame,hlen);memcpy(body+hlen+pad,frame+hlen,n-hlen);
    x->actual=data+8;x->status=USB_XFER_OK;
    radio->nd.poll(&radio->nd);
}
static void expect_eapol(const uint8_t* data,unsigned n) {
    CHECK(txlen>=4+sizeof(struct mtw_txwi)+32+n);
    CHECK(mt_le16(txcopy+2)&MTW_TXD_WIV);
    CHECK(!memcmp(txcopy+4+sizeof(struct mtw_txwi)+32,data,n));
}
int main(void) {
    usb_device_t d;init(&d);
    CHECK(mt7601u_driver.probe(&d)==0);
    CHECK(mt7601u_driver.attach(&d)==0 && registered==1 && opened==8);
    CHECK(fw_bytes==mt_le32(mt7601_fw_start)-64);
    CHECK(radio && !radio->nd.link_up(&radio->nd) && radio->nd.is_wifi && radio->nd.is_usb);
    struct mtw_softc* sc=radio->hw;
    CHECK(sc->wifi.nd.mac[0]==2 && sc->wifi.nd.mac[5]==1);
    CHECK(mt_channel(radio,0)<0 && mt_channel(radio,12)<0 && mt_channel(radio,11)==0);
    CHECK(wifi_scan(radio)==0);inject(BEACON,sizeof(BEACON),0,0);CHECK(radio->nap==1);
    for(int i=0;i<11;i++) { now+=300;radio->nd.poll(&radio->nd); }
    CHECK(wifi_connect(radio,(const uint8_t*)"TestNet",7,"12345678",0)==0);
    inject(AUTH,sizeof(AUTH),0,0);inject(ASSOC,sizeof(ASSOC),0,0);
    CHECK(radio->state==WIFI_KEYS);
    inject(M1,sizeof(M1),0,0);expect_eapol(M2_EXPECTED,sizeof(M2_EXPECTED));
    inject(M3,sizeof(M3),0,0);expect_eapol(M4_EXPECTED,sizeof(M4_EXPECTED));
    CHECK(radio->state==WIFI_UP && radio->nd.link_up(&radio->nd));
    uint8_t eth[60]={0};memset(eth,0xff,6);memcpy(eth+6,radio->nd.mac,6);eth[12]=8;
    CHECK(radio->nd.send(&radio->nd,eth,sizeof(eth))==0);
    const uint8_t* raw=txcopy+4+sizeof(struct mtw_txwi);
    CHECK((raw[1]&0x40) && !memcmp(raw+32,TX_CIPHER,sizeof(TX_CIPHER)));
    inject(UNICAST8,sizeof(UNICAST8),0,0);CHECK(received==1);
    inject(UNICAST9,sizeof(UNICAST9),2,MTW_RX_L2PAD);CHECK(received==2);
    inject(GROUP1,sizeof(GROUP1),0,0);
    inject(MULTICAST2,sizeof(MULTICAST2),0,0);CHECK(received==3);
    inject(MULTICAST2,sizeof(MULTICAST2),0,0);CHECK(received==3);
    inject(UNICAST8,sizeof(UNICAST8),0,MTW_RX_CRCERR);CHECK(received==3);
    uint8_t garbage[64]={0};mt_put32(garbage,10000);
    CHECK(mt_receive(sc,garbage,sizeof(garbage))<0);
    mt_put32(garbage,32);mt_put16(garbage+10,2000);
    CHECK(mt_receive(sc,garbage,sizeof(garbage))<0);
    /* Exact-allocation fuzzing of DMA lengths, RXWI lengths/flags and payload. */
    uint8_t seed[160]={0};
    unsigned block=(sizeof(struct mtw_rxwi)+sizeof(UNICAST9)+3)&~3u;
    CHECK(block+8<=sizeof(seed));mt_put32(seed,block);
    mt_put16(seed+10,sizeof(UNICAST9));memcpy(seed+4+sizeof(struct mtw_rxwi),UNICAST9,sizeof(UNICAST9));
    uint32_t rng=0xa1184c01;
    for(unsigned i=0;i<10000;i++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        unsigned n=i%2?block+8:rng%(block+9);
        uint8_t* packet=calloc(1,n?n:1);CHECK(packet);memcpy(packet,seed,n);
        if(n) packet[rng%n]^=(uint8_t)(rng>>16);
        mt_receive(sc,packet,n);free(packet);
    }
    CHECK(mt_rate_supported(radio,4));
    mt_put32(regs+MTW_TX_STA_CNT1,100);mt_rate_update(sc);mt_put32(regs+MTW_TX_STA_CNT1,100);mt_rate_update(sc);
    CHECK(sc->rate==5);
    mt_put32(regs+MTW_TX_STA_CNT0,100);mt_rate_update(sc);CHECK(sc->rate==4);
    d.present=0;mt7601u_driver.detach(&d);CHECK(!radio->nd.present);
    init(&d);short_read=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);short_bulk=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);bad_write=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);ep_fail=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);bad_ready=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);stuck_bbp=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);stuck_rf=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);stuck_efuse=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);stall_bulk=1;CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    CHECK(pending && pending->status==USB_XFER_PENDING && pending->dev==&d); /* no DMA UAF */
    init(&d);mt_put32(cfg+MTW_MCU_DMA_ADDR,MTW_MCU_READY);
    CHECK(mt7601u_driver.attach(&d)==0 && registered && fw_bytes==0);
    init(&d);mt_put32(regs+MTW_ASIC_VER,0x76100001);
    CHECK(mt7601u_driver.attach(&d)<0 && !registered);
    init(&d);uint8_t saved=config_desc[27];config_desc[27]=0x81;
    CHECK(mt7601u_driver.probe(&d)<0);config_desc[27]=saved;
    init(&d);d.desc.idVendor=0x0bda;d.desc.idProduct=0x8152;
    CHECK(mt7601u_driver.probe(&d)<0); /* cannot steal a working RTL8152 */
    init(&d);
    printf("MT7601U: %u checks passed (firmware/registers/USB/WPA2/CCMP/failure paths)\n",checks);
}
