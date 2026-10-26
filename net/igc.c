/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Native polling/DMA frontend for Intel I225-V / I226-V.
 * The separate Intel/OpenBSD shared HAL supplies PHY, NVM, reset and link
 * arbitration; this frontend does not reuse/change the e1000 driver.
 * One queue, MTU 1500, software checksums, no MSI or hardware offloads. */
#include "netdev.h"
#include "pci.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"
#include "../third_party/igc/igc_api.h"

#define IGC_RX_COUNT 256u
#define IGC_TX_COUNT 64u
#define IGC_BUFFER 2048u
#define IGC_CARDS 8u
typedef struct {
    netdev_t nd;
    struct igc_hw hw;
    pci_dev_t pci;
    volatile union igc_adv_rx_desc rx[IGC_RX_COUNT] __attribute__((aligned(128)));
    volatile union igc_adv_tx_desc tx[IGC_TX_COUNT] __attribute__((aligned(128)));
    uint8_t rxbuf[IGC_RX_COUNT][IGC_BUFFER] __attribute__((aligned(16)));
    uint8_t txbuf[IGC_TX_COUNT][IGC_BUFFER] __attribute__((aligned(16)));
    uint32_t rxcur, txcur, txclean, txused, link_ms;
    int link, drop_chain;
} igc_card_t;
static igc_card_t* igc_cards[IGC_CARDS];
static unsigned igc_ncards;
#define IGC_BARRIER() __asm__ volatile("" ::: "memory")

#ifndef IGC_MODEL
__asm__(".pushsection .rodata\n"
        ".global igc_notices_start\nigc_notices_start:\n"
        ".incbin \"third_party/igc/NOTICES\"\n"
        ".global igc_notices_end\nigc_notices_end:\n.popsection\n");
uint32_t igc_mmio_read(struct igc_hw* hw,uint32_t reg) {
    uint32_t v=*(volatile uint32_t*)(hw->hw_addr+reg);
    if(reg==IGC_STATUS && v==0xffffffffu) hw->native_fault=true;
    return v;
}
void igc_mmio_write(struct igc_hw* hw,uint32_t reg,uint32_t value) {
    *(volatile uint32_t*)(hw->hw_addr+reg)=value;
}
void igc_delay_us(uint32_t us) {
    uint64_t start=timer_counter(),limit=(uint64_t)us*1000;
    while(timer_counter()-start<limit) __asm__ volatile("pause");
}
#endif

static int igc_carrier(netdev_t* nd) {
    igc_card_t* c=nd->priv;
    uint32_t now=timer_ms();
    if((uint32_t)(now-c->link_ms)>=500) {
        c->link_ms=now;
        c->hw.mac.get_link_status=true; /* polling replaces link-change IRQ */
        int err=igc_check_for_link(&c->hw);

        c->link=!err && !c->hw.mac.get_link_status && !c->hw.native_fault &&
                (IGC_READ_REG(&c->hw,IGC_STATUS)&IGC_STATUS_LU)!=0;
    }
    return c->link && !c->hw.native_fault;
}
static void igc_multicast(netdev_t* nd,int all) {
    igc_card_t* c=nd->priv;
    uint32_t v=IGC_READ_REG(&c->hw,IGC_RCTL);
    if(all) v|=IGC_RCTL_MPE; else v&=~IGC_RCTL_MPE;
    IGC_WRITE_REG(&c->hw,IGC_RCTL,v);
}
static void igc_reap(igc_card_t* c) {
    while(c->txused && (c->tx[c->txclean].wb.status&IGC_TXD_STAT_DD)) {
        IGC_BARRIER();
        c->txclean=(c->txclean+1)%IGC_TX_COUNT;c->txused--;
    }
}
static int igc_send(netdev_t* nd,const void* frame,uint32_t len) {
    igc_card_t* c=nd->priv;
    if(!frame || len<14 || len>ETH_FRAME_MAX || !igc_carrier(nd)) return -1;
    igc_reap(c);
    /* Leave one descriptor unused so head == tail always means empty. */
    if(c->txused==IGC_TX_COUNT-1) { nd->tx_errors++;return -1; }
    unsigned i=c->txcur;
    memcpy(c->txbuf[i],frame,len);
    c->tx[i].read.buffer_addr=(uint64_t)(uintptr_t)c->txbuf[i];
    c->tx[i].read.olinfo_status=len<<IGC_ADVTXD_PAYLEN_SHIFT;
    c->tx[i].read.cmd_type_len=len|IGC_ADVTXD_DTYP_DATA|IGC_ADVTXD_DCMD_DEXT|
        IGC_ADVTXD_DCMD_EOP|IGC_ADVTXD_DCMD_IFCS|IGC_ADVTXD_DCMD_RS;
    IGC_BARRIER();
    c->txcur=(i+1)%IGC_TX_COUNT;c->txused++;
    IGC_WRITE_REG(&c->hw,IGC_TDT(0),c->txcur);
    nd->tx_packets++;nd->tx_bytes+=len;
    return 0;
}
static int igc_poll(netdev_t* nd) {
    igc_card_t* c=nd->priv;
    if(c->hw.native_fault) return 0;
    igc_reap(c);
    int count=0;
    /* Bounded even if the NIC immediately refills descriptors. */
    for(unsigned budget=0;budget<IGC_RX_COUNT;budget++) {
        unsigned i=c->rxcur;
        uint32_t status=c->rx[i].wb.upper.status_error;
        if(!(status&IGC_RXD_STAT_DD)) break;
        IGC_BARRIER();
        unsigned len=c->rx[i].wb.upper.length;
        int eop=(status&IGC_RXD_STAT_EOP)!=0;
        if(c->drop_chain || !eop || len<14 || len>ETH_FRAME_MAX ||
           (status&(IGC_RXDEXT_STATERR_RXE|IGC_RXDADV_ERR_HBO))) {
            nd->rx_errors++;nd->rx_dropped++;
        } else {
            nd->rx_packets++;nd->rx_bytes+=len;
            net_rx(nd,c->rxbuf[i],len);count++;
        }
        c->drop_chain=!eop;
        /* Write-back overlays both buffer pointers: restore both. */
        c->rx[i].read.pkt_addr=(uint64_t)(uintptr_t)c->rxbuf[i];
        c->rx[i].read.hdr_addr=0;
        IGC_BARRIER();
        c->rxcur=(i+1)%IGC_RX_COUNT;
        IGC_WRITE_REG(&c->hw,IGC_RDT(0),i);
    }
    return count;
}
static int igc_valid_mac(const uint8_t* m) {
    unsigned any=0;
    for(unsigned i=0;i<6;i++) any|=m[i];
    return any && !(m[0]&1);
}
static void igc_ring_setup(igc_card_t* c) {
    struct igc_hw* hw=&c->hw;
    for(unsigned i=0;i<IGC_RX_COUNT;i++) c->rx[i].read.pkt_addr=(uint64_t)(uintptr_t)c->rxbuf[i];
    uint64_t a=(uint64_t)(uintptr_t)c->rx;
    IGC_WRITE_REG(hw,IGC_RXDCTL(0),0);
    IGC_WRITE_REG(hw,IGC_RDBAL(0),(uint32_t)a);
    IGC_WRITE_REG(hw,IGC_RDBAH(0),(uint32_t)(a>>32));
    IGC_WRITE_REG(hw,IGC_RDLEN(0),sizeof(c->rx));
    IGC_WRITE_REG(hw,IGC_SRRCTL(0),2|IGC_SRRCTL_DESCTYPE_ADV_ONEBUF);
    IGC_WRITE_REG(hw,IGC_RDH(0),0);
    IGC_WRITE_REG(hw,IGC_RDT(0),0);
    IGC_WRITE_REG(hw,IGC_RXDCTL(0),IGC_RXDCTL_QUEUE_ENABLE|8|(8<<8)|(4<<16));
    a=(uint64_t)(uintptr_t)c->tx;
    IGC_WRITE_REG(hw,IGC_TDBAL(0),(uint32_t)a);
    IGC_WRITE_REG(hw,IGC_TDBAH(0),(uint32_t)(a>>32));
    IGC_WRITE_REG(hw,IGC_TDLEN(0),sizeof(c->tx));
    IGC_WRITE_REG(hw,IGC_TDH(0),0);IGC_WRITE_REG(hw,IGC_TDT(0),0);
    IGC_WRITE_REG(hw,IGC_TXDCTL(0),0x1f|(1<<8)|(1<<16)|(1<<22)|IGC_TXDCTL_GRAN|(1<<25));
    /* No RSS, VLAN stripping or receive checksum offload. */
    IGC_WRITE_REG(hw,IGC_MRQC,0);
    IGC_WRITE_REG(hw,IGC_RXCSUM,0);
    uint32_t ctrl=IGC_READ_REG(hw,IGC_CTRL)&~IGC_CTRL_VME;
    IGC_WRITE_REG(hw,IGC_CTRL,ctrl);
    IGC_WRITE_REG(hw,IGC_RLPML,1518);
    IGC_WRITE_REG(hw,IGC_DMACR,IGC_READ_REG(hw,IGC_DMACR)&~IGC_DMACR_DMAC_EN);
    IGC_BARRIER();
    IGC_WRITE_REG(hw,IGC_RCTL,IGC_RCTL_EN|IGC_RCTL_BAM|IGC_RCTL_MPE|IGC_RCTL_SECRC);
    IGC_WRITE_REG(hw,IGC_RDT(0),IGC_RX_COUNT-1);
    uint32_t tctl=IGC_READ_REG(hw,IGC_TCTL)&~IGC_TCTL_CT;
    IGC_WRITE_REG(hw,IGC_TCTL,tctl|IGC_TCTL_EN|IGC_TCTL_PSP|IGC_TCTL_RTLC|
                  (IGC_COLLISION_THRESHOLD<<IGC_CT_SHIFT));
    IGC_WRITE_FLUSH(hw);
}
static int igc_attach(const pci_dev_t* pd) {
    if(igc_ncards>=IGC_CARDS) return -1;
    for(unsigned i=0;i<igc_ncards;i++) {
        pci_dev_t* p=&igc_cards[i]->pci;
        if(p->bus==pd->bus && p->dev==pd->dev && p->fn==pd->fn) return -1;
    }
    int io=0;
    uintptr_t bar=pci_bar(pd,0,&io);
    if(!bar || io) return -1;
    igc_card_t* c=kmalloc_aligned(sizeof(*c),128);
    if(!c) return -1;
    memset(c,0,sizeof(*c));c->pci=*pd;
    c->hw.back=c;c->hw.hw_addr=(void*)bar;c->hw.device_id=pd->device;c->hw.bus.func=pd->fn;
    /* Remember before DMA can be enabled: never free memory still reachable
     * by an unresponsive controller. Failed adapters are not registered. */
    igc_cards[igc_ncards++]=c;
    pci_enable(pd);
    /* Polling only: mask legacy/MSI-X and suppress PCI INTx. */
    uint16_t command=pci_read16(pd->bus,pd->dev,pd->fn,4);
    pci_write16(pd->bus,pd->dev,pd->fn,4,command|(1u<<10));
    struct igc_hw* hw=&c->hw;
    IGC_WRITE_REG(hw,IGC_IMC,0xffffffffu);IGC_WRITE_REG(hw,IGC_EIMC,0xffffffffu);
    if(igc_setup_init_funcs(hw,true) || hw->native_fault ||
       hw->phy.id!=I225_I_PHY_ID || hw->nvm.type!=igc_nvm_flash_hw ||
       !(IGC_READ_REG(hw,IGC_I225_PHPM)&IGC_I225_PHPM_RST_COMPL)) goto failed;
    hw->mac.autoneg=true;
    hw->phy.autoneg_wait_to_complete=false;
    hw->phy.autoneg_advertised=AUTONEG_ADVERTISE_SPEED_DEFAULT_2500;
    hw->phy.mdix=0; /* automatic MDI/MDIX */
    hw->mac.max_frame_size=1518;hw->dev_spec._i225.mtu=1500;
    hw->dev_spec._i225.eee_disable=true;
    hw->fc.high_water=34*1024-2048;hw->fc.low_water=hw->fc.high_water-16;
    hw->fc.requested_mode=igc_fc_full;hw->fc.pause_time=0x0680;hw->fc.send_xon=true;
    IGC_WRITE_REG(hw,IGC_CTRL_EXT,IGC_READ_REG(hw,IGC_CTRL_EXT)|IGC_CTRL_EXT_DRV_LOAD);
    if(igc_check_reset_block(hw) || igc_reset_hw(hw) || hw->native_fault) goto failed;
    unsigned reset_wait=1000;
    while((IGC_READ_REG(hw,IGC_CTRL)&IGC_CTRL_DEV_RST) && reset_wait) {
        reset_wait--;igc_delay_us(50);
    }
    if(!reset_wait) goto failed;
    if(!(IGC_READ_REG(hw,IGC_EECD)&IGC_EECD_AUTO_RD)) goto failed;
    if(igc_validate_nvm_checksum(hw) && igc_validate_nvm_checksum(hw)) goto failed;
    if(igc_read_mac_addr(hw) || !igc_valid_mac(hw->mac.addr)) goto failed;
    IGC_WRITE_REG(hw,IGC_WUC,0);
    if(igc_init_hw(hw) || igc_set_eee_i225(hw,false,false,false) || hw->native_fault) goto failed;
    IGC_WRITE_REG(hw,IGC_EIMC,0xffffffffu);
    igc_ring_setup(c);
    if(hw->native_fault) goto failed;
    c->nd.name="igc";c->nd.model=pd->device==0x15f3?"Intel I225-V (experimental)":"Intel I226-V (experimental)";
    c->nd.priv=c;c->nd.irq=0xff;
    memcpy(c->nd.mac,hw->mac.addr,6);
    c->nd.send=igc_send;c->nd.poll=igc_poll;c->nd.link_up=igc_carrier;c->nd.set_multicast=igc_multicast;
    c->link_ms=timer_ms()-500;
    net_register_device(&c->nd);
    klog("igc: experimental Intel 2.5GbE polling interface registered\n");
    return 0;
failed:
    IGC_WRITE_REG(hw,IGC_IMC,0xffffffffu);IGC_WRITE_REG(hw,IGC_EIMC,0xffffffffu);
    IGC_WRITE_REG(hw,IGC_RCTL,0);IGC_WRITE_REG(hw,IGC_TCTL,IGC_TCTL_PSP);
    klog("igc: controller initialization failed; interface not registered\n");
    return -1;
}
static int igc_scan(const pci_dev_t* pd,void* unused) {
    (void)unused;
    if(pd->vendor==0x8086 && (pd->device==0x15f3 || pd->device==0x125c) &&
       pd->class_code==2 && pd->subclass==0) igc_attach(pd);
    return 0;
}
void igc_probe_all(void) { pci_scan(igc_scan,NULL); }
