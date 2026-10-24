/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../kernel/web_policy.h"
#include "../../net/net.h"
#include "../../net/ipv6.h"
extern int printf(const char*,...);
extern void exit(int);
static int checks;
#define CHECK(x) do{checks++;if(!(x)){printf("FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    CHECK(web_network_url("https://github.com/qcom-toolbox/Banana-OS/releases"));
    CHECK(!web_network_url("file:///etc/hostname"));
    CHECK(!web_network_url("javascript:alert(1)"));
    CHECK(!web_network_url("https://user@github.com/"));
    CHECK(!web_network_url("https://github.com/\r\nHost: evil"));
    CHECK(!web_network_url("https://github.com:65536/"));
    CHECK(web_same_origin("https://github.com/a","https://GITHUB.com:443/b"));
    CHECK(!web_same_origin("https://github.com/a","http://github.com/a"));
    CHECK(!web_same_origin("https://github.com/a","https://github.com.evil/a"));
    CHECK(!web_same_origin("https://github.com/a","https://github.com:444/a"));
    CHECK(!web_same_origin("about:blank","https://github.com/"));
    CHECK(web_same_origin("http://[::1]:8080/a","http://[::1]:8080/b"));
    uint32_t public=IP4(142,250,1,1);
    CHECK(web_endpoint_allowed("https://www.google.com/","www.gstatic.com",443,4,&public));
    const uint32_t private[]={
        IP4(0,0,0,0),IP4(10,0,2,2),IP4(127,0,0,1),IP4(169,254,169,254),
        IP4(172,16,0,1),IP4(192,168,1,25),IP4(100,64,0,1),IP4(224,0,0,1)};
    for(unsigned i=0;i<sizeof(private)/sizeof(private[0]);i++){
        CHECK(!web_endpoint_allowed("https://evil.example/","evil.example",443,4,&private[i]));
        CHECK(!web_endpoint_allowed("https://www.google.com/","localhost",80,4,&private[i]));
    }
    uint32_t lan=IP4(192,168,1,25);
    CHECK(web_endpoint_allowed("http://192.168.1.25:8080/","192.168.1.25",8080,4,&lan));
    CHECK(!web_endpoint_allowed("http://192.168.1.25:8080/","192.168.1.25",80,4,&lan));
    CHECK(!web_endpoint_allowed("http://192.168.1.25/","192.168.1.26",80,4,&lan));
    ip6_t v6={{0}};
    v6.b[15]=1;
    CHECK(!web_endpoint_allowed("https://example.com/","example.com",443,6,&v6));
    CHECK(web_endpoint_allowed("http://[::1]/","::1",80,6,&v6));
    v6.b[0]=0xfc;
    CHECK(!web_endpoint_allowed("https://example.com/","example.com",443,6,&v6));
    v6.b[0]=0x26;v6.b[1]=0x07;
    CHECK(web_endpoint_allowed("https://example.com/","cdn.example.com",443,6,&v6));
    printf("PASS: %d browser origin and resolved-endpoint policy checks\n",checks);
    return 0;
}
