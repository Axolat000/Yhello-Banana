/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual RTL8125 native frontend + OpenBSD/Realtek HAL, register/DMA model.
 * Firmware bytes are transmitted to modeled MAC/PHY RAM, not executed. */
#include "../../kernel/types.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern int posix_memalign(void**,size_t,size_t);
extern void free(void*);
#define R8125_MODEL
#include "../../net/r8125.c"
static unsigned checks,registered,received,nalloc,enabled,mac_writes,phy_writes,phy_mcu_writes;
#define CHECK(c) do { checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();} }while(0)
typedef struct {
    uint8_t regs[0x10000];
    uint16_t mac[32768],phy[32768],ephy[128],ram[65536],index;
    uint32_t csi[1024],revision,command;
    unsigned program_words;
    int drop_version,stuck_phy,stuck_ephy,stuck_csi,stuck_reset,stuck_patch,stuck_power;
} model_t;
static model_t models[8];
static netdev_t* devs[8];
static void* allocations[8];
static uint32_t elapsed;
static void put(uint8_t* p,uint32_t n,unsigned width) { for(unsigned i=0;i<width;i++) p[i]=(uint8_t)(n>>(8*i)); }
static uint32_t get(const uint8_t* p,unsigned width) { uint32_t n=0;for(unsigned i=0;i<width;i++) n|=(uint32_t)p[i]<<(8*i);return n; }
void* kmalloc_aligned(size_t n,size_t a) {
    void* p=NULL;if(posix_memalign(&p,a,n)) return NULL;
    CHECK(nalloc<8);allocations[nalloc++]=p;return p;
}
void klog(const char* f,...) { (void)f; }
void net_register_device(netdev_t* nd) { CHECK(registered<8);devs[registered++]=nd;nd->present=1; }
void net_rx(netdev_t* nd,const uint8_t* b,uint32_t n) { CHECK(nd && n>=14 && n<=1514 && b[0]==0xa5);received++; }
void rge_delay_us(uint32_t n) { elapsed+=n;CHECK(elapsed<300000000); }
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t o) { (void)b;(void)f;return o==4?(uint16_t)models[d].command:0; }
void pci_write16(uint8_t b,uint8_t d,uint8_t f,uint8_t o,uint16_t v) { (void)b;(void)f;CHECK(o==4);models[d].command=v; }
void pci_enable_from(const pci_dev_t* pd,const char* file) { CHECK(pd && file);enabled++;models[pd->dev].command|=7; }
uintptr_t pci_bar(const pci_dev_t* pd,int bar,int* io) { CHECK(bar==2);*io=0;return (uintptr_t)&models[pd->dev]; }
int pci_scan(int(*cb)(const pci_dev_t*,void*),void* arg) {
    for(unsigned i=0;i<6;i++) {
        pci_dev_t pd={.bus=1,.dev=(uint8_t)i,.vendor=0x10ec,.device=i<4?0x8125:i==4?0x8168:0x8152,.class_code=2};
        cb(&pd,arg);
    }
    return 0;
}
uint32_t rge_native_read(struct rge_softc* sc,uint32_t reg,unsigned width) {
    if(reg>0x10000-width || reg%width) printf("bad register %x width %u\n",reg,width);
    CHECK(reg<=0x10000-width && (width==1 || width==2 || width==4) && !(reg%width));
    if(sc->fault) return 0;
    model_t* m=(void*)sc->mmio;
    if(reg==RGE_TXCFG && width==4) return get(m->regs+reg,4)|m->revision;
    return get(m->regs+reg,width);
}
static uint16_t phy_read(model_t* m,uint16_t reg) {
    if(reg==0xa438) return m->ram[m->index];
    if(reg==0xa420) return m->stuck_power?0:3;
    if(reg==0xb800) return (m->phy[reg/2]&~0x40)|(m->stuck_patch?0:(m->phy[0xb820/2]&0x10?0x40:0));
    return m->phy[reg/2];
}
static void phy_write(model_t* m,uint16_t reg,uint16_t val) {
    phy_writes++;
    if(m->phy[0xb820/2]&0x80) m->program_words++;
    if(reg==0xa436) m->index=val;
    else if(reg==0xa438) { if(!m->drop_version || m->index!=0x801e) m->ram[m->index]=val;phy_mcu_writes++; }
    if(reg==0xa400) val&=~BMCR_RESET;
    m->phy[reg/2]=val;
}
void rge_native_write(struct rge_softc* sc,uint32_t reg,uint32_t value,unsigned width) {
    if(reg>0x10000-width || reg%width) printf("bad register %x width %u\n",reg,width);
    CHECK(reg<=0x10000-width && (width==1 || width==2 || width==4) && !(reg%width));
    if(sc->fault) return;
    model_t* m=(void*)sc->mmio;
    if(reg==RGE_CMD && width==1) CHECK(!(m->command&4)); /* no old DMA while initializing */
    if(reg==RGE_CMD && width==1 && (value&RGE_CMD_RESET) && !m->stuck_reset) value=0;
    if(reg==RGE_MACOCP) {
        unsigned addr=((value>>16)&0x7fff)*2;
        if(value&RGE_MACOCP_BUSY) { m->mac[addr/2]=(uint16_t)value;mac_writes++; }
        else value|=m->mac[addr/2];
    }
    if(reg==RGE_PHYOCP && !m->stuck_phy) {
        uint16_t addr=(uint16_t)(((value>>16)&0x7fff)*2);
        if(value&RGE_PHYOCP_BUSY) { phy_write(m,addr,(uint16_t)value);value&=~RGE_PHYOCP_BUSY; }
        else value|=RGE_PHYOCP_BUSY|phy_read(m,addr);
    }
    if(reg==RGE_EPHYAR && !m->stuck_ephy) {
        unsigned addr=(value>>RGE_EPHYAR_ADDR_SHIFT)&RGE_EPHYAR_ADDR_MASK;CHECK(addr<128);
        if(value&RGE_EPHYAR_BUSY) { m->ephy[addr]=(uint16_t)value;value&=~RGE_EPHYAR_BUSY; }
        else value|=RGE_EPHYAR_BUSY|m->ephy[addr];
    }
    if(reg==RGE_CSIAR && !m->stuck_csi) {
        unsigned addr=(value&RGE_CSIAR_ADDR_MASK)/4;CHECK(addr<1024);
        if(value&RGE_CSIAR_BUSY) { m->csi[addr]=get(m->regs+RGE_CSIDR,4);value&=~RGE_CSIAR_BUSY; }
        else { put(m->regs+RGE_CSIDR,m->csi[addr],4);value|=RGE_CSIAR_BUSY; }
    }
    put(m->regs+reg,value,width);
}
static void init(void) {
    for(unsigned i=0;i<nalloc;i++) free(allocations[i]);
    nalloc=registered=received=enabled=mac_writes=phy_writes=phy_mcu_writes=r8125_count=elapsed=0;
    memset(r8125_cards,0,sizeof(r8125_cards));memset(models,0,sizeof(models));
    uint32_t revisions[]={0x60900000,0x64100000,0x68800000,0x68900000};
    for(unsigned i=0;i<8;i++) {
        model_t* m=&models[i];m->revision=revisions[i%4];
        put(m->regs+RGE_ADDR0,0xaa221102,4);put(m->regs+RGE_ADDR1,0x5544,2);
        put(m->regs+RGE_PHYSTAT,RGE_PHYSTAT_LINK|RGE_PHYSTAT_2500MBPS|RGE_PHYSTAT_FDX,2);
        m->regs[RGE_MCUCMD]=RGE_MCUCMD_RXFIFO_EMPTY|RGE_MCUCMD_TXFIFO_EMPTY;
        put(m->regs+RGE_IM,0x0103,2);
        m->mac[0xe00e/2]=0x2000;put(m->regs+RGE_TWICMD,0x0200,2);
    }
}
static void inject(r8125_t* c,uint32_t status,unsigned length) {
    unsigned i=c->rxcur;memset(c->rxbuf[i],0xa5,R8125_BUFFER);
    c->rx[i].hi_qword1.rx_qword4.rge_cmdsts=status|length;
    r8125_poll(&c->nd);
    CHECK(c->rx[i].hi_qword1.rx_qword4.rge_cmdsts&RGE_RDCMDSTS_OWN);
    CHECK(c->rx[i].hi_qword0.rge_addr==(uint64_t)(uintptr_t)c->rxbuf[i]);
}
int main(void) {
    init();r8125_probe_all();
    CHECK(registered==4 && enabled==4 && mac_writes>1000 && phy_writes>5000 && phy_mcu_writes>1000);
    CHECK(sizeof(struct rge_rx_desc)==32 && sizeof(struct rge_tx_desc)==32);
    CHECK(__builtin_offsetof(struct rge_rx_desc,hi_qword1.rx_qword4.rge_cmdsts)==28);
    r8125_probe_all();CHECK(registered==4);
    for(unsigned n=0;n<4;n++) {
        r8125_t* c=devs[n]->priv;model_t* m=&models[n];
        const uint16_t expected_versions[]={0x0b33,0x0b99,0x0027,0x0031};
        CHECK(m->ram[0x801e]==expected_versions[n]);
        CHECK(r8125_carrier(&c->nd) && !memcmp(c->nd.mac,"\x02\x11\x22\xaa\x44\x55",6));
        CHECK(m->command&(1u<<10));CHECK(get(m->regs+RGE_IMR,4)==0);
        CHECK(get(m->regs+RGE_RXDESC_ADDR_LO,4)==(uint32_t)(uintptr_t)c->rx);
        CHECK(get(m->regs+RGE_RXDESC_ADDR_HI,4)==(uint32_t)((uint64_t)(uintptr_t)c->rx>>32));
        CHECK(!(get(m->regs+RGE_CPLUSCMD,2)&RGE_CPLUSCMD_RXCSUM));
        CHECK((get(m->regs+RGE_RXCFG,4)&RGE_RXCFG_ENBDESCV1)!=0 && (m->mac[0xeb58/2]&1));
        uint8_t frame[1515];memset(frame,0xa5,sizeof(frame));
        CHECK(r8125_send(&c->nd,frame,13)<0 && r8125_send(&c->nd,frame,1515)<0);
        for(unsigned i=0;i<R8125_TX-1;i++) CHECK(!r8125_send(&c->nd,frame,1514));
        CHECK(r8125_send(&c->nd,frame,1514)<0 && c->txused==63);
        CHECK(!memcmp(c->txbuf[0],frame,1514));
        for(unsigned i=0;i<R8125_TX-1;i++) c->tx[i].rge_cmdsts&=~RGE_TDCMDSTS_OWN;
        r8125_poll(&c->nd);CHECK(!c->txused);
        for(unsigned i=0;i<300;i++) {
            unsigned index=c->txcur;CHECK(!r8125_send(&c->nd,frame,60));
            c->tx[index].rge_cmdsts&=~RGE_TDCMDSTS_OWN;r8125_poll(&c->nd);
            inject(c,RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF,1518);
        }
        unsigned before=received;
        inject(c,RGE_RDCMDSTS_SOF,100);
        inject(c,RGE_RDCMDSTS_EOF,100);
        inject(c,RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF,16383);
        inject(c,RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF|RGE_RDCMDSTS_RXERRSUM,64);
        CHECK(received==before);
        inject(c,RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF,64);CHECK(received==before+1);
        put(m->regs+RGE_PHYSTAT,0,2);CHECK(!r8125_carrier(&c->nd) && r8125_send(&c->nd,frame,60)<0);
        put(m->regs+RGE_PHYSTAT,RGE_PHYSTAT_LINK,2);CHECK(r8125_carrier(&c->nd));
    }
    pci_dev_t pd={.bus=1,.vendor=0x10ec,.device=0x8125,.class_code=2};
    init();models[0].stuck_phy=1;CHECK(r8125_attach(&pd)<0 && !registered && !(models[0].command&4));
    init();models[0].stuck_ephy=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].stuck_csi=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].stuck_reset=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].stuck_patch=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].stuck_power=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].drop_version=1;CHECK(r8125_attach(&pd)<0 && !registered);
    init();models[0].ram[0x801e]=0x0b33;
    CHECK(!r8125_attach(&pd) && registered==1 && models[0].program_words==0);
    init();models[0].revision=0x60800000;CHECK(r8125_attach(&pd)<0 && !registered && !enabled && !(models[0].command&4));
    init();put(models[0].regs+RGE_ADDR0,0,4);put(models[0].regs+RGE_ADDR1,0,2);
    CHECK(r8125_attach(&pd)<0 && !registered);
    init();printf("RTL8125: %u checks passed (native HAL/firmware/register/DMA model)\n",checks);
    return 0;
}
