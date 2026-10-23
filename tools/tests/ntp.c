/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ntp.h"
static int checks;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
static uint32_t now, utc, server_utc, dns_delay, config_delay, nonce, sends, sent_ms;
static int mode, interrupted, wait_interrupt, bind_error, send_error, pending, bound;
static udp_handler_t handler;
static void* context;
static uint8_t request[48], previous[48];
static ip4_t server=IP4(10,0,2,2);
uint32_t timer_ms(void) { return now; }
uint32_t rtc_now_utc(void) { return utc; }
uint32_t random_u32(void) { return ++nonce; }
int net_wait_configured(uint32_t t) { CHECK(t>0); now+=config_delay; return mode!=20; }
int net_interrupted(void) { return interrupted; }
int str_to_ip4(const char* s, ip4_t* out) { if (!strcmp(s,"10.0.2.2")) {*out=server;return 1;} if(!strcmp(s,"0.0.0.0")){*out=0;return 1;}return 0; }
int dns_resolve(const char* host, ip4_t* out, uint32_t t) {CHECK(host&&t>0); now+=dns_delay; *out=server; return mode==21?NET_ERR_DNS:0;}
uint16_t udp_ephemeral_port(void) {return 50000;}
int udp_bind(uint16_t port,udp_handler_t cb,void* c) {CHECK(port==50000&&!bound);if(bind_error)return -1;handler=cb;context=c;bound=1;return 0;}
void udp_unbind(uint16_t port) {CHECK(port==50000&&bound);bound=0;handler=NULL;pending=0;}
int udp_send(ip4_t ip,uint16_t s,uint16_t d,const void* p,uint32_t len) {
    CHECK(bound&&ip==server&&s==50000&&d==123&&len==48);
    memcpy(previous,request,48);memcpy(request,p,48);sends++;sent_ms=now;pending=1;
    CHECK(request[0]==0x23);
    return send_error;
}
void net_wait(uint32_t ms) {
    CHECK(ms>0&&ms<=20);now+=ms;
    if(wait_interrupt)interrupted=1;
    if (!pending || now-sent_ms<100 || mode==10) return;
    pending=0;
    uint8_t p[48]={0};p[0]=0x24;p[1]=2;
    memcpy(p+24,request+40,8);
    wr32(p+32,(uint32_t)((uint64_t)server_utc+2208988800ull));
    wr32(p+40,rd32(p+32));
    ip4_t ip=server;uint16_t port=123;uint32_t len=48;
    switch(mode) {
      case 1:ip++;break;case 2:port++;break;case 3:p[24]^=1;break;
      case 4:p[0]=0xe4;break;case 5:p[1]=0;break;case 6:p[1]=16;break;
      case 7:p[0]=0x23;break;case 8:len=47;break;case 9:p[0]=0x14;break;
      case 11:wr32(p+40,rd32(p+40)-1);break;
      case 12:wr32(p+40,rd32(p+40)+1);break;
      case 13:memcpy(p+24,previous+40,8);break;
    }
    handler(ip,port,p,len,context);
}
static void reset(void) {
    CHECK(!bound);now=dns_delay=config_delay=sends=0;
    utc=1792756800u;server_utc=utc+10;
    mode=interrupted=wait_interrupt=bind_error=send_error=pending=0;
    memset(request,0,48);memset(previous,0,48);
}
int main(void) {
    ntp_result_t r;
    reset();CHECK(ntp_query("10.0.2.2",1000,&r)==0);
    CHECK(r.utc==server_utc&&r.stratum==2&&r.roundtrip_ms==100&&r.offset_ms==9950);
    CHECK(!bound&&sends==1);
    for(int n=1;n<=13;n++) {reset();mode=n;CHECK(ntp_query(NULL,300,&r)==NET_ERR_TIMEOUT);CHECK(!bound&&now==300);}
    reset();server_utc=2085978496u;CHECK(ntp_query(NULL,300,&r)==0&&r.utc==2085978496u); /* era rollover */
    reset();server_utc=4102444798u;CHECK(ntp_query(NULL,300,&r)==0&&r.utc==server_utc);
    reset();server_utc=946684799u;CHECK(ntp_query(NULL,300,&r)==NET_ERR_TIMEOUT);
    reset();now=0xfffffff0u;CHECK(ntp_query(NULL,300,&r)==0&&r.roundtrip_ms==100);
    reset();utc=0;CHECK(ntp_query(NULL,300,&r)==0&&r.utc==server_utc); /* unset RTC */
    reset();dns_delay=300;CHECK(ntp_query(NULL,300,&r)==NET_ERR_TIMEOUT&&!bound&&!sends);
    reset();config_delay=300;CHECK(ntp_query(NULL,300,&r)==NET_ERR_TIMEOUT&&!sends);
    reset();mode=20;CHECK(ntp_query(NULL,300,&r)==NET_ERR_NOTREADY);
    reset();mode=21;CHECK(ntp_query(NULL,300,&r)==NET_ERR_DNS);
    reset();bind_error=1;CHECK(ntp_query(NULL,300,&r)==NET_ERR_NOMEM&&!bound);
    reset();send_error=NET_ERR_RESET;CHECK(ntp_query(NULL,300,&r)==NET_ERR_RESET&&!bound);
    reset();interrupted=1;CHECK(ntp_query(NULL,300,&r)==NET_ERR_INTR&&!bound);
    reset();wait_interrupt=1;CHECK(ntp_query(NULL,300,&r)==NET_ERR_INTR&&!bound);
    reset();CHECK(ntp_query(NULL,0,&r)==NET_ERR_TIMEOUT);CHECK(ntp_query(NULL,300,NULL)==NET_ERR_PROTO);
    CHECK(ntp_query("0.0.0.0",300,&r)==NET_ERR_NOROUTE);
    reset();mode=10;CHECK(ntp_query(NULL,6000,&r)==NET_ERR_TIMEOUT&&sends==3&&now==6000&&!bound);
    printf("ntp: %d checks passed\n",checks);return 0;
}
