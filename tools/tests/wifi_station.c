/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../net/wifi.h"
#include "../../crypto/wifi_crypto.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void* calloc(size_t,size_t);
extern void free(void*);
static unsigned checks,sent,received,notifications,stations;
static uint32_t now,txlen;
static int fail_send;
static wifi_radio_t radio;
static uint8_t tx[2048],last_eth[1514];
#define CHECK(c) do { checks++;if(!(c)) { printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort(); } } while(0)
#include "station_vectors.h"
uint32_t timer_ms(void) { return now; }
void random_bytes(void* out,uint32_t n) { for(uint32_t i=0;i<n;i++) ((uint8_t*)out)[i]=(uint8_t)(64+i); }
void net_irq_notify(void) { notifications++; }
void net_register_device(netdev_t* nd) { nd->present=1; }
void net_unregister_device(netdev_t* nd) { nd->present=0; }
void net_rx(netdev_t* nd,const uint8_t* b,uint32_t n) {
    CHECK(nd==&radio.nd && n<=sizeof(last_eth));memcpy(last_eth,b,n);received++;
}
static int hw_poll(wifi_radio_t* r) { CHECK(r==&radio);return 0; }
static int hw_send(wifi_radio_t* r,const uint8_t* b,uint32_t n) {
    CHECK(r==&radio && n<=sizeof(tx));sent++;txlen=n;memcpy(tx,b,n);
    if(fail_send) { fail_send=0;return -1; } return 0;
}
static int hw_chan(wifi_radio_t* r,unsigned channel) { CHECK(r==&radio && channel>=1 && channel<=11);return 0; }
static int hw_station(wifi_radio_t* r,const uint8_t* bssid,unsigned aid) {
    CHECK(r==&radio && aid<=2007);
    if(bssid) CHECK(!memcmp(bssid,BEACON+10,6));
    stations++;return 0;
}
static const wifi_ops_t ops={hw_poll,hw_send,hw_chan,hw_station};
static void rx(const uint8_t* p,uint32_t n) { wifi_rx(&radio,p,n,-40); }
static void scan(void) {
    CHECK(wifi_scan(&radio)==0);rx(BEACON,sizeof(BEACON));CHECK(radio.nap==1);
    for(int i=0;i<11;i++) { now+=300;radio.nd.poll(&radio.nd); }
    CHECK(radio.state==WIFI_DOWN && !radio.nd.link_up(&radio.nd));
}
static void connect(void) {
    CHECK(wifi_connect(&radio,(const uint8_t*)"TestNet",7,"12345678",0)==0);
    CHECK(radio.state==WIFI_AUTH && tx[0]==0xb0 && txlen==30);
    rx(AUTH,sizeof(AUTH));CHECK(radio.state==WIFI_ASSOC && tx[0]==0);
    rx(ASSOC,sizeof(ASSOC));CHECK(radio.state==WIFI_KEYS && !radio.nd.link_up(&radio.nd));
}
int main(void) {
    memset(&radio,0,sizeof(radio));radio.ops=&ops;
    memcpy(radio.nd.mac,BEACON+4,6);radio.nd.mac[0]=2;
    memset(radio.nd.mac+1,0,4);radio.nd.mac[5]=1;
    CHECK(wifi_register(&radio)==0 && radio.nd.is_wifi && !radio.nd.link_up(&radio.nd));
    scan();CHECK(wifi_connect(&radio,(const uint8_t*)"TestNet",7,NULL,1)<0); /* no open downgrade */
    connect();
    rx(M1,sizeof(M1));CHECK(sent==3 && radio.handshake==1);
    CHECK(txlen==32+sizeof(M2_EXPECTED) && !memcmp(tx+32,M2_EXPECTED,sizeof(M2_EXPECTED)));
    rx(M1,sizeof(M1));CHECK(sent==4 && !memcmp(tx+32,M2_EXPECTED,sizeof(M2_EXPECTED)));
    uint8_t invalid[512];memcpy(invalid,M3,sizeof(M3));invalid[32+81]^=1;
    rx(invalid,sizeof(M3));CHECK(radio.state==WIFI_KEYS && !radio.pair.installed);
    fail_send=1;rx(M3,sizeof(M3));
    CHECK(radio.state==WIFI_KEYS && radio.pair.installed && radio.handshake==2);
    radio.pair.rx_pn[16]=7; /* retransmission must preserve counters */
    rx(M3,sizeof(M3));CHECK(radio.state==WIFI_UP && radio.nd.link_up(&radio.nd));
    CHECK(radio.pair.rx_pn[16]==7 && !memcmp(radio.active_ptk,PTK,48));
    CHECK(txlen==32+sizeof(M4_EXPECTED) && !memcmp(tx+32,M4_EXPECTED,sizeof(M4_EXPECTED)));
    uint8_t ethernet[60]={0};memset(ethernet,0xff,6);memcpy(ethernet+6,radio.nd.mac,6);
    ethernet[12]=8;ethernet[13]=0;
    CHECK(radio.nd.send(&radio.nd,ethernet,60)==0 && (tx[1]&0x40) && radio.pair.tx_pn==1);
    CHECK(txlen==32+sizeof(TX_CIPHER) && !memcmp(tx+32,TX_CIPHER,sizeof(TX_CIPHER)));
    rx(UNICAST8,sizeof(UNICAST8));CHECK(received==1 && radio.pair.rx_pn[16]==8);
    CHECK(last_eth[12]==8 && last_eth[13]==0 && !memcmp(last_eth,radio.nd.mac,6));
    for(int i=0;i<46;i++) CHECK(last_eth[14+i]==i);
    rx(UNICAST8,sizeof(UNICAST8));CHECK(received==1);
    memcpy(invalid,UNICAST9,sizeof(UNICAST9));invalid[40]^=1;
    rx(invalid,sizeof(UNICAST9));CHECK(received==1 && radio.pair.rx_pn[16]==8);
    rx(UNICAST9,sizeof(UNICAST9));CHECK(received==2 && radio.pair.rx_pn[16]==9);
    rx(M3,sizeof(M3));CHECK(radio.pair.tx_pn==1 && radio.pair.rx_pn[16]==9);
    unsigned before=sent;rx(M1,sizeof(M1));CHECK(sent==before); /* stale key negotiation */
    rx(GROUP1,sizeof(GROUP1));CHECK(radio.group[1].installed && radio.replay==3);
    rx(MULTICAST2,sizeof(MULTICAST2));CHECK(received==3 && radio.group[1].rx_pn[16]==2);
    rx(GROUP1,sizeof(GROUP1));rx(GROUP2,sizeof(GROUP2));
    CHECK(radio.group[1].rx_pn[16]==2 && radio.replay==4);
    rx(MULTICAST2,sizeof(MULTICAST2));CHECK(received==3); /* no GTK reinstallation */
    memcpy(invalid,UNICAST9,sizeof(UNICAST9));invalid[1]&=~0x40;
    rx(invalid,sizeof(UNICAST9));CHECK(received==3); /* plaintext forbidden */
    /* Fuzz untrusted beacons, key messages and data at exact allocation bounds. */
    uint32_t rng=0x11387765;
    for(unsigned i=0;i<10000;i++) {
        rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
        const uint8_t* seed=i%3==0?BEACON:i%3==1?M3:UNICAST9;
        uint32_t size=i%3==0?sizeof(BEACON):i%3==1?sizeof(M3):sizeof(UNICAST9);
        uint32_t n=i%2?size:rng%(size+1);
        uint8_t* b=calloc(1,n?n:1);CHECK(b);memcpy(b,seed,n);
        if(n) b[rng%n]^=(uint8_t)(rng>>16);
        radio.state=i%2?WIFI_SCAN:WIFI_UP;
        rx(b,n);free(b);
    }
    wifi_disconnect(&radio);CHECK(!radio.nd.link_up(&radio.nd));
    uint8_t zero[48]={0};CHECK(!memcmp(radio.pmk,zero,32) && !memcmp(radio.active_ptk,zero,48));
    scan();connect();now+=10000;radio.nd.poll(&radio.nd);CHECK(radio.state==WIFI_DOWN);
    scan();CHECK(wifi_connect(&radio,(const uint8_t*)"TestNet",7,"12345678",0)==0);
    now+=1000;radio.nd.poll(&radio.nd);
    now+=1000;radio.nd.poll(&radio.nd);now+=1000;radio.nd.poll(&radio.nd);
    now+=1000;radio.nd.poll(&radio.nd);CHECK(radio.state==WIFI_DOWN);
    wifi_unregister(&radio);CHECK(!radio.nd.present);
    printf("Wi-Fi station: %u checks passed (association, WPA2, CCMP, rekey/replay/fuzz)\n",checks);
}
