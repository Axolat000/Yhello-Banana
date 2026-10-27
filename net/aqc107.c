/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native polling AQC107 frontend. Shared hardware code retains OpenBSD,
 * Aquantia and NetBSD notices under third_party/aq.
 */
#include "../third_party/aq/aq_native.h"

#undef printf
#define AQ_RXN 128u
#define AQ_TXN 64u
#define AQ_BUFSZ 2048u
typedef struct {
 struct aq_softc hw;
 netdev_t nd;
 pci_dev_t pci;
 volatile struct aq_rx_desc_read rx[AQ_RXN] __attribute__((aligned(4096)));
 volatile struct aq_tx_desc tx[AQ_TXN] __attribute__((aligned(4096)));
 uint8_t rxbuf[AQ_RXN][AQ_BUFSZ] __attribute__((aligned(16)));
 uint8_t txbuf[AQ_TXN][AQ_BUFSZ] __attribute__((aligned(16)));
 unsigned rxcur,txprod,txclean,txused;
 int drop_chain,carrier;
 uint32_t link_ms;
} aq_native_t;
static aq_native_t* aq_cards[8];
static unsigned aq_count;
#ifndef AQC107_MODEL
static uint32_t aq_mmio_read(struct aq_softc*s,uint32_t a){
 if(a>0x10000-4||(a&3)){s->ok=0;return 0;}
 return *(volatile uint32_t*)(s->mmio+a);
}
static void aq_mmio_write(struct aq_softc*s,uint32_t a,uint32_t v){
 if(a>0x10000-4||(a&3)){s->ok=0;return;}
 *(volatile uint32_t*)(s->mmio+a)=v;
}
static void aq_wait_us(struct aq_softc*s,uint32_t n){
 (void)s;
 if(!timer_has_hardware_clock()){while(n--)(void)inb(0x80);return;}
 uint64_t start=timer_counter();while(timer_counter()-start<(uint64_t)n*1000)__asm__ volatile("pause");
}
#else
static uint32_t aq_mmio_read(struct aq_softc*,uint32_t);
static void aq_mmio_write(struct aq_softc*,uint32_t,uint32_t);
static void aq_wait_us(struct aq_softc*,uint32_t);
#endif
static void aq_stop(aq_native_t*c){
 c->hw.ok=0;c->carrier=0;
 uint16_t v=pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,4);
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,v&~4u);
}
static int aq_carrier(netdev_t*nd){
 aq_native_t*c=nd->priv;if(!c->hw.ok)return 0;
 if(timer_ms()-c->link_ms>=250){
  uint32_t version=AQ_READ_REG(&c->hw,AQ_FW_VERSION_REG);
  if(!version||version==0xffffffffu){aq_stop(c);return 0;}
  c->link_ms=timer_ms();enum aq_link_speed rate=AQ_LINK_NONE;
  c->carrier=aq_get_linkmode(&c->hw,&rate,NULL,NULL)==0&&rate!=AQ_LINK_NONE;
 }
 return c->carrier;
}
static void aq_reap(aq_native_t*c){
 unsigned head=AQ_READ_REG(&c->hw,TX_DMA_DESC_HEAD_PTR_REG(0));
 if(head>=AQ_TXN){aq_stop(c);return;}
 unsigned done=(head+AQ_TXN-c->txclean)%AQ_TXN;
 if(done>c->txused){aq_stop(c);return;}
 __sync_synchronize();
 c->txclean=head;c->txused-=done;
}
static int aq_send(netdev_t*nd,const void*f,uint32_t n){
 aq_native_t*c=nd->priv;if(!f||n<14||n>ETH_FRAME_MAX||!aq_carrier(nd))return -1;
 aq_reap(c);if(!c->hw.ok||c->txused>=AQ_TXN-1){nd->tx_errors++;return -1;}
 unsigned i=c->txprod,len=n<60?60:n;
 memset(c->txbuf[i],0,len);memcpy(c->txbuf[i],f,n);
 c->tx[i].buf_addr=(uintptr_t)c->txbuf[i];
 c->tx[i].ctl2=len<<AQ_TXDESC_CTL2_LEN_SHIFT;
 c->tx[i].ctl1=AQ_TXDESC_CTL1_TYPE_TXD|AQ_TXDESC_CTL1_CMD_FCS|AQ_TXDESC_CTL1_CMD_EOP|
     AQ_TXDESC_CTL1_CMD_WB|(len<<AQ_TXDESC_CTL1_BLEN_SHIFT);
 __sync_synchronize();c->txprod=(i+1)%AQ_TXN;c->txused++;
 AQ_WRITE_REG(&c->hw,TX_DMA_DESC_TAIL_PTR_REG(0),c->txprod);
 nd->tx_packets++;nd->tx_bytes+=n;return 0;
}
static int aq_poll(netdev_t*nd){
 aq_native_t*c=nd->priv;if(!c->hw.ok)return 0;
 aq_reap(c);if(!c->hw.ok)return 0;
 unsigned end=AQ_READ_REG(&c->hw,RX_DMA_DESC_HEAD_PTR_REG(0));
 if(end>=AQ_RXN){aq_stop(c);return 0;}
 int count=0;
 for(unsigned budget=0;c->rxcur!=end&&budget<AQ_RXN;budget++){
  unsigned i=c->rxcur;
  const volatile struct aq_rx_desc_wb*w=(const volatile void*)&c->rx[i];
  uint16_t status=w->status;if(!(status&AQ_RXDESC_STATUS_DD))break;
  __sync_synchronize();
  unsigned n=w->pkt_len;uint32_t type=w->type;
  int fragment=!(status&AQ_RXDESC_STATUS_EOP),bad=c->drop_chain||fragment;
  c->drop_chain=fragment;
  if(bad||(status&AQ_RXDESC_STATUS_MACERR)||(type&AQ_RXDESC_TYPE_DMA_ERR)||n<14||n>ETH_FRAME_MAX){
   nd->rx_errors++;nd->rx_dropped++;
  }else{nd->rx_packets++;nd->rx_bytes+=n;net_rx(nd,c->rxbuf[i],n);count++;}
  c->rx[i].hdr_addr=0;c->rx[i].buf_addr=(uintptr_t)c->rxbuf[i];
  __sync_synchronize();c->rxcur=(i+1)%AQ_RXN;
  AQ_WRITE_REG(&c->hw,RX_DMA_DESC_TAIL_PTR_REG(0),(c->rxcur+AQ_RXN-1)%AQ_RXN);
 }
 return count;
}
static void aq_multicast(netdev_t*nd,int all){
 aq_native_t*c=nd->priv;
 AQ_WRITE_REG_BIT(&c->hw,RPF_MCAST_FILTER_MASK_REG,RPF_MCAST_FILTER_MASK_ALLMULTI,!!all);
 AQ_WRITE_REG_BIT(&c->hw,RPF_MCAST_FILTER_REG(0),RPF_MCAST_FILTER_EN,!!all);
}
static int aq_rings(aq_native_t*c){
 struct aq_softc*sc=&c->hw;
 AQ_WRITE_REG_BIT(sc,TX_DMA_DESC_REG(0),TX_DMA_DESC_EN,0);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_REG(0),RX_DMA_DESC_EN,0);
 uint32_t cache=AQ_READ_REG(sc,RX_DMA_DESC_CACHE_INIT_REG);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_CACHE_INIT_REG,RX_DMA_DESC_CACHE_INIT,(cache&RX_DMA_DESC_CACHE_INIT)^RX_DMA_DESC_CACHE_INIT);
 uintptr_t tx=(uintptr_t)c->tx,rx=(uintptr_t)c->rx;
 AQ_WRITE_REG(sc,TX_DMA_DESC_BASE_ADDRLSW_REG(0),(uint32_t)tx);
 AQ_WRITE_REG(sc,TX_DMA_DESC_BASE_ADDRMSW_REG(0),(uint64_t)tx>>32);
 AQ_WRITE_REG(sc,RX_DMA_DESC_BASE_ADDRLSW_REG(0),(uint32_t)rx);
 AQ_WRITE_REG(sc,RX_DMA_DESC_BASE_ADDRMSW_REG(0),(uint64_t)rx>>32);
 AQ_WRITE_REG_BIT(sc,TX_DMA_DESC_REG(0),TX_DMA_DESC_LEN,AQ_TXN/8);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_REG(0),RX_DMA_DESC_LEN,AQ_RXN/8);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_BUFSIZE_REG(0),RX_DMA_DESC_BUFSIZE_DATA,AQ_BUFSZ/1024);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_BUFSIZE_REG(0),RX_DMA_DESC_BUFSIZE_HDR,0);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_REG(0),RX_DMA_DESC_HEADER_SPLIT,0);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_REG(0),RX_DMA_DESC_VLAN_STRIP,0);
 AQ_WRITE_REG(sc,TX_DMA_DESC_WRWB_THRESH_REG(0),0);
 c->txprod=c->txclean=AQ_READ_REG(sc,TX_DMA_DESC_HEAD_PTR_REG(0));
 c->rxcur=AQ_READ_REG(sc,RX_DMA_DESC_HEAD_PTR_REG(0));
 if(c->txprod>=AQ_TXN||c->rxcur>=AQ_RXN)return -1;
 for(unsigned i=0;i<AQ_RXN;i++)c->rx[i].buf_addr=(uintptr_t)c->rxbuf[i];
 __sync_synchronize();
 AQ_WRITE_REG(sc,TX_DMA_DESC_TAIL_PTR_REG(0),c->txprod);
 AQ_WRITE_REG(sc,RX_DMA_DESC_TAIL_PTR_REG(0),(c->rxcur+AQ_RXN-1)%AQ_RXN);
 AQ_WRITE_REG_BIT(sc,TDM_DCA_REG,TDM_DCA_EN,0);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DCA_REG,RX_DMA_DCA_EN,0);
 AQ_WRITE_REG_BIT(sc,TX_DMA_DESC_REG(0),TX_DMA_DESC_EN,1);
 AQ_WRITE_REG_BIT(sc,RX_DMA_DESC_REG(0),RX_DMA_DESC_EN,1);
 AQ_WRITE_REG(sc,AQ_INTR_MASK_CLR_REG,0xffffffffu);
 return sc->ok?0:-1;
}
static int aq_pci_setup(const pci_dev_t*p){
 uint16_t command=pci_read16(p->bus,p->dev,p->fn,4);
 pci_write16(p->bus,p->dev,p->fn,4,(command|2u|(1u<<10))&~4u);
 if(!(pci_read16(p->bus,p->dev,p->fn,6)&16))return 0;
 unsigned at=pci_read16(p->bus,p->dev,p->fn,0x34)&0xfc;
 uint64_t seen=0;
 while(at){
  if(at<0x40||at>0xfc)return -1;
  uint64_t bit=1ULL<<((at-0x40)/4);
  if(seen&bit)return -1;
  seen|=bit;
  uint16_t h=pci_read16(p->bus,p->dev,p->fn,at);
  if((h&255)==1){
   if(at>0xf8)return -1;
   uint16_t pm=pci_read16(p->bus,p->dev,p->fn,at+4);
   /* Enter D0 without acknowledging the neighbouring PME status W1C bit. */
   pci_write16(p->bus,p->dev,p->fn,at+4,pm&~0x8003u);
   aq_wait_us(NULL,10000);
  }
  if((h&255)==5){
   uint16_t v=pci_read16(p->bus,p->dev,p->fn,at+2);
   pci_write16(p->bus,p->dev,p->fn,at+2,v&~1u);
  }else if((h&255)==0x11){
   uint16_t v=pci_read16(p->bus,p->dev,p->fn,at+2);
   pci_write16(p->bus,p->dev,p->fn,at+2,v&~0x8000u);
  }
  at=(h>>8)&0xfc;
 }
 return 0;
}
static int aq_attach(const pci_dev_t*p){
 if(aq_count>=8||p->vendor!=0x1d6a||p->device!=0x07b1||p->class_code!=2||p->subclass)return -1;
 for(unsigned i=0;i<aq_count;i++)if(aq_cards[i]->pci.bus==p->bus&&aq_cards[i]->pci.dev==p->dev&&aq_cards[i]->pci.fn==p->fn)return -1;
 if(pci_read16(p->bus,p->dev,p->fn,0)!=0x1d6a)return -1;
 if(aq_pci_setup(p))return -1;
 int io=0;uintptr_t bar=pci_bar(p,0,&io);if(!bar||io)return -1;
 aq_native_t*c=kmalloc_aligned(sizeof(*c),4096);if(!c)return -1;
 memset(c,0,sizeof(*c));c->pci=*p;
 c->hw.mmio=(void*)bar;c->hw.ok=1;c->hw.read=aq_mmio_read;c->hw.write=aq_mmio_write;c->hw.wait=aq_wait_us;c->hw.sc_nqueues=1;
 aq_cards[aq_count++]=c; /* Retain all DMA storage even on failed reset. */
 struct aq_softc*sc=&c->hw;
 if(aq1_fw_reboot(sc)||!sc->ok||!(sc->sc_features&FEATURES_AQ1_REV_B))goto fail;
 if(aq_hw_reset(sc)||aq1_get_mac_addr(sc)||!sc->ok)goto fail;
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=sc->sc_enaddr.ether_addr_octet[i];
 if(!any||(sc->sc_enaddr.ether_addr_octet[0]&1))goto fail;
 if(aq_hw_init(sc,0,0)||!sc->ok)goto fail;
 AQ_WRITE_REG(sc,AQ_INTR_MASK_CLR_REG,0xffffffffu);
 AQ_WRITE_REG_BIT(sc,TPO_HWCSUM_REG,TPO_HWCSUM_IP4CSUM_EN,0);
 AQ_WRITE_REG_BIT(sc,TPO_HWCSUM_REG,TPO_HWCSUM_L4CSUM_EN,0);
 AQ_WRITE_REG_BIT(sc,RPO_HWCSUM_REG,RPO_HWCSUM_IP4CSUM_EN,0);
 AQ_WRITE_REG_BIT(sc,RPO_HWCSUM_REG,RPO_HWCSUM_L4CSUM_EN,0);
 if(aq_rings(c)||aq_set_linkmode(sc,AQ_LINK_ALL,AQ_FC_NONE,AQ_EEE_DISABLE)||!sc->ok)goto fail;
 AQ_WRITE_REG_BIT(sc,TPB_TX_BUF_REG,TPB_TX_BUF_EN,1);
 AQ_WRITE_REG_BIT(sc,RPB_RPF_RX_REG,RPB_RPF_RX_BUF_EN,1);
 memcpy(c->nd.mac,sc->sc_enaddr.ether_addr_octet,6);
 c->nd.name="aqc107";c->nd.model="Aquantia AQC107 (experimental)";
 c->nd.irq=0xff;c->nd.priv=c;c->nd.poll=aq_poll;c->nd.send=aq_send;c->nd.link_up=aq_carrier;c->nd.set_multicast=aq_multicast;
 pci_enable(p);net_register_device(&c->nd);
 if(!c->nd.present)goto fail;
 return 0;
fail:aq_stop(c);klog("aqc107: firmware/reset failed; no interface registered\n");return -1;
}
static int aq_scan(const pci_dev_t*p,void*ctx){(void)ctx;aq_attach(p);return 0;}
void aqc107_probe_all(void){pci_scan(aq_scan,NULL);}

__asm__(".pushsection .rodata\n.global aqc107_notice_start\naqc107_notice_start:\n"
        ".incbin \"third_party/aq/NOTICES\"\n"
        ".global aqc107_notice_end\naqc107_notice_end:\n.popsection\n");
