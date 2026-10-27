/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native BCM5720 A0 copper profile, implemented from Broadcom
 * 571X-5720-PG102 (2026-08-05), chapters 4, 7, 9, 13 and 14.
 * No bge/tg3 implementation or header is incorporated.
 * Active sideband management firmware is declined before reset.
 */
#include "netdev.h"
#include "pci.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "io.h"
#define BC_RX 512u
#define BC_TX 512u
#define BC_BUF 2048u
typedef struct {
 pci_dev_t pci;
 netdev_t nd;
 volatile uint8_t *mmio,*ape;
 volatile uint32_t rx[BC_RX][8] __attribute__((aligned(4096)));
 volatile uint32_t returns[BC_RX][8] __attribute__((aligned(4096)));
 volatile uint32_t tx[BC_TX][4] __attribute__((aligned(4096)));
 volatile uint32_t status[8] __attribute__((aligned(64)));
 uint8_t rxbuf[BC_RX][BC_BUF] __attribute__((aligned(16)));
 uint8_t txbuf[BC_TX][BC_BUF] __attribute__((aligned(16)));
 unsigned rxcons,rxpost,txprod,txclean,txused;
 unsigned phy;
 uint32_t cookies[BC_RX];
 uint8_t posted[BC_RX];
 uint32_t link_ms;
 int ok,carrier,nv_owned;
} bcm5720_t;
static bcm5720_t* bc_cards[8];
static unsigned bc_count;
#ifndef BCM5720_MODEL
static uint32_t bc_read(bcm5720_t*c,unsigned reg){
 if(reg>65532||(reg&3)){c->ok=0;return 0;}
 return *(volatile uint32_t*)(c->mmio+reg);
}
static void bc_write(bcm5720_t*c,unsigned reg,uint32_t value){
 if(reg>65532||(reg&3)){c->ok=0;return;}
 *(volatile uint32_t*)(c->mmio+reg)=value;
}
static uint32_t bc_ape_read(bcm5720_t*c,unsigned reg){
 if(reg>65532||(reg&3)){c->ok=0;return 0;}
 return *(volatile uint32_t*)(c->ape+reg);
}
static void bc_delay(unsigned us){
 if(!timer_has_hardware_clock()){while(us--)(void)inb(0x80);return;}
 uint64_t start=timer_counter();
 while(timer_counter()-start<(uint64_t)us*1000)__asm__ volatile("pause");
}
#else
static uint32_t bc_read(bcm5720_t*,unsigned);
static void bc_write(bcm5720_t*,unsigned,uint32_t);
static uint32_t bc_ape_read(bcm5720_t*,unsigned);
static void bc_delay(unsigned);
#endif
static int bc_poll_value(bcm5720_t*c,unsigned reg,uint32_t mask,uint32_t want,unsigned count,unsigned us){
 for(unsigned i=0;i<count&&c->ok;i++){if((bc_read(c,reg)&mask)==want)return 0;bc_delay(us);}
 c->ok=0;return -1;
}
static void bc_stop(bcm5720_t*c){
 c->carrier=0;c->ok=0;
 uint16_t v=pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,4);
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,v&~4u);
 if(c->nv_owned){bc_write(c,0x7020,1u<<5);c->nv_owned=0;}
}
static int bc_mdio(bcm5720_t*c,unsigned reg,int value){
 if(!c->ok||reg>31)return -1;
 uint32_t v=(1u<<29)|((value<0?2u:1u)<<26)|(c->phy<<21)|(reg<<16);
 if(value>=0)v|=(uint16_t)value;
 bc_write(c,0x44c,v);
 if(bc_poll_value(c,0x44c,1u<<29,0,400,5))return -1;
 v=bc_read(c,0x44c);
 if(v&(1u<<28)){c->ok=0;return -1;}
 return v&0xffff;
}
static int bc_phy_start(bcm5720_t*c){
 int id1=bc_mdio(c,2,-1),id2=bc_mdio(c,3,-1);
 if(id1<=0||id1==65535||id2<0||id2==65535)return -1;
 if(bc_mdio(c,0,0x8000)<0)return -1;
 unsigned tries;
 for(tries=0;tries<500;tries++){
  int v=bc_mdio(c,0,-1);if(v<0)return -1;if(!(v&0x8000))break;bc_delay(1000);
 }
 if(tries==500)return -1;
 /* IEEE Clause 22: 10/100 half/full, 1000 full, no pause or EEE. */
 if(bc_mdio(c,4,0x01e1)<0||bc_mdio(c,9,0x0200)<0||bc_mdio(c,0,0x1200)<0)return -1;
 return 0;
}
static int bc_carrier(netdev_t*nd){
 bcm5720_t*c=nd->priv;if(!c->ok)return 0;
 if(timer_ms()-c->link_ms>=250){
  c->link_ms=timer_ms();
  if(pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,0)!=0x14e4){bc_stop(c);return 0;}
  (void)bc_mdio(c,1,-1);int bmsr=bc_mdio(c,1,-1),aux=bc_mdio(c,25,-1);
  if(bmsr<0||aux<0){bc_stop(c);return 0;}
  unsigned mode=(unsigned)aux>>8&7;
  c->carrier=(bmsr&4)&&mode&&(mode!=4)&&(mode!=6);
  if(c->carrier){
   uint32_t mac=bc_read(c,0x400)&~13u;
   mac|=mode==7?8:4;
   if(mode==1||mode==3)mac|=1;
   bc_write(c,0x400,mac);
  }
 }
 return c->carrier;
}
static void bc_status(bcm5720_t*c){
 /* Polling: force a status DMA update without delivering an interrupt. */
 bc_write(c,0x3c00,bc_read(c,0x3c00)|0x808);
 __sync_synchronize();
 if(c->status[0]&4)bc_stop(c);
}
static void bc_reap(bcm5720_t*c){
 if(!c->ok)return;
 unsigned head=c->status[4]>>16;
 if(head>=BC_TX){bc_stop(c);return;}
 unsigned done=(head+BC_TX-c->txclean)%BC_TX;
 if(done>c->txused){bc_stop(c);return;}
 c->txclean=head;c->txused-=done;
}
static int bc_send(netdev_t*nd,const void*data,uint32_t len){
 bcm5720_t*c=nd->priv;
 if(!data||len<14||len>ETH_FRAME_MAX||!bc_carrier(nd))return -1;
 bc_status(c);bc_reap(c);
 if(!c->ok||c->txused>=BC_TX-1){nd->tx_errors++;return -1;}
 unsigned i=c->txprod,n=len<60?60:len;
 memset(c->txbuf[i],0,n);memcpy(c->txbuf[i],data,len);
 uintptr_t address=(uintptr_t)c->txbuf[i];
 c->tx[i][0]=(uint64_t)address>>32;c->tx[i][1]=(uint32_t)address;
 c->tx[i][2]=(n<<16)|4; /* packet end; no checksum/VLAN/segmentation offload */
 c->tx[i][3]=0;
 __sync_synchronize();c->txprod=(i+1)%BC_TX;c->txused++;
 bc_write(c,0x304,c->txprod);(void)bc_read(c,0x304);
 nd->tx_packets++;nd->tx_bytes+=len;return 0;
}
static void bc_post_rx(bcm5720_t*c,unsigned slot){
 uintptr_t address=(uintptr_t)c->rxbuf[slot];
 memset((void*)c->rx[slot],0,32);
 c->rx[slot][0]=(uint64_t)address>>32;c->rx[slot][1]=(uint32_t)address;
 c->rx[slot][2]=(slot<<16)|BC_BUF;
 c->cookies[slot]=slot;c->rx[slot][7]=slot;c->posted[slot]=slot<BC_RX-1;
}
static int bc_poll(netdev_t*nd){
 bcm5720_t*c=nd->priv;if(!c->ok)return 0;
 bc_status(c);bc_reap(c);if(!c->ok)return 0;
 unsigned end=c->status[4]&65535;
 if(end>=BC_RX){bc_stop(c);return 0;}
 int packets=0;
 for(unsigned budget=0;c->rxcons!=end&&budget<BC_RX;budget++){
  unsigned at=c->rxcons;uint32_t meta=c->returns[at][2],flags=c->returns[at][3];
  unsigned source=meta>>16,len=meta&65535;
  uint32_t errors=c->returns[at][5]>>16;
  /* Index is only an identifier, never a trusted pointer. */
  if(source>=BC_RX||!c->posted[source]||c->returns[at][7]!=c->cookies[source]){bc_stop(c);return packets;}
  c->posted[source]=0;
  if(!(flags&4)||(flags&0x400)||errors||len<18||len>ETH_FRAME_MAX+4){
   nd->rx_errors++;nd->rx_dropped++;
  }else{
   len-=4;nd->rx_packets++;nd->rx_bytes+=len;net_rx(nd,c->rxbuf[source],len);packets++;
  }
  memset((void*)c->returns[at],0,32);
  /* Producer entries are filled sequentially; retain the buffer identifier. */
  if(c->cookies[source]>0xffffffffu-BC_RX){bc_stop(c);return packets;}
  c->cookies[source]+=BC_RX;c->posted[source]=1;
  uintptr_t address=(uintptr_t)c->rxbuf[source];
  memset((void*)c->rx[c->rxpost],0,32);
  c->rx[c->rxpost][0]=(uint64_t)address>>32;c->rx[c->rxpost][1]=(uint32_t)address;
  c->rx[c->rxpost][2]=(source<<16)|BC_BUF;c->rx[c->rxpost][7]=c->cookies[source];
  __sync_synchronize();c->rxpost=(c->rxpost+1)%BC_RX;bc_write(c,0x26c,c->rxpost);
  c->rxcons=(at+1)%BC_RX;bc_write(c,0x284,c->rxcons);
 }
 return packets;
}
static void bc_multicast(netdev_t*nd,int all){
 bcm5720_t*c=nd->priv;
 for(unsigned off=0x470;off<=0x47c;off+=4)bc_write(c,off,all?0xffffffffu:0);
}
static int bc_core_reset(bcm5720_t*c){
 /* Shared NVRAM arbitration: request slot 1, never steal another grant. */
 bc_write(c,0x7020,2);c->nv_owned=1;
 if(bc_poll_value(c,0x7020,1u<<9,1u<<9,5000,50))return -1;
 pci_write32(c->pci.bus,c->pci.dev,c->pci.fn,0x68,0x9a);
 bc_write(c,0x7c,0);
 bc_write(c,0x8b50,0x4b657654);
 bc_write(c,0x6894,0);
 bc_write(c,0x4000,2);
 bc_write(c,0x6804,(1u<<29)|(65u<<1)|1u);
 bc_delay(100000);
 /* Reset clears host decode; restore only the command word, never status. */
 uint16_t command=pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,4);
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,(command|2u|0x400u)&~4u);
 pci_write32(c->pci.bus,c->pci.dev,c->pci.fn,0x68,0x9a);
 bc_write(c,0x7c,0);bc_write(c,0x4000,2);
 bc_write(c,0x6800,(bc_read(c,0x6800)&0x480c0u)|0x34);
 if(bc_poll_value(c,0x8b50,0xffffffffu,0xb49a89ab,20000,50))return -1;
 bc_write(c,0x7020,1u<<5);c->nv_owned=0;
 /* Return MAC clock selection to hardware following the boot handshake. */
 bc_write(c,0x3624,bc_read(c,0x3624)&~(1u<<31));
 return c->ok?0:-1;
}
static void bc_rcb(bcm5720_t*c,unsigned off,const volatile void*host,unsigned count,unsigned flags){
 uintptr_t address=(uintptr_t)host;
 bc_write(c,0x8000+off,(uint64_t)address>>32);
 bc_write(c,0x8004+off,(uint32_t)address);
 bc_write(c,0x8008+off,(count<<16)|flags);
 bc_write(c,0x800c+off,0);
}
static int bc_program(bcm5720_t*c){
 /* Little-endian descriptor words, host send descriptors, no data interrupts. */
 uint32_t mode=(bc_read(c,0x6800)&0x480c0u)|0x36034u;
 bc_write(c,0x6800,mode);
 pci_write32(c->pci.bus,c->pci.dev,c->pci.fn,0x6c,3u<<19);
 bc_write(c,0x6804,(65u<<1)|(1u<<29));
 bc_write(c,0x4410,0);bc_write(c,0x4414,42);bc_write(c,0x4418,160);
 bc_write(c,0x4400,6);
 if(bc_poll_value(c,0x4400,2,2,400,50))return -1;
 bc_write(c,0x5c00,0xffffffffu);bc_write(c,0x5c00,0);
 if(bc_poll_value(c,0x5c00,0xffffffffu,0,400,50))return -1;
 uintptr_t address=(uintptr_t)c->rx;
 bc_write(c,0x2450,(uint64_t)address>>32);bc_write(c,0x2454,(uint32_t)address);
 bc_write(c,0x2458,BC_RX<<16);
 bc_write(c,0x26c,0);
 bc_write(c,0x2c18,0x19);bc_write(c,0x2d00,0x20);
 /* Disable all unused queues before exposing ring zero. */
 for(unsigned i=0;i<4;i++)bc_rcb(c,0x100+i*16,NULL,0,2);
 bc_rcb(c,0x100,c->tx,BC_TX,0);bc_write(c,0x810c,0x4000);
 for(unsigned i=0;i<16;i++)bc_rcb(c,0x200+i*16,NULL,BC_RX,2);
 bc_rcb(c,0x200,c->returns,BC_RX,0);
 bc_write(c,0x304,0);bc_write(c,0x284,0);
 unsigned seed=0;for(unsigned i=0;i<6;i++)seed+=c->nd.mac[i];
 bc_write(c,0x438,seed&1023);bc_write(c,0x43c,1518);
 bc_write(c,0x410,((unsigned)c->nd.mac[0]<<8)|c->nd.mac[1]);
 bc_write(c,0x414,((uint32_t)c->nd.mac[2]<<24)|((uint32_t)c->nd.mac[3]<<16)|
    ((unsigned)c->nd.mac[4]<<8)|c->nd.mac[5]);
 bc_write(c,0x464,(bc_read(c,0x464)&0xffc00000u)|0x2620);
 bc_write(c,0x500,8);bc_write(c,0x2010,0x181);
 bc_write(c,0x2018,0x7bffff);bc_write(c,0x2014,1);
 bc_write(c,0xc0c,0xffffff);bc_write(c,0xc08,3);
 bc_write(c,0x3c00,0);
 if(bc_poll_value(c,0x3c00,2,0,400,50))return -1;
 bc_write(c,0x3c08,72);bc_write(c,0x3c0c,20);
 bc_write(c,0x3c10,5);bc_write(c,0x3c14,53);
 bc_write(c,0x3c20,5);bc_write(c,0x3c24,5);
 address=(uintptr_t)c->status;
 bc_write(c,0x3c38,(uint64_t)address>>32);bc_write(c,0x3c3c,(uint32_t)address);
 bc_write(c,0x3c00,0x1902); /* 32-byte status; force updates do not interrupt. */
 bc_write(c,0x3000,6);bc_write(c,0x2000,2);
 bc_write(c,0x400,0xe0d804);bc_delay(40);
 bc_write(c,0x4c00,0x200003fe);bc_delay(40);
 bc_write(c,0x4900,bc_read(c,0x4900)|4);
 bc_write(c,0x4800,(bc_read(c,0x4800)&0x20000000u)|0x303fe);
 bc_write(c,0x4910,(bc_read(c,0x4910)&~0xf0000u)|0xe0000);
 bc_delay(40);
 bc_write(c,0x2800,6);bc_write(c,0x1000,2);bc_write(c,0x1c00,6);
 bc_write(c,0x2400,0x12);bc_write(c,0x2c00,2);
 bc_write(c,0xc00,2);bc_write(c,0x1800,6);bc_write(c,0x1400,6);
 bc_write(c,0x45c,(bc_read(c,0x45c)&0x60u)|0x102);bc_delay(100);
 bc_write(c,0x468,0x1700402);bc_delay(10); /* keep VLAN tags; IPv6 recognition. */
 bc_write(c,0x504,1);bc_write(c,0x40c,0x800);
 bc_write(c,0x454,0xc0000); /* MDIO clock, autopoll off. */
 bc_write(c,0x450,1);
 return c->ok?0:-1;
}

static int bc_pci_setup(const pci_dev_t*p){
 uint16_t command=pci_read16(p->bus,p->dev,p->fn,4);
 pci_write16(p->bus,p->dev,p->fn,4,(command|2u|0x400u)&~4u);
 if(!(pci_read16(p->bus,p->dev,p->fn,6)&16))return 0;
 unsigned at=pci_read16(p->bus,p->dev,p->fn,0x34)&0xfc;
 uint64_t seen=0;
 while(at){
  if(at<0x40||at>0xfc)return -1;
  uint64_t bit=1ULL<<((at-0x40)/4);if(seen&bit)return -1;seen|=bit;
  uint16_t h=pci_read16(p->bus,p->dev,p->fn,at);
  unsigned id=h&255;
  if(id==1){
   if(at>0xf8)return -1;
   uint16_t pm=pci_read16(p->bus,p->dev,p->fn,at+4);
   pci_write16(p->bus,p->dev,p->fn,at+4,pm&~0x8003u);bc_delay(10000);
  }else if(id==5||id==0x11){
   uint16_t v=pci_read16(p->bus,p->dev,p->fn,at+2);
   pci_write16(p->bus,p->dev,p->fn,at+2,v&~(id==5?1u:0x8000u));
  }else if(id==0x10){
   if(at>0xec)return -1;
   uint16_t v=pci_read16(p->bus,p->dev,p->fn,at+8);
   /* MPS 128, MRRS 2K; do not set relaxed ordering or no-snoop. */
   pci_write16(p->bus,p->dev,p->fn,at+8,(v&~0x78f0u)|0x4000u);
   v=pci_read16(p->bus,p->dev,p->fn,at+16);
   pci_write16(p->bus,p->dev,p->fn,at+16,v&~3u);
  }
  at=(h>>8)&0xfc;
 }
 return 0;
}
static int bc_dma_range(const volatile void*p,size_t n){
 uint64_t a=(uintptr_t)p;return n&&a+n-1>=a&&(a>>32)==((a+n-1)>>32);
}
static int bc_attach(const pci_dev_t*p){
 if(bc_count>=8||p->vendor!=0x14e4||p->device!=0x165f||
    p->class_code!=2||p->subclass||p->fn>1)return -1;
 for(unsigned i=0;i<bc_count;i++)if(bc_cards[i]->pci.bus==p->bus&&
    bc_cards[i]->pci.dev==p->dev&&bc_cards[i]->pci.fn==p->fn)return -1;
 if(pci_read16(p->bus,p->dev,p->fn,0)!=0x14e4||
    pci_read32(p->bus,p->dev,p->fn,0xf4)!=0x05720000)return -1;
 if(bc_pci_setup(p))return -1;
 int io=0;uintptr_t bar=pci_bar(p,0,&io);if(!bar||io)return -1;
 uintptr_t ape=pci_bar(p,2,&io);if(!ape||io)return -1;
 bcm5720_t*c=kmalloc_aligned(sizeof(*c),4096);if(!c)return -1;
 memset(c,0,sizeof(*c));c->pci=*p;c->mmio=(void*)bar;c->ape=(void*)ape;
 c->ok=1;c->phy=p->fn+1;
 bc_cards[bc_count++]=c; /* DMA memory is never recycled on a failed device. */
 /* Initial copper profile excludes sideband/BMC ownership. No reset or
  * NVRAM request is sent to a controller with an active APE signature. */
 uint32_t signature=bc_ape_read(c,0x4000),strap=bc_read(c,0x3664);
 if(signature!=0||strap==0xffffffffu||(strap&0x20))goto fail;
 if(bc_core_reset(c))goto fail;
 /* Boot firmware publishes its factory address in the communication SRAM. */
 uint32_t hi=bc_read(c,0x8c14),lo=bc_read(c,0x8c18);
 if((hi>>16)!=0x484b)goto fail;
 c->nd.mac[0]=hi>>8;c->nd.mac[1]=hi;
 c->nd.mac[2]=lo>>24;c->nd.mac[3]=lo>>16;c->nd.mac[4]=lo>>8;c->nd.mac[5]=lo;
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=c->nd.mac[i];
 if(!any||(c->nd.mac[0]&1))goto fail;
 if(!bc_dma_range(c->rx,sizeof(c->rx))||!bc_dma_range(c->tx,sizeof(c->tx))||
    !bc_dma_range(c->returns,sizeof(c->returns))||!bc_dma_range(c->status,sizeof(c->status)))goto fail;
 for(unsigned i=0;i<BC_RX;i++){
  if(!bc_dma_range(c->rxbuf[i],BC_BUF)||!bc_dma_range(c->txbuf[i],BC_BUF))goto fail;
  bc_post_rx(c,i);
 }
 if(bc_program(c)||bc_phy_start(c)||!c->ok)goto fail;
 for(unsigned r=0x470;r<=0x47c;r+=4)bc_write(c,r,0);
 c->nd.name="bcm5720";c->nd.model="Broadcom BCM5720 (experimental)";
 c->nd.priv=c;c->nd.irq=0xff;c->nd.send=bc_send;c->nd.poll=bc_poll;
 c->nd.link_up=bc_carrier;c->nd.set_multicast=bc_multicast;
 /* One unused producer entry distinguishes full from empty. */
 c->rxpost=BC_RX-1;
 __sync_synchronize();bc_write(c,0x26c,c->rxpost);
 pci_enable(p);
    /* The generic helper clears INTx-disable for interrupt-driven devices. */
    uint16_t poll_command=pci_read16(p->bus,p->dev,p->fn,4);
    pci_write16(p->bus,p->dev,p->fn,4,poll_command|0x400u);net_register_device(&c->nd);
 if(!c->nd.present)goto fail;
 return 0;
fail:
 bc_stop(c);klog("bcm5720: unsupported profile or initialization failure; no interface\n");return -1;
}
static int bc_scan(const pci_dev_t*p,void*arg){(void)arg;bc_attach(p);return 0;}
void bcm5720_probe_all(void){pci_scan(bc_scan,NULL);}
