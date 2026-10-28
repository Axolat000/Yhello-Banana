/* SPDX-License-Identifier: GPL-3.0-or-later
 * Native AR9380 2.2 PCIe frontend; hardware routines retain ISC notices.
 * Exact 168c:0030 PCIe, MAC 2.2; legacy 2.4 GHz channels 1-11.
 */
#include "../third_party/athn/ar9380_native.h"
#undef printf
#undef malloc
#undef free
#include "pci.h"
#include "wifi.h"
#include "io.h"
#define ATH_RXN 80u
#define ATH_TXN 16u
#define ATH_BUFSZ 2304u
typedef struct {
 struct athn_softc hal;
 pci_dev_t pci;
 wifi_radio_t radio;
 struct ieee80211_channel chan[11];
 struct ieee80211_node node;
 volatile uint8_t*mmio;
 volatile struct ar_tx_status status[64] __attribute__((aligned(4096)));
 volatile struct ar_tx_desc tx[ATH_TXN] __attribute__((aligned(4096)));
 uint8_t rxbuf[ATH_RXN][ATH_BUFSZ] __attribute__((aligned(64)));
 uint8_t txbuf[ATH_TXN][ATH_BUFSZ] __attribute__((aligned(64)));
 uint8_t packet[ATH_BUFSZ];
 unsigned rxcur[2],txprod,txclean,txused,txactive,status_cur;
 uint16_t generation,cookies[ATH_TXN];
 unsigned rate,good,channel,drop_chain[2];
 uint8_t adaptive[ATH_TXN];
 uint32_t tx_ms,cal_ms,thermal_ms,nf_ms,health_ms,iq_ms;
} ath_native_t;
static ath_native_t*ath_cards[8];
static unsigned ath_count;
#ifndef ATHN_MODEL
static void ath_wait_us(unsigned us){
 if(!timer_has_hardware_clock()){while(us--)(void)inb(0x80);return;}
 uint64_t start=timer_counter();while(timer_counter()-start<(uint64_t)us*1000)__asm__ volatile("pause");
}
static uint32_t ath_dma(struct athn_softc*sc,const volatile void*p,size_t n){
 uint64_t a=(uintptr_t)p;
 if(!a||!n||a+n-1>0xffffffffu||a+n-1<a){sc->ok=0;return 0;}
 return a;
}
static uint32_t ath_read(struct athn_softc*sc,uint32_t reg){
 ath_native_t*c=sc->priv;if(reg>131068||(reg&3)){sc->ok=0;return 0;}
 return *(volatile uint32_t*)(c->mmio+reg);
}
static void ath_write(struct athn_softc*sc,uint32_t reg,uint32_t v){
 ath_native_t*c=sc->priv;if(reg>131068||(reg&3)){sc->ok=0;return;}
 *(volatile uint32_t*)(c->mmio+reg)=v;
}
#else
static uint32_t ath_dma(struct athn_softc*,const volatile void*,size_t);
static uint32_t ath_read(struct athn_softc*,uint32_t);
static void ath_write(struct athn_softc*,uint32_t,uint32_t);
#endif
static void ath_barrier(struct athn_softc*sc){(void)sc;__sync_synchronize();}
static void ath_stop(ath_native_t*c){
 c->hal.ok=0;
 if(c->radio.ops)wifi_disconnect(&c->radio);
 uint16_t v=pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,4);
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,v&~4u);
}
static int ath_ordered(const uint8_t*p,unsigned n,unsigned stride){
 int previous=-1,used=0,ended=0;
 for(unsigned i=0;i<n;i++){
  int f=p[i*stride];
  if(f==255){ended=1;continue;}
  if(ended||f==0||f<=previous)return 0;
  previous=f;used=1;
 }
 return used;
}
static int ath_rom_valid(struct athn_softc*sc){
 const struct ar9380_eeprom*e=sc->eep;
 unsigned tx=e->baseEepHeader.txrxMask>>4,rx=e->baseEepHeader.txrxMask&15;
 if(e->eepromVersion!=2||!(e->baseEepHeader.opFlags&AR_OPFLAGS_11G)||
    (tx!=1&&tx!=3&&tx!=7)||(rx!=1&&rx!=3&&rx!=7))return 0;
 unsigned any=0;for(unsigned i=0;i<6;i++)any|=e->macAddr[i];
 if(!any||(e->macAddr[0]&1))return 0;
 if((sc->flags&ATHN_FLAG_RFSILENT)&&(sc->rfsilent_pin<0||sc->rfsilent_pin>=16))return 0;
 if(!ath_ordered(e->calFreqPier2G,AR9380_NUM_2G_CAL_PIERS,1)||
    !ath_ordered(e->calTargetFbinCck,AR9380_NUM_2G_CCK_TARGET_POWERS,1)||
    !ath_ordered(e->calTargetFbin2G,AR9380_NUM_2G_20_TARGET_POWERS,1)||
    !ath_ordered(e->calTargetFbin2GHT20,AR9380_NUM_2G_20_TARGET_POWERS,1))return 0;
 if((e->baseEepHeader.txrxgain>>4)>3||(e->baseEepHeader.txrxgain&15)>1)return 0;
 for(unsigned ch=0;ch<11;ch++)if(ar9380_ctl_limit(e,112+5*ch,AR_CTL_11B)<0||
   ar9380_ctl_limit(e,112+5*ch,AR_CTL_11G)<0)return 0;
 for(unsigned row=0;row<AR9380_NUM_CTLS_2G;row++){
  if(!e->ctlIndex_2G[row])continue;
  int last=-1,ended=0;
  for(unsigned edge=0;edge<AR9380_NUM_BAND_EDGES_2G;edge++){
   int bin=e->ctl_freqbin_2G[row][edge];
   if(!bin||bin==255){ended=1;continue;}
   if(ended||bin<=last)return 0;
   last=bin;
  }
 }
 if(AR_READ(sc,AR_ENT_OTP)&AR_ENT_OTP_CHAIN2_DISABLE){sc->txchainmask&=3;sc->rxchainmask&=3;}
 return sc->txchainmask&&sc->rxchainmask;
}
static unsigned ath_header(const uint8_t*f,unsigned n){
 if(n<2)return 0;
 unsigned type=f[0]&12;
 if(type==4)return 0; /* ACK/CTS etc are consumed by the MAC. */
 unsigned h=24;
 if(type==8){
  if((f[1]&3)==3)h+=6;
  if(f[0]&0x80){h+=2;if(f[1]&0x80)h+=4;}
 }
 return h<=n?h:0;
}
static const uint8_t ath_rates[12]={2,4,11,12,18,22,24,36,48,72,96,108};
static const uint8_t ath_hw_rates[12]={0x1b,0x1a,0x19,0x0b,0x0f,0x18,0x0a,0x0e,0x09,0x0d,0x08,0x0c};
static unsigned ath_duration(unsigned len,unsigned rate){
 if(rate<3||rate==5)return 192+(len*16+ath_rates[rate]-1)/ath_rates[rate]+10;
 return 20+4*((16+len*8+6+ath_rates[rate]*2-1)/(ath_rates[rate]*2))+16;
}
static int ath_rate_supported(ath_native_t*c,unsigned r){
 for(unsigned i=0;i<c->radio.ap.nrates&&i<16;i++)if((c->radio.ap.rates[i]&127)==ath_rates[r])return 1;
 return 0;
}
static void ath_kick(ath_native_t*c){
 if(!c->hal.ok||c->txactive||!c->txused)return;
 c->txactive=1;c->tx_ms=timer_ms();
 __sync_synchronize();
 AR_WRITE(&c->hal,AR_QTXDP(0),ath_dma(&c->hal,&c->tx[c->txclean],sizeof(c->tx[0])));
}
static void ath_reap(ath_native_t*c){
 if(!c->txused){if(c->status[c->status_cur].ds_status8&AR_TXS8_DONE)ath_stop(c);return;}
 if(!c->txactive){ath_kick(c);return;}
 volatile struct ar_tx_status*d=&c->status[c->status_cur];
 if(!(d->ds_status8&AR_TXS8_DONE)){if(timer_ms()-c->tx_ms>2000)ath_stop(c);return;}
 __sync_synchronize();
 uint32_t info=d->ds_info;
 if((info&(AR_TXI_DESC_ID_M|AR_TXI_DESC_TX))!=
    (SM(AR_TXI_DESC_ID,AR_VENDOR_ATHEROS)|AR_TXI_DESC_TX)||
    MS(info,AR_TXI_QCU_NUM)!=0||(d->ds_status1>>16)!=c->cookies[c->txclean]){
  ath_stop(c);return;
 }
 int bad=!!(d->ds_status3&(AR_TXS3_EXCESSIVE_RETRIES|AR_TXS3_UNDERRUN));
 if(c->adaptive[c->txclean]){
  if(bad){if(c->rate)c->rate--;c->good=0;}
  else if(++c->good>=16){for(unsigned r=c->rate+1;r<12;r++)if(ath_rate_supported(c,r)){c->rate=r;break;}c->good=0;}
 }
 if(bad)c->radio.nd.tx_errors++;
 memset((void*)d,0,sizeof(*d));__sync_synchronize();
 c->status_cur=(c->status_cur+1)%64;c->txused--;c->txclean=(c->txclean+1)%ATH_TXN;c->txactive=0;
 ath_kick(c);
}
static int ath_send(wifi_radio_t*r,const uint8_t*f,uint32_t n){
 ath_native_t*c=r->hw;struct athn_softc*sc=&c->hal;
 if(!sc->ok||!f||n<24||n>WIFI_FRAME_MAX)return -1;
 ath_reap(c);if(!sc->ok||c->txused>=ATH_TXN)return -1;
 unsigned i=c->txprod,rate=0;int adaptive=r->state==WIFI_UP&&(f[0]&12)==8&&!(f[4]&1);
 if(adaptive&&ath_rate_supported(c,c->rate))rate=c->rate;
 unsigned h=ath_header(f,n);if(!h)return -1;
 unsigned pad=(4-h%4)%4,total=(n+pad+3)&~3u;
 if(total>ATH_BUFSZ)return -1;
 memset(c->txbuf[i],0,total);memcpy(c->txbuf[i],f,h);
 memcpy(c->txbuf[i]+h+pad,f+h,n-h);
 volatile struct ar_tx_desc*d=&c->tx[i];memset((void*)d,0,sizeof(*d));
 d->ds_info=SM(AR_TXI_DESC_ID,AR_VENDOR_ATHEROS)|SM(AR_TXI_DESC_NDWORDS,23)|AR_TXI_DESC_TX|AR_TXI_CTRL_STAT;
 d->ds_segs[0].ds_data=ath_dma(sc,c->txbuf[i],total);d->ds_segs[0].ds_ctl=SM(AR_TXC_BUF_LEN,total);
 d->ds_ctl11=SM(AR_TXC11_FRAME_LEN,n+4)|SM(AR_TXC11_XMIT_POWER,AR_MAX_RATE_POWER)|AR_TXC11_CLR_DEST_MASK;
 if(f[4]&1)d->ds_ctl12=AR_TXC12_NO_ACK;
 d->ds_ctl13=SM(AR_TXC13_XMIT_DATA_TRIES0,10)|AR_TXC13_DUR_UPDATE_ENA;
 d->ds_ctl14=SM(AR_TXC14_XMIT_RATE0,ath_hw_rates[rate]);
 d->ds_ctl15=SM(AR_TXC15_PACKET_DUR0,(f[4]&1)?0:ath_duration(14,0));
 d->ds_ctl18=SM(AR_TXC18_CHAIN_SEL0,sc->txchainmask);d->ds_ctl19=AR_TXC19_NOT_SOUNDING;
 uint32_t sum=d->ds_info+d->ds_segs[0].ds_data+d->ds_segs[0].ds_ctl;
 c->generation++;if(!c->generation)c->generation++;
 c->cookies[i]=c->generation;
 d->ds_ctl10=((uint32_t)c->generation<<16)|(((sum>>16)+(sum&65535))&65535);
 if(!sc->ok)return -1;
 c->adaptive[i]=adaptive;c->txused++;c->txprod=(i+1)%ATH_TXN;
 __sync_synchronize();ath_kick(c);return sc->ok?0:-1;
}
static int ath_rx_start(ath_native_t*c){
 struct athn_softc*sc=&c->hal;
 uint32_t reg=AR_READ(sc,AR_RXBP_THRESH);
 reg=RW(reg,AR_RXBP_THRESH_HP,1);reg=RW(reg,AR_RXBP_THRESH_LP,1);AR_WRITE(sc,AR_RXBP_THRESH,reg);
 AR_WRITE(sc,AR_DATABUF_SIZE,ATH_BUFSZ-sizeof(struct ar_rx_status));
 for(unsigned i=0;i<ATH_RXN;i++){
  memset(c->rxbuf[i],0,ATH_BUFSZ);
  AR_WRITE(sc,i<64?AR_LP_RXDP:AR_HP_RXDP,ath_dma(sc,c->rxbuf[i],ATH_BUFSZ));
 }
 c->rxcur[0]=0;c->rxcur[1]=64;c->drop_chain[0]=c->drop_chain[1]=0;
 if(!sc->ok)return -1;
 __sync_synchronize();AR_WRITE(sc,AR_CR,0);
 athn_set_rxfilter(sc,AR_RX_FILTER_UCAST|AR_RX_FILTER_BCAST|AR_RX_FILTER_MCAST|AR_RX_FILTER_BEACON);
 AR_WRITE(sc,AR_MCAST_FIL0,0xffffffffu);AR_WRITE(sc,AR_MCAST_FIL1,0xffffffffu);
 AR_WRITE(sc,AR_FILT_OFDM,0);AR_WRITE(sc,AR_FILT_CCK,0);AR_WRITE(sc,AR_MIBC,0);
 AR_SETBITS(sc,AR_DIAG_SW,AR_DIAG_ENCRYPT_DIS|AR_DIAG_DECRYPT_DIS);
 AR_CLRBITS(sc,AR_DIAG_SW,AR_DIAG_RX_DIS|AR_DIAG_RX_ABORT);
 return sc->ok?0:-1;
}
static void ar9003_reset_txsring(struct athn_softc*sc){
 ath_native_t*c=sc->priv;c->status_cur=0;
 memset((void*)c->status,0,sizeof(c->status));
 uint32_t begin=ath_dma(sc,c->status,sizeof(c->status));
 AR_WRITE(sc,AR_Q_STATUS_RING_START,begin);AR_WRITE(sc,AR_Q_STATUS_RING_END,begin+sizeof(c->status));
}
static int ath_channel(wifi_radio_t*r,unsigned ch){
 ath_native_t*c=r->hw;struct athn_softc*sc=&c->hal;
 if(!sc->ok||ch<1||ch>11)return -1;
 if(ch==c->channel)return 0;
 /* Core reset stops both DMA engines before descriptor storage is reused. */
 uint16_t cmd=pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,4);
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,cmd&~4u);
 sc->sc_ic.ic_bss->ni_chan=&c->chan[ch-1];sc->curchan=&c->chan[ch-1];
 if(sc->flags&ATHN_FLAG_RFSILENT)sc->ops.rfsilent_init(sc);
 if(athn_hw_reset(sc,&c->chan[ch-1],NULL,1)||!sc->ok){ath_stop(c);return -1;}
 athn_disable_interrupts(sc);
 for(int k=0;k<sc->kc_entries;k++)athn_reset_key(sc,k);
 c->txused=c->txactive=0;c->txclean=c->txprod=0;c->rate=c->good=0;
 if(ath_rx_start(c)){ath_stop(c);return -1;}
 pci_write16(c->pci.bus,c->pci.dev,c->pci.fn,4,cmd|4u);
 c->channel=ch;c->cal_ms=c->thermal_ms=c->nf_ms=timer_ms();return 0;
}
static int ath_station(wifi_radio_t*r,const uint8_t*bssid,unsigned aid){
 ath_native_t*c=r->hw;if(!c->hal.ok||aid>2007)return -1;
 uint8_t zero[6]={0};const uint8_t*b=bssid?bssid:zero;
 AR_WRITE(&c->hal,AR_BSS_ID0,LE_READ_4(b));
 AR_WRITE(&c->hal,AR_BSS_ID1,LE_READ_2(b+4)|SM(AR_BSS_ID1_AID,aid));
 c->rate=c->good=0;return c->hal.ok?0:-1;
}
static int ath_poll(wifi_radio_t*r){
 ath_native_t*c=r->hw;struct athn_softc*sc=&c->hal;if(!sc->ok)return 0;
 ath_reap(c);if(!sc->ok)return 0;
 uint32_t now=timer_ms();
 if(now-c->health_ms>=500){
  c->health_ms=now;
  if(pci_read16(c->pci.bus,c->pci.dev,c->pci.fn,0)!=0x168c){ath_stop(c);return 0;}
  if(sc->flags&ATHN_FLAG_RFSILENT){
   int on=sc->ops.gpio_read(sc,sc->rfsilent_pin);
   if(sc->flags&ATHN_FLAG_RFSILENT_REVERSED)on=!on;
   if(!on){ath_stop(c);return 0;}
  }
  if(sc->nf_calib_pending&&sc->ops.get_noisefloor(sc)){
   if(sc->nf_hist_nvalid<ATHN_NF_CAL_HIST_MAX)sc->nf_hist_nvalid++;
   athn_filter_noisefloor(sc);sc->ops.apply_noisefloor(sc);sc->nf_calib_pending=0;
  }
  if(now-c->nf_ms>=240000){athn_start_noisefloor_calib(sc,0);c->nf_ms=now;}
  if(now-c->thermal_ms>=30000){
   ar9380_set_correction(sc,&c->chan[c->channel-1]);
   c->thermal_ms=now;
  }
  if(sc->cur_calib_mask){
   sc->ops.next_calib(sc);
   if(sc->cur_calib_mask&&now-c->iq_ms>1000){ath_stop(c);return 0;}
  }else if(now-c->cal_ms>=30000){
   memset(&sc->calib,0,sizeof(sc->calib));sc->cur_calib_mask=ATHN_CAL_IQ;
   sc->ops.do_calib(sc);c->cal_ms=c->iq_ms=now;
  }
 }
 int delivered=0;
 for(unsigned q=0;q<2&&sc->ok;q++)for(unsigned budget=0;budget<(q?16u:64u);budget++){
  unsigned i=c->rxcur[q],first=q?64:0,count=q?16:64;
  volatile struct ar_rx_status*d=(volatile void*)c->rxbuf[i];
  if(!(d->ds_status11&AR_RXS11_DONE)){
   unsigned next=first+(i-first+1)%count;
   volatile struct ar_rx_status*after=(volatile void*)c->rxbuf[next];
   if(after->ds_status11&AR_RXS11_DONE)ath_stop(c);
   break;
  }
  __sync_synchronize();
  if((d->ds_info&(AR_RXI_DESC_ID_M|AR_RXI_DESC_TX|AR_RXI_CTRL_STAT))!=SM(AR_RXI_DESC_ID,AR_VENDOR_ATHEROS)){ath_stop(c);return delivered;}
  unsigned len=MS(d->ds_status2,AR_RXS2_DATA_LEN),more=!!(d->ds_status2&0x1000);
  uint32_t status=d->ds_status11;int bad=c->drop_chain[q]||more;c->drop_chain[q]=more;
  const uint8_t*f=c->rxbuf[i]+sizeof(*d);
  if(bad||!(status&AR_RXS11_FRAME_OK)||(status&(AR_RXS11_CRC_ERR|AR_RXS11_PHY_ERR|AR_RXS11_DECRYPT_CRC_ERR|AR_RXS11_MICHAEL_ERR))||
     len<28||len>ATH_BUFSZ-sizeof(*d))r->nd.rx_errors++;
  else{
   unsigned h=ath_header(f,len),pad=(4-h%4)%4;
   if(!h||len<h+pad+4||len-pad-4>WIFI_FRAME_MAX)r->nd.rx_errors++;
   else{
    memcpy(c->packet,f,h);memcpy(c->packet+h,f+h+pad,len-h-pad-4);
    unsigned raw=MS(d->ds_status5,AR_RXS5_RSSI_COMBINED);
    wifi_rx(r,c->packet,len-pad-4,raw==255?AR_DEFAULT_NOISE_FLOOR:(int)raw+AR_DEFAULT_NOISE_FLOOR);delivered++;
   }
  }
  memset((void*)d,0,sizeof(*d));__sync_synchronize();
  AR_WRITE(sc,q?AR_HP_RXDP:AR_LP_RXDP,ath_dma(sc,c->rxbuf[i],ATH_BUFSZ));
  c->rxcur[q]=first+(i-first+1)%count;
 }

 return delivered;
}
static const wifi_ops_t ath_ops={ath_poll,ath_send,ath_channel,ath_station};
static int ath_pci_setup(const pci_dev_t*p){
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
   pci_write16(p->bus,p->dev,p->fn,at+4,pm&~0x8003u);ath_wait_us(10000);
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

static int ath_attach(const pci_dev_t*p){
 if(ath_count>=8||p->vendor!=0x168c||p->device!=0x0030||p->class_code!=2||p->subclass!=0x80)return -1;
 for(unsigned i=0;i<ath_count;i++)if(ath_cards[i]->pci.bus==p->bus&&ath_cards[i]->pci.dev==p->dev&&ath_cards[i]->pci.fn==p->fn)return -1;
 if(pci_read16(p->bus,p->dev,p->fn,0)!=0x168c||ath_pci_setup(p))return -1;
 int io=0;uintptr_t bar=pci_bar(p,0,&io);if(!bar||io)return -1;
 ath_native_t*c=kmalloc_aligned(sizeof(*c),4096);if(!c)return -1;
 memset(c,0,sizeof(*c));c->pci=*p;c->mmio=(void*)bar;
 struct athn_softc*sc=&c->hal;sc->ok=1;sc->priv=c;sc->flags=ATHN_FLAG_PCIE;
 sc->ops.read=ath_read;sc->ops.write=ath_write;sc->ops.write_barrier=ath_barrier;
 memcpy(sc->sc_dev.dv_xname,"ath9380",8);
 ath_cards[ath_count++]=c; /* All DMA storage is retained, including failures. */
 athn_get_chipid(sc);if(!AR_SREV_9380(sc)||sc->mac_rev!=3||
    (AR_READ(sc,AR_SREV)&AR_SREV_TYPE2_HOST_MODE))goto fail;
 if(athn_reset_power_on(sc)||athn_set_power_awake(sc)||ar9380_attach(sc)||!sc->ok||!ath_rom_valid(sc))goto fail;
 sc->flags&=~ATHN_FLAG_PAPRD;
 sc->ntxchains=__builtin_popcount(sc->txchainmask);sc->nrxchains=__builtin_popcount(sc->rxchainmask);
 sc->sc_ic.ic_bss=&c->node;sc->sc_ic.ic_softc=sc;sc->sc_ic.ic_curmode=IEEE80211_MODE_11G;sc->sc_ic.ic_opmode=IEEE80211_M_STA;
 for(unsigned i=0;i<11;i++)c->chan[i].ic_freq=2412+5*i;
 c->radio.hw=c;c->radio.ops=&ath_ops;
 memcpy(c->radio.nd.mac,sc->sc_ic.ic_myaddr,6);
 c->radio.nd.irq=0xff;
 c->radio.nd.name="ath9380";c->radio.nd.model="Atheros AR9380 2.2 (experimental 2.4 GHz)";
 if(ath_channel(&c->radio,1))goto fail;
 pci_enable(p);
    /* The generic helper clears INTx-disable for interrupt-driven devices. */
    uint16_t poll_command=pci_read16(p->bus,p->dev,p->fn,4);
    pci_write16(p->bus,p->dev,p->fn,4,poll_command|0x400u);
 if(wifi_register(&c->radio))goto fail;
 return 0;
fail:ath_stop(c);klog("ath9380: unsupported silicon/EEPROM or radio initialization failure\n");return -1;
}
static int ath_scan(const pci_dev_t*p,void*a){(void)a;ath_attach(p);return 0;}
void ath9380_probe_all(void){pci_scan(ath_scan,NULL);}

