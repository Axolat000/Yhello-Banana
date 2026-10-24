/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "web_policy.h"
#include "kstring.h"
#include "../net/http.h"
int web_network_url(const char* u) {
    if(!u || (strncasecmp(u,"http://",7) && strncasecmp(u,"https://",8)))return 0;
    for(const char* p=u;*p;p++)if((uint8_t)*p<=32 || *p==127)return 0;
    url_t parsed;
    return url_parse(u,&parsed) && !strchr(parsed.host,'@');
}
int web_same_origin(const char* a,const char* b) {
    url_t x,y;
    return web_network_url(a)&&web_network_url(b)&&url_parse(a,&x)&&url_parse(b,&y)&&
        x.https==y.https&&x.port==y.port&&!strcasecmp(x.host,y.host);
}
int web_endpoint_allowed(const char* principal,const char* host,uint16_t port,int family,const void* address) {
    int public=0;
    if(family==4){
        uint32_t ip=*(const uint32_t*)address;
        unsigned a=ip>>24,b=(ip>>16)&255;
        public=a && a!=10 && a!=127 && a<224 && !(a==169&&b==254) &&
               !(a==172&&b>=16&&b<=31) && !(a==192&&b==168) &&
               !(a==100&&b>=64&&b<=127) && !(a==198&&(b==18||b==19));
    }else if(family==6){
        const uint8_t* a=address;
        /* Global unicast only; mapped IPv4, loopback, ULA and link-local
         * cannot bypass the IPv4 private-network policy. */
        public=(a[0]&0xe0)==0x20;
    }
    if(public)return 1;
    url_t u;
    if(!web_network_url(principal)||!url_parse(principal,&u)||u.port!=port||strcasecmp(u.host,host))return 0;
    /* An Internet hostname never acquires LAN permission by rebinding DNS.
     * Local access requires an explicitly local top-level address. */
    uint32_t literal;
    size_t n=strlen(u.host);
    int local=str_to_ip4(u.host,&literal)||strchr(u.host,':')||
        !strcasecmp(u.host,"localhost")||!strchr(u.host,'.')||
        (n>6&&!strcasecmp(u.host+n-6,".local"));
    return local;
}
