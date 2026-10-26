/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../usb/ucsi.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern void abort(void);
static unsigned checks;
#define CHECK(c) do { checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);abort();} }while(0)
typedef struct {
    uint32_t cci,now,commands,acks,cancels,resets;
    uint8_t data[16];
    unsigned delay,remaining;uint32_t pending_cci;
    int freeze_clock,source,connected,fail_read,fail_write,stuck,bad_length,unsupported,ack_fail,cancel_fail;
} model_t;
static void put32(uint8_t* b,uint32_t n) { for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(n>>(i*8)); }
static int read(void* ctx,unsigned off,void* data,unsigned n) {
    model_t* m=ctx;
    if(m->fail_read) return -1;
    if(off==0) { CHECK(n==2);((uint8_t*)data)[0]=0x20;((uint8_t*)data)[1]=1; }
    else if(off==4) {
        CHECK(n==4);
        if(m->remaining && !--m->remaining) m->cci=m->pending_cci;
        put32(data,m->cci);
    }
    else if(off==16) { CHECK(n<=16);memcpy(data,m->data,n); }
    else CHECK(0);
    return 0;
}
static int write(void* ctx,uint64_t cmd) {
    model_t* m=ctx;unsigned op=(unsigned)cmd&255;m->commands++;
    if(m->fail_write) return -1;
    CHECK(op==1 || op==2 || op==4 || op==5 || op==6 || op==7 || op==11 || op==16 || op==18);
    /* No connector reset, raw voltage request or alt-mode command. */
    memset(m->data,0,sizeof(m->data));
    if(op==1) { m->resets++;m->cci=1u<<27;return 0; }
    if(op==2) { m->cancels++;m->cci=m->cancel_fail?0:1u<<26;return 0; }
    if(op==4) { m->acks++;m->cci=m->ack_fail?0:1u<<29;return 0; }
    if(m->stuck) { m->cci=1u<<28;return 0; }
    if(m->unsupported) { m->cci=(1u<<31)|(1u<<25);return 0; }
    m->cci=1u<<31;
    if(op==6) {
        put32(m->data,6);m->data[4]=1;m->data[5]=16;m->data[13]=3;m->data[14]=0x20;m->data[15]=1;
        m->cci|=16<<8;
    } else if(op==7) { m->data[0]=0x64;m->data[1]=0x33;m->cci|=2<<8;
    } else if(op==18) {
        unsigned flags=m->connected?(3|8|(m->source?16:0)|(1u<<13)):0;
        m->data[1]=0x40;m->data[2]=(uint8_t)flags;m->data[3]=(uint8_t)(flags>>8);
        put32(m->data+4,(2u<<28)|(300u<<10)|300);m->data[8]=1;m->cci|=9<<8;
        if(m->bad_length) m->cci=(1u<<31)|(255<<8);
    } else if(op==16) {
        CHECK(((cmd>>16)&127)==1);
        CHECK(((cmd>>23)&1)==(unsigned)!m->source);
        if(!((cmd>>24)&255)) {
            put32(m->data,(100u<<10)|300);
            put32(m->data+4,(400u<<10)|300);
            m->cci|=8<<8;
        }
    } else if(op==11) m->source=((cmd>>23)&3)==1;
    if(m->delay) { m->pending_cci=m->cci;m->cci=1u<<28;m->remaining=m->delay; }
    return 0;
}
static uint32_t now(void* ctx) { return ((model_t*)ctx)->now; }
static void wait(void* ctx,unsigned ms) { if(!((model_t*)ctx)->freeze_clock) ((model_t*)ctx)->now+=ms; }
static const ucsi_transport_t ops={read,write,now,wait};
int main(void) {
    model_t m={.connected=1};ucsi_t u;
    CHECK(!ucsi_init(&u,&ops,&m));
    CHECK(u.ready && u.num_ports==1 && u.version==0x120);
    CHECK(u.ports[0].valid && u.ports[0].connected && !u.ports[0].source);
    CHECK(u.ports[0].contract_valid && u.ports[0].millivolts==20000 &&
          u.ports[0].milliamps==3000 && u.ports[0].milliwatts==60000);
    CHECK(!ucsi_power_role(&u,1,1) && u.ports[0].source && m.source);
    CHECK(ucsi_power_role(&u,0,1)<0 && ucsi_power_role(&u,2,1)<0);
    CHECK(m.resets==1 && !m.cancels);
    m.connected=0;CHECK(!ucsi_refresh(&u));
    CHECK(!u.ports[0].connected && !u.ports[0].contract_valid && !u.ports[0].milliwatts);
    m.connected=1;m.bad_length=1;
    CHECK(ucsi_refresh(&u)<0 && !u.ports[0].valid);
    m.bad_length=0;CHECK(!ucsi_refresh(&u));
    m.stuck=1;unsigned before=m.now;
    CHECK(ucsi_refresh(&u)<0 && m.cancels==1 && m.now-before<=4000);
    m.stuck=0;CHECK(!ucsi_refresh(&u));
    m.unsupported=1;CHECK(ucsi_refresh(&u)<0 && !u.ports[0].valid);
    m.unsupported=0;CHECK(!ucsi_refresh(&u));
    m.ack_fail=1;CHECK(ucsi_refresh(&u)<0 && u.failed && !u.ports[0].valid);
    memset(&m,0,sizeof(m));m.fail_read=1;CHECK(ucsi_init(&u,&ops,&m)<0 && !u.ready);
    memset(&m,0,sizeof(m));m.fail_write=1;CHECK(ucsi_init(&u,&ops,&m)<0 && !u.ready);
    memset(&m,0,sizeof(m));m.connected=1;CHECK(!ucsi_init(&u,&ops,&m));
    m.stuck=m.cancel_fail=1;CHECK(ucsi_refresh(&u)<0 && u.failed);
    memset(&m,0,sizeof(m));m.connected=1;m.delay=3;
    CHECK(!ucsi_init(&u,&ops,&m) && m.now>0 && u.ports[0].contract_valid);
    memset(&m,0,sizeof(m));m.connected=1;m.freeze_clock=1;
    CHECK(!ucsi_init(&u,&ops,&m));m.stuck=1;
    CHECK(ucsi_refresh(&u)<0 && m.cancels==1 && m.now==0);
    ucsi_port_t p={.connected=1,.mode=3,.nr_pdos=1};
    p.source_pdos[0]=(3u<<30)|(210u<<17)|(33u<<8)|60; /* PPS 3.3–21V, 3A */
    p.rdo=(1u<<28)|(450u<<9)|40;ucsi_decode_contract(&p);
    CHECK(p.contract_valid && p.millivolts==9000 && p.milliamps==2000 && p.milliwatts==18000);
    p.rdo|=1u<<20;ucsi_decode_contract(&p);CHECK(!p.contract_valid);
    p.rdo=(1u<<28)|(450u<<9)|40|(1u<<7);ucsi_decode_contract(&p);CHECK(!p.contract_valid);
    p.rdo=(1u<<28)|(450u<<9)|40|(1u<<31);ucsi_decode_contract(&p);CHECK(!p.contract_valid);
    p.rdo=(1u<<28)|(1100u<<9)|40;ucsi_decode_contract(&p);CHECK(!p.contract_valid);
    p.source_pdos[0]=(1u<<30)|(400u<<20)|(100u<<10)|240; /* battery PDO max60W */
    p.rdo=(1u<<28)|(180u<<10);ucsi_decode_contract(&p);CHECK(p.contract_valid && p.milliwatts==45000 && !p.millivolts);
    uint32_t random=0xbadc0de;
    for(unsigned i=0;i<50000;i++) {
        random^=random<<13;random^=random>>17;random^=random<<5;p.rdo=random;
        for(unsigned j=0;j<7;j++) p.source_pdos[j]=random^((j+1)*0x12345678u);
        p.nr_pdos=i%8;ucsi_decode_contract(&p);
        CHECK(!p.contract_valid || (p.milliwatts && p.milliwatts<=255750));
        if(!p.contract_valid) CHECK(!p.millivolts && !p.milliamps && !p.milliwatts);
    }
    printf("UCSI: %u checks passed (PPM command/PD contract model)\n",checks);
    return 0;
}
