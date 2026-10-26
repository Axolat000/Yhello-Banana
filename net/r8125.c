/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Native PCI/DMA frontend for RTL8125/A/B/D. Hardware initialization and
 * Realtek firmware live separately under third_party/rge (ISC / firmware licence).
 * Never binds the existing RTL8111/8168 IDs or the USB RTL8152 path.
 */
#include "netdev.h"
#include "pci.h"
#include "kheap.h"
#include "timer.h"
#include "io.h"
#include "../third_party/rge/rge_native.h"
#undef printf
#define R8125_RX 256u
#define R8125_TX 64u
#define R8125_BUFFER 2048u
#define R8125_MAX 8u
_Static_assert(sizeof(struct rge_rx_desc)==32,"RTL8125 v1 RX descriptor");
_Static_assert(sizeof(struct rge_tx_desc)==32,"RTL8125 TX descriptor");
typedef struct {
    netdev_t nd;
    struct rge_softc hw;
    pci_dev_t pci;
    volatile struct rge_rx_desc rx[R8125_RX] __attribute__((aligned(256)));
    volatile struct rge_tx_desc tx[R8125_TX] __attribute__((aligned(256)));
    uint8_t rxbuf[R8125_RX][R8125_BUFFER] __attribute__((aligned(16)));
    uint8_t txbuf[R8125_TX][R8125_BUFFER] __attribute__((aligned(16)));
    unsigned rxcur,txcur,txclean,txused;
} r8125_t;
static r8125_t* r8125_cards[R8125_MAX];
static unsigned r8125_count;
#define DMA_BARRIER() __asm__ volatile("" ::: "memory")
#ifndef R8125_MODEL
__asm__(".pushsection .rodata\n"
        ".global r8125_notice_start\nr8125_notice_start:\n"
        ".incbin \"third_party/rge/NOTICES\"\n"
        ".global r8125_notice_end\nr8125_notice_end:\n.popsection\n");
uint32_t rge_native_read(struct rge_softc* sc,uint32_t reg,unsigned width) {
    if(sc->fault) return 0;
    if(reg>0x10000-width || (width!=1 && width!=2 && width!=4) || reg%width) { sc->fault=1;return 0; }
    uint32_t v=width==1?*(volatile uint8_t*)(sc->mmio+reg):
               width==2?*(volatile uint16_t*)(sc->mmio+reg):*(volatile uint32_t*)(sc->mmio+reg);
    if(reg==RGE_CMD && width==1 && v==255) { sc->fault=1;return 0; }
    return v;
}
void rge_native_write(struct rge_softc* sc,uint32_t reg,uint32_t v,unsigned width) {
    if(sc->fault) return;
    if(reg>0x10000-width || (width!=1 && width!=2 && width!=4) || reg%width) { sc->fault=1;return; }
    if(width==1) *(volatile uint8_t*)(sc->mmio+reg)=(uint8_t)v;
    else if(width==2) *(volatile uint16_t*)(sc->mmio+reg)=(uint16_t)v;
    else *(volatile uint32_t*)(sc->mmio+reg)=v;
}
void rge_delay_us(uint32_t us) {
    if(!timer_has_hardware_clock()) {
        while(us--) (void)inb(0x80);
        return;
    }
    uint64_t start=timer_counter();
    while(timer_counter()-start<(uint64_t)us*1000) __asm__ volatile("pause");
}
#endif
static int r8125_carrier(netdev_t* nd) {
    r8125_t* c=nd->priv;
    (void)RGE_READ_1(&c->hw,RGE_CMD);
    return !c->hw.fault && (RGE_READ_2(&c->hw,RGE_PHYSTAT)&RGE_PHYSTAT_LINK)!=0;
}
static void r8125_reap(r8125_t* c) {
    while(c->txused && !(c->tx[c->txclean].rge_cmdsts&RGE_TDCMDSTS_OWN)) {
        DMA_BARRIER();
        if(c->tx[c->txclean].rge_cmdsts&RGE_TDCMDSTS_TXERR) c->nd.tx_errors++;
        c->txclean=(c->txclean+1)%R8125_TX;c->txused--;
    }
}
static int r8125_send(netdev_t* nd,const void* frame,uint32_t len) {
    r8125_t* c=nd->priv;
    if(!frame || len<14 || len>ETH_FRAME_MAX || !r8125_carrier(nd)) return -1;
    r8125_reap(c);
    if(c->txused>=R8125_TX-1) { nd->tx_errors++;return -1; }
    unsigned i=c->txcur;
    memcpy(c->txbuf[i],frame,len);
    c->tx[i].rge_extsts=0;
    c->tx[i].rge_addr=(uint64_t)(uintptr_t)c->txbuf[i];
    DMA_BARRIER();
    c->tx[i].rge_cmdsts=len|RGE_TDCMDSTS_SOF|RGE_TDCMDSTS_EOF|RGE_TDCMDSTS_OWN|
                           (i==R8125_TX-1?RGE_TDCMDSTS_EOR:0);
    DMA_BARRIER();c->txcur=(i+1)%R8125_TX;c->txused++;
    RGE_WRITE_2(&c->hw,RGE_TXSTART,RGE_TXSTART_START);
    nd->tx_packets++;nd->tx_bytes+=len;
    return 0;
}
static int r8125_poll(netdev_t* nd) {
    r8125_t* c=nd->priv;
    if(c->hw.fault) return 0;
    r8125_reap(c);int count=0;
    for(unsigned budget=0;budget<R8125_RX;budget++) {
        unsigned i=c->rxcur;
        uint32_t s=c->rx[i].hi_qword1.rx_qword4.rge_cmdsts;
        if(s&RGE_RDCMDSTS_OWN) break;
        DMA_BARRIER();
        unsigned len=s&RGE_RDCMDSTS_FRAGLEN;
        if((s&(RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF))!=(RGE_RDCMDSTS_SOF|RGE_RDCMDSTS_EOF) ||
           (s&RGE_RDCMDSTS_RXERRSUM) || len<18 || len>ETH_FRAME_MAX+4) {
            nd->rx_errors++;nd->rx_dropped++;
        } else {
            len-=4; /* RTL8125 descriptor length includes FCS. */
            nd->rx_packets++;nd->rx_bytes+=len;
            net_rx(nd,c->rxbuf[i],len);count++;
        }
        /* Restore all DMA-facing fields overwritten by the NIC. */
        memset((void*)&c->rx[i],0,sizeof(c->rx[i]));
        c->rx[i].hi_qword0.rge_addr=(uint64_t)(uintptr_t)c->rxbuf[i];
        DMA_BARRIER();
        c->rx[i].hi_qword1.rx_qword4.rge_cmdsts=R8125_BUFFER|RGE_RDCMDSTS_OWN|
                                                 (i==R8125_RX-1?RGE_RDCMDSTS_EOR:0);
        c->rxcur=(i+1)%R8125_RX;
    }
    return count;
}
static void r8125_multicast(netdev_t* nd,int all) {
    r8125_t* c=nd->priv;uint32_t v=RGE_READ_4(&c->hw,RGE_RXCFG);
    if(all) v|=RGE_RXCFG_MULTI;else v&=~RGE_RXCFG_MULTI;
    RGE_WRITE_4(&c->hw,RGE_RXCFG,v);
}
static void r8125_pci_setup(const pci_dev_t* pd) {
    uint16_t before=pci_read16(pd->bus,pd->dev,pd->fn,4);
    pci_write16(pd->bus,pd->dev,pd->fn,4,(before&~4u)|(1u<<10));
    if(pci_read16(pd->bus,pd->dev,pd->fn,6)&16) {
        uint8_t p=(uint8_t)(pci_read16(pd->bus,pd->dev,pd->fn,0x34)&0xfc);
        for(unsigned n=0;p>=0x40 && n<48;n++) {
            uint16_t h=pci_read16(pd->bus,pd->dev,pd->fn,p);
            if((h&255)==1 && p<=0xf8) {
                uint8_t off=p+4;uint16_t pm=pci_read16(pd->bus,pd->dev,pd->fn,off);
                pci_write16(pd->bus,pd->dev,pd->fn,off,pm&~3u);
                rge_delay_us(10000);
            }
            if((h&255)==0x10 && p<=0xec) {
                uint8_t off=p+0x10;uint16_t link=pci_read16(pd->bus,pd->dev,pd->fn,off);
                pci_write16(pd->bus,pd->dev,pd->fn,off,link&~0x103u);
            }
            p=(uint8_t)((h>>8)&0xfc);
        }
    }
    uint16_t cmd=pci_read16(pd->bus,pd->dev,pd->fn,4);
    /* MMIO is enabled, but bus mastering waits until new rings are ready. */
    pci_write16(pd->bus,pd->dev,pd->fn,4,(cmd|2u|(1u<<10))&~4u);
}
static int r8125_valid_mac(const uint8_t* m) {
    unsigned any=0;for(unsigned i=0;i<6;i++) any|=m[i];
    return any && !(m[0]&1);
}
static int r8125_attach(const pci_dev_t* pd) {
    if(r8125_count>=R8125_MAX) return -1;
    for(unsigned i=0;i<r8125_count;i++) {
        const pci_dev_t* p=&r8125_cards[i]->pci;
        if(p->bus==pd->bus && p->dev==pd->dev && p->fn==pd->fn) return -1;
    }
    r8125_pci_setup(pd);
    int io=0;uintptr_t bar=pci_bar(pd,2,&io);
    if(!bar || io) return -1; /* memory BAR2 is required by this frontend */
    struct rge_softc probe;memset(&probe,0,sizeof(probe));probe.mmio=(void*)bar;
    uint32_t revision=RGE_READ_4(&probe,RGE_TXCFG)&RGE_TXCFG_HWREV;
    enum rge_mac_type type=revision==0x60900000?MAC_R25:revision==0x64100000?MAC_R25B:
                           revision==0x68800000?MAC_R25D_1:revision==0x68900000?MAC_R25D_2:MAC_UNKNOWN;
    if(type==MAC_UNKNOWN) return -1;
    r8125_t* c=kmalloc_aligned(sizeof(*c),256);if(!c) return -1;
    memset(c,0,sizeof(*c));c->pci=*pd;c->hw=probe;c->hw.rge_type=type;c->hw.sc_dev.dv_xname="r8125";
    c->hw.sc_nqueues=c->hw.sc_ntxqs=1;
    /* Persistent contexts: an unresponsive device must never DMA into freed RAM. */
    r8125_cards[r8125_count++]=c;
    struct rge_softc* sc=&c->hw;
    if(rge_chipinit(sc) || sc->fault) goto fail;
    rge_get_macaddr(sc,c->nd.mac);
    if(!r8125_valid_mac(c->nd.mac) || rge_phy_config(sc) || sc->fault) goto fail;
    uint16_t version=type==MAC_R25?RGE_MAC_R25_RCODE_VER:type==MAC_R25B?RGE_MAC_R25B_RCODE_VER:
                     type==MAC_R25D_1?RGE_MAC_R25D_1_RCODE_VER:RGE_MAC_R25D_2_RCODE_VER;
    rge_write_phy_ocp(sc,0xa436,0x801e);
    if(rge_read_phy_ocp(sc,0xa438)!=version || sc->fault) goto fail;
    for(unsigned i=0;i<R8125_RX;i++) {
        c->rx[i].hi_qword0.rge_addr=(uint64_t)(uintptr_t)c->rxbuf[i];
        c->rx[i].hi_qword1.rx_qword4.rge_cmdsts=R8125_BUFFER|RGE_RDCMDSTS_OWN|
                                              (i==R8125_RX-1?RGE_RDCMDSTS_EOR:0);
    }
    sc->rx_addr=(uintptr_t)c->rx;sc->tx_addr=(uintptr_t)c->tx;
    if(rge_native_start(sc) || sc->fault) goto fail;
    c->nd.name="r8125";
    c->nd.model=type==MAC_R25?"Realtek RTL8125 (experimental)":type==MAC_R25B?"Realtek RTL8125B (experimental)":"Realtek RTL8125D (experimental)";
    c->nd.irq=0xff;c->nd.priv=c;c->nd.send=r8125_send;c->nd.poll=r8125_poll;
    c->nd.link_up=r8125_carrier;c->nd.set_multicast=r8125_multicast;
    pci_enable(pd); /* start DMA only after initialization and ring setup */
    net_register_device(&c->nd);klog("r8125: experimental RTL8125 interface registered\n");return 0;
fail:
    /* MMIO may be unresponsive: PCI config still stops future bus mastering. */
    uint16_t command=pci_read16(pd->bus,pd->dev,pd->fn,4);
    pci_write16(pd->bus,pd->dev,pd->fn,4,command&~4u);
    RGE_WRITE_4(sc,RGE_IMR,0);RGE_WRITE_1(sc,RGE_CMD,0);
    klog("r8125: hardware initialization failed; interface not registered\n");return -1;
}
static int r8125_scan(const pci_dev_t* pd,void* arg) {
    (void)arg;
    if(pd->vendor==0x10ec && pd->device==0x8125 && pd->class_code==2 && pd->subclass==0)
        r8125_attach(pd);
    return 0;
}
void r8125_probe_all(void) { pci_scan(r8125_scan,NULL); }
