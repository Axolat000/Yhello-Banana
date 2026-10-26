/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual native flyout and keyboard navigation, simulated registered radios.
 * The station/radio drivers are covered separately; this validates UI actions. */
#include "../../kernel/nettray.c"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
static unsigned checks;
#define CHECK(c) do { checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();} }while(0)
static netif_t interface;
static netdev_t wired[16],*devices[16];
static wifi_radio_t radios[2];
static int count,scans,connects,disconnects,connect_fail;
static char submitted[65],frame[16384];
static int submitted_open;
static unsigned submitted_len;
static uint8_t submitted_ssid[32];
static uint32_t clock_ms;
netif_t* net_if(void) { return &interface; }
int net_device_count(void) { return count; }
netdev_t* net_device_at(int i) { return i>=0 && i<count?devices[i]:NULL; }
void net_select_device(netdev_t* d) { interface.dev=d; }
void ip4_to_str(ip4_t ip,char out[16]) { (void)ip;kstrlcpy(out,"10.0.2.15",16); }
uint32_t timer_ms(void) { return clock_ms; }
uint32_t theme_color(uint32_t c) { return c; }
const char* tr(const char* s) { return s; }
void gfx_fill_rect(int x,int y,int w,int h,uint32_t c) {
    (void)c;CHECK(x>=0 && y>=0 && w>=0 && h>=0 && x+w<=640 && y+h<=480);
}
void gfx_draw_text(int x,int y,const char* t,uint32_t f,uint32_t b) {
    (void)f;(void)b;CHECK(x>=0 && y>=0 && x+(int)strlen(t)*8<=640 && y+8<=480);
    kstrlcat(frame,t,sizeof(frame));kstrlcat(frame,"\n",sizeof(frame));
}
wifi_radio_t* wifi_from_device(netdev_t* d) { return d && d->is_wifi?d->priv:NULL; }
const char* wifi_state_name(const wifi_radio_t* r) {
    static const char* n[]={"disconnected","scanning","authenticating","associating","WPA2 handshake","connected"};
    return n[r->state];
}
int wifi_scan(wifi_radio_t* r) { scans++;if(r->state!=WIFI_DOWN) return -1;r->nap=0;r->state=WIFI_SCAN;return 0; }
int wifi_connect(wifi_radio_t* r,const uint8_t* s,uint32_t n,const char* pass,int open) {
    CHECK(r->nd.present && r->state==WIFI_DOWN);CHECK(n && n<=32);
    connects++;kstrlcpy(submitted,pass,sizeof(submitted));submitted_open=open;
    submitted_len=n;memcpy(submitted_ssid,s,n);
    if(connect_fail) return -1;
    r->state=WIFI_AUTH;return 0;
}
void wifi_disconnect(wifi_radio_t* r) { disconnects++;r->state=WIFI_DOWN; }
static void draw(void) { frame[0]=0;nettray_draw();CHECK(!strstr(frame,"TestSecret42")); }
static int find(int action,int value) {
    for(int i=0;i<g_nbtn;i++) if(g_btn[i].action==action && (value<0 || g_btn[i].value==value)) return i;
    CHECK(0);return -1;
}
static int click(int action,int value) {
    int b=find(action,value);
    return nettray_click(g_btn[b].x+g_btn[b].w/2,g_btn[b].y+g_btn[b].h/2);
}
static void type(const char* s) { while(*s) nettray_key((unsigned char)*s++); }
static void erased(void) { for(unsigned i=0;i<sizeof(g_password);i++) CHECK(!g_password[i]); }
static void networks(wifi_radio_t* r) {
    r->state=WIFI_DOWN;r->nap=9;
    for(int i=0;i<r->nap;i++) {
        wifi_ap_t* a=&r->aps[i];memset(a,0,sizeof(*a));
        a->ssid_len=32;memset(a->ssid,'a'+i,32);a->bssid[5]=(uint8_t)i;
        a->privacy=i!=1;a->wpa2=i!=1 && i!=2;a->rssi=-45-i*7;
    }
    r->aps[3].ssid[0]=27; /* control characters must not reach the renderer */
}
static void setup(void) {
    nettray_close();memset(&interface,0,sizeof(interface));memset(radios,0,sizeof(radios));
    count=2;
    for(int i=0;i<2;i++) {
        wifi_radio_t* r=&radios[i];r->nd.present=1;r->nd.is_wifi=1;r->nd.is_usb=1;r->nd.priv=r;
        ksnprintf(r->nd.ifname,sizeof(r->nd.ifname),"wlan%d",i);r->nd.model="MediaTek MT7601U";
        devices[i]=&r->nd;networks(r);
    }
    g_radio=NULL;g_radio_index=0;
    nettray_toggle(636,452);draw();
}
int main(void) {
    count=0;nettray_toggle(636,452);draw();
    click(ACT_WIFI,0);draw();CHECK(strstr(frame,"No supported Wi-Fi"));nettray_key(27);erased();
    setup();click(ACT_WIFI,0);CHECK(scans==1);draw();CHECK(strstr(frame,"Scanning"));
    networks(&radios[0]);draw();CHECK(strstr(frame,"WPA2") && !strcmp(g_message,"Select a network."));
    click(ACT_AP,0);type("TestSecret42");draw();
    CHECK(strlen(g_password)==12 && !strcmp(g_password,"TestSecret42"));
    CHECK(!nettray_key('\n'));CHECK(connects==1 && !strcmp(submitted,"TestSecret42") && !submitted_open);
    CHECK(submitted_len==32 && !memcmp(submitted_ssid,radios[0].aps[0].ssid,32));erased();
    radios[0].state=WIFI_UP;draw();CHECK(!strcmp(g_message,"Connected."));
    click(ACT_DISCONNECT,0);CHECK(disconnects==1);
    draw();click(ACT_AP,1);draw();click(ACT_CONNECT,0);
    CHECK(connects==2 && submitted_open && !submitted[0]);erased();
    radios[0].state=WIFI_DOWN;draw();click(ACT_AP,2);type("password");
    draw();click(ACT_CONNECT,0);CHECK(connects==2);erased(); /* unsupported security */
    draw();click(ACT_AP,0);type("TestSecret42");radios[0].aps[0].bssid[0]^=1;
    draw();click(ACT_CONNECT,0);CHECK(connects==2 && g_selected<0);erased(); /* stale AP */
    draw();click(ACT_NEXT,0);draw();CHECK(g_ap_page==1);
    click(ACT_AP,4);type("TestSecret42");draw();click(ACT_RADIO,0);erased();
    CHECK(g_radio==&radios[1]);networks(&radios[1]);draw();
    click(ACT_AP,0);type("TestSecret42");
    draw();radios[1].nd.present=0;click(ACT_CONNECT,0);erased();CHECK(g_selected<0);
    draw();
    networks(&radios[0]);draw();click(ACT_AP,0);type("TestSecret42");
    nettray_close();erased();CHECK(!nettray_is_open());
    setup();click(ACT_WIFI,0);networks(&radios[0]);draw();click(ACT_AP,0);
    type("TestSecret42");nettray_toggle(636,452);erased();CHECK(!nettray_is_open());
    setup();click(ACT_WIFI,0);networks(&radios[0]);draw();click(ACT_AP,0);
    type("12345678");connect_fail=1;nettray_key('\n');erased();CHECK(strstr(g_message,"Could not connect"));
    connect_fail=0;draw();click(ACT_AP,0);
    for(int i=0;i<100;i++) nettray_key('x');
    CHECK(strlen(g_password)==64);nettray_key('\b');CHECK(strlen(g_password)==63);
    nettray_key(KB_ESC);erased();
    setup();click(ACT_WIFI,0);draw();
    CHECK(click(ACT_WIFI_SETTINGS,0)==NETTRAY_WIFI_SETTINGS && !nettray_is_open());
    setup();CHECK(click(ACT_SETTINGS,0)==NETTRAY_SETTINGS && !nettray_is_open());
    /* Existing Ethernet details and all 16 adapters fit a 640x480 desktop. */
    nettray_close();count=16;
    for(int i=0;i<16;i++) {
        memset(&wired[i],0,sizeof(wired[i]));wired[i].present=1;wired[i].name="e1000";
        ksnprintf(wired[i].ifname,sizeof(wired[i].ifname),"eth%d",i);devices[i]=&wired[i];
    }
    interface.dev=&wired[0];interface.configured=1;interface.ip=1;
    nettray_toggle(636,452);
    for(int page=0;page<8;page++) {
        draw();CHECK(g_adapter_page==page);
        click(ACT_CARDS,0);
    }
    draw();CHECK(!g_adapter_page);
    click(ACT_WIFI,0);draw();nettray_key(KB_TAB);draw();CHECK(g_nav.shown);
    nettray_key(KB_ENTER);draw();CHECK(!g_wifi_mode);
    /* Signature notices scan/association progress even while Ethernet is active. */
    setup();click(ACT_WIFI,0);networks(&radios[0]);draw();
    uint32_t sig=nettray_sig();radios[0].state=WIFI_AUTH;CHECK(nettray_sig()!=sig);
    nt_conn_t c[NT_MAX];CHECK(collect(c,NT_MAX)==2 && c[0].kind==NT_WIFI);
    radios[0].state=WIFI_UP;radios[0].ap.rssi=-40;collect(c,NT_MAX);CHECK(c[0].signal==4);
    nettray_close();sig=nettray_sig();radios[0].ap.rssi=-80;CHECK(nettray_sig()!=sig);
    interface.dev=&radios[0].nd;radios[0].state=WIFI_DOWN;interface.configured=0;
    char tooltip[80];nettray_summary(tooltip,sizeof(tooltip));CHECK(strstr(tooltip,"disconnected"));
    clock_ms=500;
    nettray_close();erased();
    printf("Network flyout: %u checks passed\n",checks);
    return 0;
}
