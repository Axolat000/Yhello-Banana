/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native RTL8811CU/8821CU USB station. Hardware-only BSD HAL in native.h.
 * Initial production-cut C/D, RFE 0/6, single RF path, high-speed USB.
 */
#include "../third_party/rtw88/rtw8821c_native.h"
#include "wifi.h"
#include "usbcore.h"
#include "serial.h"
typedef struct {
 struct rtw_dev hal;
 usb_device_t*dev;wifi_radio_t wifi;
 usb_xfer_t boot,rx[2],tx[8],zlp[8];
 u8 in,out[3],logical[512];
 unsigned ntx,channel,queued,detached,tx_seen[8];
 u32 tx_ms[8],health_ms;
 int busy;
} cu_native_t;
static unsigned cu_count;
static u16 cu16(const u8*p){return p[0]|(u16)p[1]<<8;}
static u32 cu32(const u8*p){return cu16(p)|(u32)cu16(p+2)<<16;}
static void cup16(u8*p,u16 v){p[0]=v;p[1]=v>>8;}
static void cup32(u8*p,u32 v){cup16(p,v);cup16(p+2,v>>16);}
static void cu_stop(cu_native_t*c){
 c->hal.ok=0;
 if(c->wifi.ops)wifi_disconnect(&c->wifi);
}
/* USB objects/buffers stay resident once the host controller has seen them. */
static int cu_control(struct rtw_dev*d,u32 a,void*b,unsigned n,int rd){
 cu_native_t*c=d->priv;
 if(c->detached||!c->dev->present)return -1;
 /* Work around the USB always-on register-section access requirement. */
 if(a<=255||(a>=0x1000&&a<=0x10ff)){
  u8 section=0;
  if(usb_control(c->dev,USB_TYPE_VENDOR,5,0x4e0,0,&section,1)!=1)return -1;
 }
 int rc=usb_control(c->dev,USB_TYPE_VENDOR|(rd?USB_DIR_IN:0),5,a,0,b,n);
 return rc==(int)n?0:-1;
}
static void cu_delay(struct rtw_dev*d,unsigned us){
 (void)d;
 if(us>=1000){usb_delay_ms((us+999)/1000);return;}
#ifndef CU_MODEL
 /* Submillisecond RF timings must not round every register write to 1 ms. */
 if(timer_has_hardware_clock()){
  u64 start=timer_counter();while(timer_counter()-start<(u64)us*1000)__asm__ volatile("pause");
 }else while(us--)__asm__ volatile("outb %%al,$0x80"::"a"(0));
#else
 usb_delay_ms((us+999)/1000);
#endif
}
static void cu_desc(u8*b,unsigned n,unsigned offset,unsigned qsel,const u8*f,unsigned rate){
 memset(b,0,offset);
 cup32(b,n|((qsel==0x13?0:offset)<<16));
 cup32(b+4,(qsel<<8)|((offset>48?1u:0)<<24));
 if(f){
  cup32(b,cu32(b)|BIT(26)|((f[4]&1)?BIT(24):0)|BIT(31));
  cup32(b+12,BIT(8)|BIT(10)); /* explicit legacy rate, no hardware sequence */
  cup32(b+16,rate); /* Lowest AP-supported legacy rate; no automatic adaptation. */
  cup32(b+36,((cu16(f+22)>>4)&4095)<<12);
 }
 u16 sum=0;for(unsigned i=0;i<32;i+=2)sum^=cu16(b+i);cup16(b+28,sum);
}
static int cu_boot_tx(struct rtw_dev*d,const u8*p,unsigned n,unsigned qsel){
 cu_native_t*c=d->priv;usb_xfer_t*x=&c->boot;
 if(!d->ok||!p||!n||n>4097||x->status==USB_XFER_PENDING||c->detached)return -1;
 unsigned offset=48;
 cu_desc(x->buf,n,offset,qsel,NULL,0);memcpy(x->buf+offset,p,n);
 x->dev=c->dev;x->ep=c->out[0];x->len=offset+n;
 if(usb_submit(x)<0)return -1;
 c->queued=1;u32 start=timer_ms();
 while(x->status==USB_XFER_PENDING&&c->dev->present&&timer_ms()-start<2000){
  usb_hc_t*hc=c->dev->hc;if(hc&&hc->ops->poll)hc->ops->poll(hc);
  usb_wait();
 }
 if(x->status!=USB_XFER_OK||x->actual!=x->len||!c->dev->present){d->ok=0;return -1;}
 return 0;
}
static int cu_h2c(cu_native_t*c,u16 sub,unsigned length,u32 word){
 u8 p[32]={0};cup32(p,1u|(255u<<8)|((u32)sub<<16));
 cup32(p+4,length|((u32)c->hal.h2c.seq++<<16));cup32(p+8,word);
 return cu_boot_tx(&c->hal,p,sizeof(p),0x13);
}
static int cu_mbox(cu_native_t*c,u32 msg){
 struct rtw_dev*d=&c->hal;unsigned box=d->h2c.last_box_num,tries;
 for(tries=0;tries<100&&d->ok;tries++){
  if(!(rtw_read8(d,REG_HMETFR)&BIT(box)))break;
  cu_delay(d,100);
 }
 if(tries==100||!d->ok)return -1;
 rtw_write32(d,0x1f0+box*4,0);rtw_write32(d,0x1d0+box*4,msg);
 d->h2c.last_box_num=(box+1)%4;return d->ok?0:-1;
}
static void cu_min_power(struct rtw_dev*d){
 /* Lowest hardware gain index; no assumed dBm mapping or automatic power boost. */
 for(unsigned i=0;i<0x54;i+=4)rtw_write32(d,0x1d00+i,0);
}
static int cu_calibrate(cu_native_t*c){
 struct rtw_dev*d=&c->hal;
 cu_min_power(d);
 if(cu_h2c(c,0x0e,9,1))return -1;
 unsigned tries;u32 result=0;
 for(tries=0;tries<300&&d->ok;tries++){
  result=rtw_read_rf(d,0,RF_DTXLOK,RFREG_MASK);
  if(result==0xabcde)break;
  cu_delay(d,20000);
 }
 rtw_write_rf(d,0,RF_DTXLOK,RFREG_MASK,0);
 if(tries==300||!d->ok||(rtw_read32(d,REG_IQKFAILMSK)&255))return -1;
 cu_min_power(d);return d->ok?0:-1;
}
static int cu_channel(wifi_radio_t*r,unsigned ch){
 cu_native_t*c=r->hw;if(!c->hal.ok||c->detached||ch<1||ch>11)return -1;
 if(c->channel==ch)return 0;
 /* Do not change RF parameters while submitted packets still own the channel. */
 for(unsigned i=0;i<8;i++)if(c->tx[i].status==USB_XFER_PENDING||c->zlp[i].status==USB_XFER_PENDING)return -1;
 rtw_write8(&c->hal,REG_TXPAUSE,255);
 rtw8821c_set_channel(&c->hal,ch,RTW_CHANNEL_WIDTH_20,0);
 cu_min_power(&c->hal);
 rtw_write8(&c->hal,REG_TXPAUSE,0);
 c->channel=ch;
 return c->hal.ok?0:-1;
}

static const u8 cu_rates[12]={2,4,11,22,12,18,24,36,48,72,96,108};
static unsigned cu_low_rate(const wifi_radio_t*r){
 static const u8 order[12]={0,1,2,4,5,3,6,7,8,9,10,11};
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<(unsigned)r->ap.nrates&&j<16;j++)
  if((r->ap.rates[j]&127)==cu_rates[order[i]])return order[i];
 return 0;
}
static int cu_station(wifi_radio_t*r,const u8*bssid,unsigned aid){
 cu_native_t*c=r->hw;struct rtw_dev*d=&c->hal;
 if(!d->ok||c->detached||aid>2007)return -1;
 for(unsigned i=0;i<6;i++)rtw_write8(d,0x618+i,bssid?bssid[i]:0);
 rtw_write8_mask(d,0x102,3,bssid&&aid?2:0);
 rtw_write16(d,0x6a8,(aid&0x3fff)|0xc000);
 u32 supported=0,basic=0;
 for(unsigned i=0;i<12;i++)for(unsigned j=0;j<(unsigned)r->ap.nrates&&j<16;j++)
  if((r->ap.rates[j]&127)==cu_rates[i]){
   supported|=BIT(i);if(r->ap.rates[j]&128)basic|=BIT(i);
  }
 rtw_write32_mask(d,REG_RRSR,0xfffff,basic?basic:supported?supported:1);
 rtw_write8(d,REG_SLOT,bssid&&(r->ap.capability&0x400)?9:20);
 if(cu_mbox(c,1u|((bssid&&aid?1u:0)<<8)))return -1;
 return d->ok?0:-1;
}

static void cu_reap_tx(cu_native_t*c){
 for(unsigned i=0;i<8;i++)if(c->tx_seen[i]){
  if(c->tx[i].status==USB_XFER_PENDING||c->zlp[i].status==USB_XFER_PENDING){
   if(timer_ms()-c->tx_ms[i]>2000){cu_stop(c);return;}
  }else{
   if(c->tx[i].status!=USB_XFER_OK||c->zlp[i].status!=USB_XFER_OK||c->tx[i].actual!=c->tx[i].len)c->wifi.nd.tx_errors++;
   c->tx_seen[i]=0;
  }
 }
}
static int cu_send(wifi_radio_t*r,const u8*f,u32 n){
 cu_native_t*c=r->hw;
 if(!c->hal.ok||c->detached||!c->dev->present||!f||n<24||n>WIFI_FRAME_MAX)return -1;
 cu_reap_tx(c);if(!c->hal.ok)return -1;
 unsigned i;
 for(i=0;i<8;i++)if(c->tx[i].status!=USB_XFER_PENDING&&c->zlp[i].status!=USB_XFER_PENDING)break;
 if(i==8)return -1;
 usb_xfer_t*x=&c->tx[i];unsigned offset=48;
 /* Management queue uses the high-priority endpoint for the initial legacy path. */
 cu_desc(x->buf,n,offset,0x12,f,cu_low_rate(r));memcpy(x->buf+offset,f,n);x->len=offset+n;
 x->dev=c->dev;x->ep=c->out[0];
 if(usb_submit(x)<0)return -1;
 c->queued=1;c->tx_ms[i]=timer_ms();c->tx_seen[i]=1;
 if(!(x->len&511)){
  c->zlp[i].len=0;
  if(usb_submit(&c->zlp[i])<0){cu_stop(c);return -1;}
 }
 return 0;
}
static int cu_rx(cu_native_t*c,const u8*b,unsigned total){
 /* Validate the entire USB aggregate before any externally visible delivery. */
 if(!b||!total||total>16384)return -1;
 unsigned at=0,packets=0;
 while(at<total){
  if(total-at<24)return -1;
  unsigned n=cu32(b+at)&0x3fff,info=((cu32(b+at)>>16)&15)*8,shift=(cu32(b+at)>>24)&3;
  unsigned span=24+info+shift+n;
  if(!n||span>total-at)return -1;
  packets++;if(packets>128)return -1;
  unsigned next=(span+7)&~7u;
  if(span==total-at){at=total;break;}
  if(next>total-at)return -1;
  at+=next;
 }
 at=0;int delivered=0;
 while(at<total){
  u32 w0=cu32(b+at),w2=cu32(b+at+8);
  unsigned n=w0&0x3fff,info=((w0>>16)&15)*8,shift=(w0>>24)&3;
  const u8*f=b+at+24+info+shift;unsigned span=24+info+shift+n;
  int bad=w0&(BIT(14)|BIT(15)); /* CRC/ICV */
  if(w2&BIT(28)){ /* Firmware C2H event: not an 802.11 frame. */
   if(n<2)c->wifi.nd.rx_errors++;
  }else if(bad||n<24||n>WIFI_FRAME_MAX)c->wifi.nd.rx_errors++;
  else{
   int signal=-95;
   if((w0&BIT(26))&&info>=16){
    const u8*phy=b+at+24+shift;unsigned page=phy[0]&15;
    if(page==1)signal=(int)phy[1]-110;
   }
   wifi_rx(&c->wifi,f,n,signal);delivered++;
  }
  if(span==total-at)break;
  at+=(span+7)&~7u;
 }
 return delivered;
}
static int cu_poll(wifi_radio_t*r){
 cu_native_t*c=r->hw;struct rtw_dev*d=&c->hal;
 if(!d->ok||c->busy||c->detached)return 0;
 if(!c->dev->present){cu_stop(c);return 0;}
 c->busy=1;int delivered=0;
 cu_reap_tx(c);
 for(unsigned i=0;i<2&&d->ok;i++){
  usb_xfer_t*x=&c->rx[i];if(x->status==USB_XFER_PENDING)continue;
  if(x->status!=USB_XFER_OK||x->actual>x->len){cu_stop(c);break;}
  int rc=cu_rx(c,x->buf,x->actual);if(rc<0)r->nd.rx_errors++;else delivered+=rc;
  x->len=16384;
  if(usb_submit(x)<0){cu_stop(c);break;}
 }
 c->busy=0;return delivered;
}
static const wifi_ops_t cu_ops={cu_poll,cu_send,cu_channel,cu_station};
static int cu_board(cu_native_t*c){
 const u8*e=c->logical;struct rtw_dev*d=&c->hal;
 if(cu16(e)!=0x8129||e[0xb9]>63||((e[0xca]&31)!=0&&(e[0xca]&31)!=6)||
    (e[0xc1]&0xe0)==0x20||(e[0xbc]&16)||(e[0xbd]&8))return -1;
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=e[0x107+i];
 if(!any||(e[0x107]&1))return -1;
 for(unsigned i=0;i<11;i++)if(e[0x10+i]>63)return -1;
 d->rfe=e[0xca]&31;d->hal.pkg_type=!!(e[0xca]&32);
 d->efuse.tx_bb_swing_setting_2g=e[0xc5]==255?0:e[0xc5];
 d->efuse.tx_bb_swing_setting_5g=e[0xc6]==255?0:e[0xc6];
 memcpy(c->wifi.nd.mac,e+0x107,6);return 0;
}
typedef struct {usb_ep_desc_t in,out[3];unsigned ntx,nrx,seen;int iface,bad;} cu_eps_t;
static int cu_ep(const uint8_t*b,void*ctx){
 cu_eps_t*e=ctx;
 if(b[1]==USB_DT_INTERFACE&&b[0]>=sizeof(usb_iface_desc_t)){
  if(e->iface)return 1;
  const usb_iface_desc_t*i=(const usb_iface_desc_t*)b;
  e->iface=i->bInterfaceClass==0xff&&i->bInterfaceSubClass==0xff&&i->bInterfaceProtocol==0xff&&!i->bAlternateSetting&&i->bInterfaceNumber==0;
 }else if(b[1]==USB_DT_ENDPOINT&&b[0]>=sizeof(usb_ep_desc_t)&&e->iface){
  const usb_ep_desc_t*ep=(const usb_ep_desc_t*)b;
  if((ep->bmAttributes&3)!=USB_EP_BULK)return 0;
  unsigned addr=ep->bEndpointAddress,bit=(addr&15)+((addr&0x80)?16:0),mps=ep->wMaxPacketSize&0x7ff;
  if(!(addr&15)||(addr&0x70)||(ep->wMaxPacketSize&0xf800)||
     (mps!=8&&mps!=16&&mps!=32&&mps!=64&&mps!=512)||(e->seen&(1u<<bit))){e->bad=1;return 1;}
  e->seen|=1u<<bit;
  if(addr&0x80){if(e->nrx++)e->bad=1;else e->in=*ep;}
  else if(e->ntx<3)e->out[e->ntx++]=*ep;else e->bad=1;
 }
 return e->bad;
}
static int cu_config(usb_device_t*d,int cfg,cu_eps_t*e){
 memset(e,0,sizeof(*e));int rc=usb_walk_config(d,cfg,cu_ep,e);
 if(rc<0||!e->iface||e->nrx!=1||e->ntx<2||e->ntx>3||e->bad)return -1;
 if(d->speed!=USB_SPEED_HIGH)return -1;
 unsigned expected=d->speed==USB_SPEED_HIGH?512:64;
 if((e->in.wMaxPacketSize&0x7ff)!=expected)return -1;
 for(unsigned i=0;i<e->ntx;i++)if((e->out[i].wMaxPacketSize&0x7ff)!=expected)return -1;
 return 0;
}
static int cu_probe(usb_device_t*d){
 if(d->desc.idVendor!=0x0bda||(d->desc.idProduct!=0xc811&&d->desc.idProduct!=0x8811))return -1;
 for(int i=0;i<USB_MAX_CONFIGS;i++){cu_eps_t e;if(d->configs[i]&&!cu_config(d,i,&e))return i;}
 return -1;
}

static int cu_attach(usb_device_t*dev){
 cu_eps_t e;if(cu_config(dev,dev->active_config,&e))return -1;
 if(cu_count>=32)return -1;
 cu_native_t*c=kzalloc(sizeof(*c));if(!c)return -1;cu_count++;
 struct rtw_dev*d=&c->hal;d->ok=1;d->priv=c;d->control=cu_control;d->tx=cu_boot_tx;d->wait=cu_delay;
 c->dev=dev;d->chip=&cu_chip;d->hci.type=RTW_HCI_TYPE_USB;d->hci.rpwm_addr=0xfe58;d->hci.bulkout_num=e.ntx;
 c->ntx=e.ntx;c->in=e.in.bEndpointAddress;
 if(usb_open_endpoint(dev,&e.in))goto fail;
 for(unsigned i=0;i<e.ntx;i++){
  c->out[i]=e.out[i].bEndpointAddress;
  if(usb_open_endpoint(dev,&e.out[i]))goto fail;
 }
 c->boot.buf=usb_dma_alloc(4160);c->boot.status=USB_XFER_OK;
 if(!c->boot.buf)goto fail;
 u32 silicon=rtw_read32(d,REG_SYS_CFG1);
 d->hal.cut_version=BIT_GET_CHIP_VER(silicon);
 if(!d->ok||(silicon&(BIT_RTL_ID|BIT_RF_TYPE_ID))||d->hal.cut_version<2||d->hal.cut_version>3)goto fail;
 if(rtw_mac_power_on(d)||!d->ok)goto fail;
 extern const u8 cu_fw_start[],cu_fw_end[];
 struct firmware image={cu_fw_start,(u32)(cu_fw_end-cu_fw_start)};struct rtw_fw_state fw={&image};
 if(__rtw_download_firmware(d,&fw)||!d->ok||cu_efuse_read(d,c->logical)||cu_board(c))goto fail;
 if(rtw_init_trx_cfg(d)||rtw8821c_mac_init(d)||rtw_drv_info_cfg(d)||!d->ok)goto fail;
 /* High-speed USB, no RX aggregation offload or hardware decryption. */
 rtw_write8_clr(d,REG_TXDMA_PQ_MAP,BIT_RXDMA_AGG_EN);
 rtw_write16(d,REG_RXDMA_AGG_PG_TH,0x0100);
 rtw_write8(d,REG_RXDMA_MODE,BIT_DMA_BURST_CNT|BIT_DMA_MODE|FIELD_PREP(BIT_DMA_BURST_SIZE,BIT_DMA_BURST_SIZE_512));
 rtw_write16_set(d,REG_TXDMA_OFFSET_CHK,BIT_DROP_DATA_EN);
 if(cu_phy_init(d,c->logical[0xb9]))goto fail;
 rtw_write8(d,REG_HWSEQ_CTRL,0);rtw_write16(d,0x680,0);
 rtw_write32_clr(d,REG_RCR,BIT_APP_FCS|BIT_APP_MIC|BIT_APP_ICV);
 rtw_write32_set(d,REG_RCR,BIT_APP_PHYSTS);
 rtw_write16(d,REG_RETRY_LIMIT,0x0a0a);
 for(unsigned i=0;i<6;i++)rtw_write8(d,0x610+i,c->wifi.nd.mac[i]);
 rtw_write32(d,REG_RRSR,0xfff);rtw_write8(d,0x102,0);
 c->wifi.hw=c;c->wifi.ops=&cu_ops;c->wifi.nd.name="rtl8811cu";
 c->wifi.nd.model="Realtek RTL8811CU (experimental 2.4 GHz, lowest TX gain)";
 c->wifi.nd.is_usb=1;c->wifi.nd.irq=0xff;
 if(cu_h2c(c,0x0d,12,(d->fifo.rsvd_fw_txbuf_addr-d->fifo.rsvd_boundary)<<16)||
    cu_h2c(c,0x11,16,d->rfe|(4u<<8)|((u32)d->hal.cut_version<<16)|(1u<<24)|(1u<<28))||
    cu_channel(&c->wifi,1)||cu_calibrate(c)||!d->ok)goto fail;
 for(unsigned i=0;i<8;i++){
  c->tx[i].dev=dev;c->tx[i].ep=c->out[0];c->tx[i].buf=usb_dma_alloc(2112);c->tx[i].status=USB_XFER_OK;
  if(!c->tx[i].buf)goto fail;
  c->zlp[i].dev=dev;c->zlp[i].ep=c->out[0];c->zlp[i].buf=c->tx[i].buf;c->zlp[i].status=USB_XFER_OK;
 }
 for(unsigned i=0;i<2;i++){
  c->rx[i].dev=dev;c->rx[i].ep=c->in;c->rx[i].buf=usb_dma_alloc(16384);c->rx[i].len=16384;
  if(!c->rx[i].buf)goto fail;
 }
 for(unsigned i=0;i<2;i++){if(usb_submit(&c->rx[i])<0)goto fail;c->queued=1;}
 if(wifi_register(&c->wifi))goto fail;
 dev->drvdata=c;
 klog("rtl8811cu: firmware, radio and native station ready; physical testing pending\n");return 0;
fail:
 cu_stop(c);if(!c->queued){kfree(c);cu_count--;}
 klog("rtl8811cu: unsupported profile or initialization failure; no interface registered\n");return -1;
}
static void cu_detach(usb_device_t*dev){
 cu_native_t*c=dev->drvdata;if(!c)return;
 c->detached=1;cu_stop(c);wifi_unregister(&c->wifi);dev->drvdata=NULL;
}
const usb_driver_t rtl8811cu_driver={"rtl8811cu",cu_probe,cu_attach,NULL,cu_detach};
#ifndef CU_MODEL
__asm__(".pushsection .rodata\n.balign 16\n.global cu_fw_start\ncu_fw_start:\n"
 ".incbin \"firmware/realtek/rtw8821c_fw.bin\"\n"
 ".global cu_fw_end\ncu_fw_end:\n"
 ".global rtl8811cu_notice_start\nrtl8811cu_notice_start:\n"
 ".incbin \"third_party/rtw88/NOTICES\"\n"
 ".global rtl8811cu_notice_end\nrtl8811cu_notice_end:\n.popsection\n");
#endif
