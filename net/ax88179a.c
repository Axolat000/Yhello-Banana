/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native AX88179A vendor-mode driver.
 * Register/init/framing sequences adapted from ASIX ax88179a_772d.c/h:
 * Copyright (c) 2022 ASIX Electronic Corporation. Those source files grant
 * GPL version 2 or (at your option) any later version; used here under GPLv3.
 * See third_party/asix/README.md and NOTICES for pinned sources and notices.
 */
#include "usbnet.h"
#include "kstring.h"
#include "kheap.h"
#include "timer.h"
#include "serial.h"
#define A79_RX_SIZE (48u*1024)
#define A79_RX_FLAGS 0x1aa /* no IP padding, CRC errors dropped */
typedef struct {
 usb_ep_desc_t in,out;uint8_t iface,alt;int active,have_in,have_out,done,bad;
} a79_eps_t;
typedef struct {int ok;unsigned speed,full;uint16_t rxctl;} a79_t;
static uint32_t a79_get32(const uint8_t*b){return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static void a79_put32(uint8_t*b,uint32_t v){for(unsigned i=0;i<4;i++)b[i]=v>>(i*8);}
static int a79_walk(const uint8_t*d,void*ctx){
 a79_eps_t*e=ctx;
 if(d[1]==USB_DT_INTERFACE&&d[0]>=sizeof(usb_iface_desc_t)){
  const usb_iface_desc_t*i=(const void*)d;
  if(e->active&&e->have_in&&e->have_out){e->done=1;return 1;}
  e->active=i->bInterfaceClass==0xff;e->iface=i->bInterfaceNumber;e->alt=i->bAlternateSetting;
  e->have_in=e->have_out=e->bad=0;
 }else if(d[1]==USB_DT_ENDPOINT&&d[0]>=sizeof(usb_ep_desc_t)&&e->active){
  const usb_ep_desc_t*p=(const void*)d;
  if((p->bmAttributes&3)!=USB_EP_BULK)return 0;
  /* The selected firmware mode has bulk IN EP2 and bulk OUT EP3 only. */
  if(p->bEndpointAddress==0x82){if(e->have_in)e->bad=1;e->in=*p;e->have_in=1;}
  else if(p->bEndpointAddress==3){if(e->have_out)e->bad=1;e->out=*p;e->have_out=1;}
  else e->bad=1;
 }
 return 0;
}
static int a79_config(usb_device_t*d,int cfg,a79_eps_t*e){
 memset(e,0,sizeof(*e));
 if(usb_walk_config(d,cfg,a79_walk,e)<0||e->bad)return -1;
 if(!e->done&&!(e->active&&e->have_in&&e->have_out))return -1;
 unsigned mps=d->speed==USB_SPEED_SUPER?1024:d->speed==USB_SPEED_HIGH?512:d->speed==USB_SPEED_FULL?64:0;
 if(!mps||(e->in.wMaxPacketSize&0x7ff)!=mps||(e->out.wMaxPacketSize&0x7ff)!=mps)return -1;
 return 0;
}
static int a79_match(usb_device_t*d){
 return d->desc.idVendor==0x0b95&&d->desc.idProduct==0x1790&&d->desc.bcdDevice==0x0200;
}
static int a79_probe(usb_device_t*d){
 if(!a79_match(d))return -1;
 for(int i=0;i<USB_MAX_CONFIGS;i++){a79_eps_t e;if(d->configs[i]&&!a79_config(d,i,&e))return i;}
 return -1;
}
static int a79_ctl(usbnet_t*u,int read,uint8_t req,uint16_t value,uint16_t index,void*buf,uint16_t n){
 a79_t*a=u->priv;
 if(!a->ok)return -1;
 int rc=usb_control(u->dev,(read?USB_DIR_IN:0)|USB_TYPE_VENDOR|USB_RECIP_DEVICE,req,value,index,buf,n);
 if(rc!=n){a->ok=0;return -1;}return 0;
}
static uint32_t a79_read(usbnet_t*u,unsigned reg,unsigned n){
 uint8_t b[4]={0};a79_ctl(u,1,1,reg,n,b,n);return a79_get32(b);
}
static void a79_write(usbnet_t*u,unsigned reg,uint32_t v,unsigned n){
 uint8_t b[4];a79_put32(b,v);a79_ctl(u,0,1,reg,n,b,n);
}
static int a79_phy(usbnet_t*u,unsigned reg,int value){
 uint8_t b[2]={0};
 if(value>=0){b[0]=value;b[1]=value>>8;}
 if(a79_ctl(u,value<0,2,3,reg,b,2))return -1;
 unsigned v=b[0]|(unsigned)b[1]<<8;
 if(value<0&&v==65535){((a79_t*)u->priv)->ok=0;return -1;}
 return v;
}
static int a79_link(usbnet_t*u){
 a79_t*a=u->priv;if(!a->ok)return -1;
 (void)a79_phy(u,1,-1);int status=a79_phy(u,1,-1);
 if(status<0)return -1;
 if((status&0x24)!=0x24){a->speed=0;return 0;}
 int partner=a79_phy(u,5,-1),gig=a79_phy(u,10,-1);
 if(partner<0||gig<0)return -1;
 unsigned speed=0,full=0;
 if(gig&0x0800){speed=1000;full=1;}
 else if(partner&0x100){speed=100;full=1;}
 else if(partner&0x80)speed=100;
 else if(partner&0x40){speed=10;full=1;}
 else if(partner&0x20)speed=10;
 if(!speed)return 0;
 if(speed==a->speed&&full==a->full)return 1;
 static const uint8_t queues[7][5]={
  {5,0x7b,0,0x17,0x0f},{5,0xc0,2,6,0x0f},
  {7,0xf0,0,0x0c,0x0f},{6,0,0,6,0x0f},
  {5,0xc0,4,6,0x0f},{7,0xc0,4,6,0x0f},{7,0,0,3,0x3f}};
 unsigned q=6;
 if(u->dev->speed!=USB_SPEED_FULL){
  if(speed==1000)q=u->dev->speed==USB_SPEED_SUPER?0:1;
  else if(speed==100)q=(u->dev->speed==USB_SPEED_SUPER?2:4)+!full;
 }
 a79_write(u,0x0b,0,2);a79_write(u,0xb7,0,1);
 a79_write(u,0xb5,0xa5,1);a79_write(u,0x54,0x0410,2);
 a79_write(u,0xb8,0x29,1);
 a79_write(u,0x6d,speed==10?0xff70fa:0x7078,3);
 a79_write(u,0xc0,speed==10?0xfa:0x40,1);a79_write(u,0x0e,0,1);
 a79_ctl(u,0,1,0x2e,5,(void*)queues[q],5);
 /* Receive plus negotiated duplex/speed; software checksums, no jumbo. */
 unsigned medium=0x100|(speed==1000?1:0)|(full?2:0);
 a79_write(u,0x22,medium,2);a79_write(u,0x0b,a->rxctl,2);a79_write(u,0xb7,3,1);
 if(!a->ok)return -1;
 a->speed=speed;a->full=full;return 1;
}
static void a79_decap(usbnet_t*u,const uint8_t*b,uint32_t n){
 if(n<8||n>A79_RX_SIZE)goto bad;
 uint32_t tail=a79_get32(b+n-8),count=tail&0x1fff,table=tail>>13;
 if(!count||table>n-8||count>(n-8-table)/8||(n-8-table-count*8)%16)goto bad;
 uint32_t at=0;
 for(unsigned i=0;i<count;i++){
  uint32_t h=a79_get32(b+table+i*8),extra=a79_get32(b+table+i*8+4);
  unsigned size=(h>>16)&0x7fff,padded=(size+7)&~7u;
  /* No PTP/preemption, VLAN stripping or out-of-range payload spans. */
  if((extra&0xff000000u)||(h&0x400)||at>table||padded>table-at)goto bad;
  at+=padded;
 }
 at=0;
 for(unsigned i=0;i<count;i++){
  uint32_t h=a79_get32(b+table+i*8);unsigned size=(h>>16)&0x7fff;
  if(!(h&0x800)||(h&0x80000000u)||size<14||size>ETH_FRAME_MAX){u->nd.rx_errors++;u->nd.rx_dropped++;}
  else{u->nd.rx_packets++;u->nd.rx_bytes+=size;net_rx(&u->nd,b+at,size);}
  at+=(size+7)&~7u;
 }
 return;
bad:u->nd.rx_errors++;
}
static int a79_encap(usbnet_t*u,uint8_t*out,const uint8_t*frame,uint32_t n){
 if(!frame||n<14||n>ETH_FRAME_MAX)return -1;
 unsigned payload=n<60?60:n,total=(payload+8+7)&~7u;
 if(total>u->tx_size)return -1;
 memset(out,0,total);
 unsigned checksum=(payload+(payload>>8)+(payload>>16))&0x7f;
 a79_put32(out,payload|(checksum<<21));memcpy(out+8,frame,n);
 return total;
}
static int a79_attach(usb_device_t*d){
 a79_eps_t e;if(!a79_match(d)||a79_config(d,d->active_config,&e))return -1;
 usbnet_t*u=kzalloc(sizeof(*u));a79_t*a=kzalloc(sizeof(*a));
 if(!u||!a){kfree(u);kfree(a);return -1;}
 u->dev=d;u->priv=a;a->ok=1;a->rxctl=A79_RX_FLAGS;
 if(usb_control(d,USB_TYPE_STANDARD|USB_RECIP_IFACE,USB_REQ_SET_INTERFACE,e.alt,e.iface,0,0)!=0)goto fail;
 if((a79_read(u,5,1)>>4)!=6||!a->ok)goto fail;
 a79_ctl(u,0,8,1,0,0,0); /* select native vendor framing, one TX queue, no PTP */
 a79_ctl(u,0,6,0,0,0,0); /* reload existing flash/eFuse, never program it */
 uint8_t dc[4]={0};
 a79_ctl(u,1,0x81,0x3404,0,dc,4);
 a79_put32(dc,(a79_get32(dc)&0xffff98ffu)|0x6100);
 a79_ctl(u,0,0x81,0x3404,0,dc,4); /* disable USB link power states */
 uint8_t power=2;a79_ctl(u,0,0x31,0,0,&power,1);timer_sleep_ms(500);
 a79_ctl(u,0,0xc0,5u<<8,0,0,0); /* disable automatic detach */
 a79_write(u,0x35,0,1);a79_write(u,0x34,0,1);
 a79_write(u,0xb9,2,1);a79_write(u,0x0b,0,2);
 a79_write(u,0x55,4,1);a79_write(u,0x54,0x10,1);a79_write(u,0x2b,0,1);
 a79_write(u,0x41,255,1);
 a79_write(u,0x43,0,1);a79_write(u,0x46,0,1);
 a79_write(u,0x9e,0,1);a79_write(u,0xb1,0,1);a79_write(u,0xcc,1,1);
 a79_write(u,0xb7,0,1);
 unsigned monitor=a79_read(u,0x24,1);a79_write(u,0x24,(monitor&0xe0)|4,1);
 unsigned medium=a79_read(u,0x22,2);a79_write(u,0x22,medium&~1u,2);a79_write(u,0x22,medium|1,2);
 a79_write(u,0x0e,0,1);a79_write(u,0xb0,0,1);
 if(a79_ctl(u,1,1,0x10,6,u->nd.mac,6))goto fail;
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=u->nd.mac[i];
 if(!any||(u->nd.mac[0]&1)||!a->ok)goto fail;
 int phy_id=a79_phy(u,2,-1);if(phy_id<=0||phy_id==65535)goto fail;
 a79_phy(u,4,0x1e1);a79_phy(u,9,0x200);a79_phy(u,0,0x1200);
 if(!a->ok||usb_open_endpoint(d,&e.in)||usb_open_endpoint(d,&e.out))goto fail;
 u->ep_in=e.in.bEndpointAddress;u->ep_out=e.out.bEndpointAddress;
 u->mps_out=e.out.wMaxPacketSize&0x7ff;
 u->rx_size=A79_RX_SIZE;u->tx_size=(ETH_FRAME_MAX+15)&~7u;u->zlp=1;
 u->decap=a79_decap;u->encap=a79_encap;u->get_link=a79_link;
 u->nd.name="ax88179a";u->nd.model="ASIX AX88179A/B (experimental vendor mode)";
 if(usbnet_start(u)<0)goto fail;
 d->drvdata=u;return 0;
fail:
 if(a->ok){a79_write(u,0xb7,0,1);a79_write(u,0x0b,0,2);}
 /* start may have submitted transfers: retain their embedded URBs and buffers. */
 if(!u->submitted){kfree(a);kfree(u);}
 return -1;
}
static void a79_detach(usb_device_t*d){if(d->drvdata)usbnet_stop(d->drvdata);}
const usb_driver_t ax88179a_driver={"ax88179a",a79_probe,a79_attach,0,a79_detach};

__asm__(".pushsection .rodata\n.global ax88179a_notice_start\nax88179a_notice_start:\n"
        ".incbin \"third_party/asix/NOTICES\"\n"
        ".global ax88179a_notice_end\nax88179a_notice_end:\n.popsection\n");
