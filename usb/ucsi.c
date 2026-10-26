/* SPDX-License-Identifier: GPL-3.0-or-later */
/* USB Type-C Connector System Software Interface command engine.
 * Firmware/PPM performs CC sensing and USB-PD negotiation. This engine never
 * sends raw voltage/current requests or a connector hard reset at boot.
 * Explicit role changes use SET_PDR; firmware may reject them.
 * Wire layout follows USB-IF UCSI 1.1/1.2 and Linux's published definitions. */
#include "ucsi.h"
#include "kstring.h"
#define CCI_UNSUPPORTED (1u<<25)
#define CCI_CANCEL (1u<<26)
#define CCI_RESET (1u<<27)
#define CCI_BUSY (1u<<28)
#define CCI_ACK (1u<<29)
#define CCI_ERROR (1u<<30)
#define CCI_COMPLETE (1u<<31)
#define CMD_RESET 1u
#define CMD_CANCEL 2u
#define CMD_ACK 4u
#define CMD_NOTIFICATIONS 5u
#define CMD_CAPABILITY 6u
#define CMD_PORT_CAPABILITY 7u
#define CMD_POWER_ROLE 11u
#define CMD_PDOS 16u
#define CMD_STATUS 18u
static uint16_t le16(const uint8_t* b) { return b[0]|(uint16_t)b[1]<<8; }
static uint32_t le32(const uint8_t* b) { return le16(b)|(uint32_t)le16(b+2)<<16; }
static int cci_read(ucsi_t* u,uint32_t* cci) {
    uint8_t b[4];
    if(u->ops->read(u->ctx,4,b,4)<0) return -1;
    *cci=le32(b);u->last_cci=*cci;return 0;
}
static int await(ucsi_t* u,uint32_t wanted,uint32_t timeout,uint32_t* value) {
    uint32_t start=u->ops->now(u->ctx),cci=0;
    /* Both time and iteration bounds: broken clocks cannot hang the OS. */
    for(unsigned n=0;n<timeout/10+2;n++) {
        if(cci_read(u,&cci)<0) return -1;
        if(!(cci&CCI_BUSY) && (cci&wanted)) { *value=cci;return 0; }
        if((uint32_t)(u->ops->now(u->ctx)-start)>=timeout) break;
        u->ops->wait(u->ctx,10);
    }
    return -1;
}
static int acknowledge(ucsi_t* u,int connector) {
    uint64_t cmd=CMD_ACK|(1ull<<17)|(connector?1ull<<16:0);
    uint32_t cci;
    if(u->ops->write(u->ctx,cmd)<0 || await(u,CCI_ACK,1000,&cci)<0) return -1;
    return (cci&(CCI_ERROR|CCI_UNSUPPORTED))?-1:0;
}
static int command(ucsi_t* u,uint64_t cmd,void* out,unsigned capacity,unsigned minimum,int connector_ack) {
    uint32_t cci;
    if(u->ops->write(u->ctx,cmd)<0) { u->failed=1;return -1; }
    if(await(u,CCI_COMPLETE|CCI_ERROR|CCI_UNSUPPORTED,2000,&cci)<0) {
        /* Cancel only the UCSI mailbox command, never the PD contract. */
        if(u->ops->write(u->ctx,CMD_CANCEL)<0 || await(u,CCI_CANCEL,1000,&cci)<0)
            u->failed=1;
        else if(acknowledge(u,0)<0) u->failed=1;
        return -1;
    }
    unsigned n=(cci>>8)&255;
    int result=0;
    if(cci&(CCI_ERROR|CCI_UNSUPPORTED)) result=-1;
    else if(n<minimum || n>capacity) result=-1;
    else if(n && (!out || u->ops->read(u->ctx,16,out,n)<0)) result=-1;
    if(acknowledge(u,connector_ack)<0) { u->failed=1;return -1; }
    return result<0?-1:(int)n;
}
void ucsi_decode_contract(ucsi_port_t* p) {
    p->millivolts=p->milliamps=p->milliwatts=0;p->contract_valid=0;
    if(!p->connected || p->mode!=3 || (p->rdo&0x80000000u)) return;
    /* This decoder handles 3-bit SPR object positions, not EPR requests. */
    unsigned position=(p->rdo>>28)&7;
    if(!position || position>p->nr_pdos) return;
    uint32_t pdo=p->source_pdos[position-1],type=pdo>>30;
    if(type==0) {
        uint32_t mv=((pdo>>10)&1023)*50,ma=((p->rdo>>10)&1023)*10;
        uint32_t max_ma=(pdo&1023)*10;
        if(mv<5000 || mv>20000 || !ma || max_ma>5000 || ma>max_ma) return;
        p->millivolts=mv;p->milliamps=ma;p->milliwatts=mv*ma/1000;
        p->contract_valid=1;
    } else if(type==1) {
        uint32_t mw=((p->rdo>>10)&1023)*250;
        if(mw && mw<=100000 && ((pdo>>10)&1023)>=100 && ((pdo>>20)&1023)<=400 && ((pdo>>10)&1023)<=((pdo>>20)&1023) && mw<=(pdo&1023)*250) { p->milliwatts=mw;p->contract_valid=1; }
    } else if(type==3 && ((pdo>>28)&3)==0) {
        /* PPS reserves bits 7..8 and 20..22; do not mask invalid input
         * into an apparently valid voltage/current request. */
        if(p->rdo&((3u<<7)|(7u<<20))) return;
        uint32_t mv=((p->rdo>>9)&2047)*20,ma=(p->rdo&127)*50;
        uint32_t min=((pdo>>8)&255)*100,max=((pdo>>17)&255)*100;
        if(mv<min || mv>max || min<3300 || max>21000 || !ma || (pdo&127)*50>5000 || ma>(pdo&127)*50) return;
        p->millivolts=mv;p->milliamps=ma;p->milliwatts=mv*ma/1000;p->contract_valid=1;
    }
    /* Variable PDO and EPR/AVS: do not invent a negotiated voltage/power. */
}
static void pdos(ucsi_t* u,unsigned port,ucsi_port_t* p) {
    p->nr_pdos=0;
    if(!(u->features&(1u<<4)) || p->mode!=3) return;
    for(unsigned offset=0;offset<7;offset+=4) {
        uint8_t data[16]={0};
        unsigned count=offset?3:4;
        uint64_t cmd=CMD_PDOS|((uint64_t)port<<16)|((uint64_t)offset<<24)|
                     ((uint64_t)(count-1)<<32)|(1ull<<34);
        /* As sink read partner Source PDOs; as source read our Source PDOs. */
        if(!p->source) cmd|=1ull<<23;
        int n=command(u,cmd,data,sizeof(data),0,0);
        if(n<=0 || n%4 || (unsigned)n>count*4) break;
        for(unsigned i=0;i<(unsigned)n/4;i++) p->source_pdos[p->nr_pdos++]=le32(data+i*4);
        if((unsigned)n<count*4) break;
    }
}
static int refresh_locked(ucsi_t* u) {
    unsigned success=0;
    for(unsigned i=0;i<u->num_ports && !u->failed;i++) {
        uint8_t data[16]={0};
        ucsi_port_t p;memset(&p,0,sizeof(p));
        int n=command(u,CMD_STATUS|((uint64_t)(i+1)<<16),data,sizeof(data),9,0);
        if(n<0) { u->ports[i].valid=0;u->errors++;continue; }
        p.change=le16(data);
        uint16_t flags=le16(data+2);
        p.connected=(flags&8)!=0;p.source=(flags&16)!=0;p.mode=flags&7;
        p.partner_type=(flags>>13)&7;p.rdo=le32(data+4);p.charging=data[8]&3;p.valid=1;
        if(p.connected) pdos(u,i+1,&p);
        ucsi_decode_contract(&p);
        u->ports[i]=p;success++;
        /* Clear the connector indication after the snapshot has been read. */
        if(p.change) {
            uint32_t cci;
            if(u->ops->write(u->ctx,CMD_ACK|(1ull<<16))<0 || await(u,CCI_ACK,1000,&cci)<0)
                u->failed=1;
        }
    }
    if(u->failed) for(unsigned i=0;i<u->num_ports;i++) u->ports[i].valid=0;
    u->last_ms=u->ops->now(u->ctx);
    return success==(unsigned)u->num_ports?0:-1;
}
int ucsi_init(ucsi_t* u,const ucsi_transport_t* ops,void* ctx) {
    if(!u || !ops || !ops->read || !ops->write || !ops->now || !ops->wait) return -1;
    memset(u,0,sizeof(*u));u->ops=ops;u->ctx=ctx;u->busy=1;
    uint8_t data[16]={0};uint32_t cci;
    if(ops->read(ctx,0,data,2)<0) goto fail;
    u->version=le16(data);
    /* Extended UCSI 2/3.x message/status layouts need separate decoders. */
    if(u->version!=0x0110 && u->version!=0x0120) goto fail;
    if(ops->write(ctx,CMD_RESET)<0 || await(u,CCI_RESET,3000,&cci)<0) goto fail;
    if(command(u,CMD_NOTIFICATIONS|(1ull<<16),NULL,0,0,0)<0) goto fail;
    if(command(u,CMD_CAPABILITY,data,sizeof(data),16,0)<0) goto fail;
    u->attributes=le32(data);u->num_ports=data[4]&127;u->features=le16(data+5);
    u->pd_version=le16(data+12);u->typec_version=le16(data+14);
    if(!u->num_ports || u->num_ports>UCSI_PORT_MAX) goto fail;
    for(unsigned i=0;i<u->num_ports;i++) {
        uint8_t capability[16]={0};
        if(command(u,CMD_PORT_CAPABILITY|((uint64_t)(i+1)<<16),capability,sizeof(capability),2,0)<0) goto fail;
        u->role_capabilities[i]=capability[1];
    }
    u->ready=1;u->busy=0;return ucsi_refresh(u);
fail:
    u->failed=1;u->busy=0;return -1;
}
int ucsi_refresh(ucsi_t* u) {
    if(!u || !u->ready || u->failed || u->busy) return -1;
    u->busy=1;int ret=refresh_locked(u);u->busy=0;return ret;
}
int ucsi_power_role(ucsi_t* u,unsigned port,int source) {
    if(!u || !u->ready || u->failed || u->busy || !port || port>u->num_ports) return -1;
    if(!u->ports[port-1].valid || !u->ports[port-1].connected ||
       !(u->role_capabilities[port-1]&(source?(1u<<4):(1u<<5)))) return -1;
    u->busy=1;
    uint64_t cmd=CMD_POWER_ROLE|((uint64_t)port<<16)|((uint64_t)(source?1:2)<<23)|(1ull<<25);
    int ret=command(u,cmd,NULL,0,0,0);
    if(ret>=0) ret=refresh_locked(u);
    u->busy=0;return ret<0?-1:0;
}
