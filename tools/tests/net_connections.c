/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../net/tcp.c"
extern int printf(const char*,...);
extern void abort(void);
extern void free(void*);
void kfree(void* p) { free(p); }
int ip6_is_loopback(const ip6_t* ip) {
    for(int i=0;i<15;i++) if(ip->b[i]) return 0;
    return ip->b[15]==1;
}
#define CHECK(c) do { if(!(c)) { printf("FAIL %d: %s\n",__LINE__,#c); abort(); } } while(0)
int main(void) {
    for(int i=0;i<6;i++) {
        tcp_conn_t* c=&g_conns[i];
        c->used=1; c->state=TCP_ESTABLISHED; c->rip=IP4(10,0,0,1);
        c->timer_on=c->ack_pending=c->fin_pending=1;
    }
    g_conns[1].rip=IP4(127,0,0,1);
    g_conns[2].ipv6=1; g_conns[2].rip6.b[0]=0x20;
    g_conns[3].ipv6=1; g_conns[3].rip6.b[15]=1;
    g_conns[4].listener=&g_listeners[0];
    g_conns[5].user_closed=1;
    g_listeners[0].used=1;
    tcp_network_changed();
    for(int i=0;i<4;i++) {
        tcp_conn_t* c=&g_conns[i]; CHECK(c->used);
        if(i==1 || i==3) CHECK(c->state==TCP_ESTABLISHED && c->timer_on);
        else CHECK(c->state==TCP_CLOSED && c->err==NET_ERR_NOTREADY &&
                   !c->timer_on && !c->ack_pending && !c->fin_pending);
    }
    CHECK(!g_conns[4].used && !g_conns[5].used && g_listeners[0].used);
    tcp_network_changed(); CHECK(g_conns[0].used && g_conns[1].state==TCP_ESTABLISHED);
    printf("Interface switch: TCP owners notified, loopback/listeners retained\n");
}
