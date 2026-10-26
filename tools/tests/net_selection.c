/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual interface registry, selection/debounce and persistent settings. */
#include "../../net/net.c"
#include "../../net/netconf.c"
#include "../../net/netpolicy.c"
extern int printf(const char*, ...);
extern int vsnprintf(char*, size_t, const char*, __builtin_va_list);
extern void abort(void);
static unsigned checks, starts, stops, flushed;
static uint32_t clock_ms;
static char keys[16][32], values[16][64];
static int nkeys, disk_result = 1;
#define CHECK(c) do { checks++; if (!(c)) { printf("FAIL %d: %s\n", __LINE__, #c); abort(); } } while (0)
uint32_t timer_ms(void) { return clock_ms; }
void klog(const char* fmt, ...) { (void)fmt; }
void random_add_entropy(const void* p, uint32_t n) { (void)p; (void)n; }
void arp_flush(void) { flushed++; }
void tcp_network_changed(void) { }
void ipv6_network_changed(void) { }
void dns_flush_cache(void) { }
void dhcp_start(void) { starts++; g_if.dhcp=1; }
void dhcp_stop(void) { stops++; g_if.dhcp=0; }
size_t kstrlcpy(char* d,const char* s,size_t n) {
    size_t len=strlen(s);
    if(n) { size_t c=len<n-1?len:n-1; memcpy(d,s,c); d[c]=0; }
    return len;
}
int ksnprintf(char* d,size_t n,const char* fmt,...) {
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    int r=vsnprintf(d,n,fmt,ap); __builtin_va_end(ap); return r;
}
int cfg_get(const char* path,const char* key,char* out,int cap) {
    (void)path;
    for(int i=0;i<nkeys;i++) if(!strcmp(keys[i],key) && values[i][0]) {
        kstrlcpy(out,values[i],cap); return 1;
    }
    return 0;
}
int cfg_set(const char* path,const char* key,const char* value,const char* header) {
    (void)path; (void)header;
    int i;
    for(i=0;i<nkeys;i++) if(!strcmp(keys[i],key)) break;
    CHECK(i<16);
    if(i==nkeys) { kstrlcpy(keys[i],key,32); nkeys++; }
    kstrlcpy(values[i],value?value:"",64); return 0;
}
int cfg_persist(void) { return disk_result; }
int fs_find_file(const char* path) { (void)path; return nkeys?0:-1; }
void fs_delete(const char* path,int recursive) { (void)path; (void)recursive; nkeys=0; }
int k_parse_u32(const char* s,uint32_t* v) {
    int n=0; *v=0;
    while(s[n]>='0' && s[n]<='9') { *v=*v*10+s[n]-'0'; n++; }
    return n;
}
static int carrier(netdev_t* d) { return *(int*)d->priv; }
static void tick(uint32_t ms) { clock_ms=ms; failover(); }
int main(void) {
    int wired_up=1, wifi_up=1, usb_up=1;
    netdev_t wired={.name="wired",.model="wired",.priv=&wired_up,.link_up=carrier};
    netdev_t wifi={.name="radio",.model="radio",.priv=&wifi_up,.link_up=carrier,.is_wifi=1};
    netdev_t usb={.name="usb",.model="usb",.priv=&usb_up,.link_up=carrier,.is_usb=1};
    netdev_t usb2=usb, usb3=usb;
    net_register_device(&wifi); CHECK(g_if.dev==&wifi && !strcmp(wifi.ifname,"wlan0"));
    net_register_device(&wired); CHECK(g_if.dev==&wifi && !strcmp(wired.ifname,"eth0"));
    tick(100); tick(3099); CHECK(g_if.dev==&wifi);
    tick(3100); CHECK(g_if.dev==&wired);
    net_register_device(&usb); net_register_device(&usb2);
    tick(9000); CHECK(g_if.dev==&wired); /* equal links do not flap */
    net_unregister_device(&usb); net_register_device(&usb3);
    CHECK(!strcmp(usb2.ifname,"usb1") && !strcmp(usb3.ifname,"usb0"));
    CHECK(netconf_set_priority(NET_PRIORITY_WIFI)==1 && g_if.dev==&wifi);
    CHECK(netconf_priority()==NET_PRIORITY_WIFI && !netconf_pins_iface());
    CHECK(netconf_use_dhcp()==1 && netconf_priority()==NET_PRIORITY_WIFI && !netconf_pins_iface());
    wifi_up=0; tick(10000); tick(12999); CHECK(g_if.dev==&wifi);
    tick(13000); CHECK(g_if.dev==&wired);
    wifi_up=1; tick(14000); wifi_up=0; tick(16000); wifi_up=1;
    tick(17000); tick(19999); CHECK(g_if.dev==&wired);
    tick(20000); CHECK(g_if.dev==&wifi);
    /* Configuration reload restores preference. */
    g_cfg.priority=NET_PRIORITY_AUTO; netconf_boot(); CHECK(netconf_priority()==NET_PRIORITY_WIFI);
    net_unregister_device(&wifi); CHECK(g_if.dev==&wired); /* immediate withdrawal */
    net_register_device(&wifi); tick(21000); tick(24000); CHECK(g_if.dev==&wifi);
    CHECK(netconf_save_static(&usb2,IP4(192,168,2,7),IP4(255,255,255,0),IP4(192,168,2,1),IP4(1,1,1,1))==1);
    netconf_boot(); CHECK(g_if.dev==&usb2 && !g_if.dhcp && g_if.ip==IP4(192,168,2,7));
    usb_up=0; tick(25000); tick(28000); CHECK(g_if.dev==&wifi && g_if.dhcp && !g_if.ip);
    usb_up=1; tick(29000); tick(32000); CHECK(g_if.dev==&usb2 && !g_if.dhcp && g_if.ip==IP4(192,168,2,7));
    CHECK(netconf_set_priority(NET_PRIORITY_ETHERNET)==1 && g_if.dev==&usb2);
    CHECK(!netconf_pins_iface() && !netconf_is_static() && g_if.dhcp && !g_if.gateway && !g_if.dns);
    CHECK(netconf_save_dhcp(&wired)==1);
    net_select_device(&wifi); /* temporary fallback */
    CHECK(netconf_use_dhcp()==1 && netconf_claims(&wired) && !netconf_claims(&wifi));
    CHECK(netconf_set_priority(-1)==-1 && netconf_priority()==NET_PRIORITY_ETHERNET);
    disk_result=0; CHECK(netconf_set_priority(NET_PRIORITY_AUTO)==0);
    CHECK(netconf_set_priority(NET_PRIORITY_WIFI)==0 && g_if.dev==&wifi);
    wifi_up=0; tick(0xfffffff0u); tick(0x00000ba7u); CHECK(g_if.dev==&wifi);
    tick(0x00000ba8u); CHECK(g_if.dev==&wired); /* 3 seconds across clock wrap */
    /* A radio without authenticated carrier never wins automatically. */
    netdev_t unsafe={.is_wifi=1,.present=1};
    netdev_t* radios[]={&unsafe};
    CHECK(netpolicy_choose(radios,1,NULL,NET_PRIORITY_WIFI,NULL)==NULL);
    wifi_up=0; wired_up=0; usb_up=0;
    CHECK(preferred()==NULL);
    unsigned before=starts; tick(33000); tick(39000); CHECK(starts==before);
    netconf_forget(); CHECK(netconf_priority()==NET_PRIORITY_AUTO && !netconf_pins_iface());
    /* Full registry and unplug/replug names remain unique. */
    netdev_t extra[20]; memset(extra,0,sizeof(extra));
    for(unsigned i=0;i<20;i++) net_register_device(&extra[i]);
    CHECK(g_ndevs==16);
    for(int i=0;i<g_ndevs;i++) for(int j=i+1;j<g_ndevs;j++) CHECK(strcmp(g_devs[i]->ifname,g_devs[j]->ifname));
    while(g_ndevs) net_unregister_device(g_devs[0]);
    usb_up=1; kstrlcpy(usb2.ifname,"usb0",sizeof(usb2.ifname));
    CHECK(netconf_save_static(&usb2,IP4(192,168,2,7),IP4(255,255,255,0),0,0)==0);
    net_register_device(&usb2); CHECK(g_if.dev==&usb2 && !g_if.dhcp && g_if.ip==IP4(192,168,2,7));
    net_unregister_device(&usb2); CHECK(!g_if.dev);
    net_register_device(&usb2); CHECK(g_if.dev==&usb2 && !g_if.dhcp && g_if.ip==IP4(192,168,2,7));
    CHECK(flushed && starts && stops);
    printf("Network selection: %u checks passed\n",checks);
    return 0;
}
