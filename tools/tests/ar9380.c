/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wifi.h"
#include "pci.h"
#include "kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void*calloc(size_t,size_t);
extern int posix_memalign(void**,size_t,size_t);
extern void free(void*);
static unsigned checks,nalloc,registered,received,enabled,txlen,rx_frames;
static void*allocs[128];
static uint32_t now;
static wifi_radio_t*radio;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();}}while(0)
#include "station_vectors.h"
#define ATHN_MODEL 1
#include "../../net/ar9380.c"
#undef abs
static uint32_t regs[2][32768];
static uint8_t physical[4096];
static uint16_t commands[2];
static uint32_t iq_results[6];
static unsigned fifo[2][2][64],fifo_head[2][2],fifo_count[2][2],otp_at;
static uint32_t otp[256];
static int otp_mode,iq_failed;
static unsigned rom_at;
static uint8_t pcfg[2][256],txcopy[4096];
static int reset_stuck,rtc_stuck,rom_stuck,cal_stuck,register_full,radio_off;
static unsigned writes;
static int pending_tx,failed_tx;
void*kmalloc(size_t n){void*p=calloc(1,n);CHECK(nalloc<128);if(p)allocs[nalloc++]=p;return p;}
void*kzalloc(size_t n){return kmalloc(n);}
void*kmalloc_aligned(size_t n,size_t align){
 void*p=0;if(posix_memalign(&p,align,n))return 0;CHECK(nalloc<128);allocs[nalloc++]=p;return p;
}
void kfree(void*p){for(unsigned i=0;i<nalloc;i++)if(allocs[i]==p){free(p);allocs[i]=allocs[--nalloc];return;}}
void klog(const char*f,...){(void)f;}
uint32_t timer_ms(void){return now;}
static void ath_wait_us(unsigned us){now+=(us+999)/1000;}
void random_bytes(void*p,uint32_t n){for(uint32_t i=0;i<n;i++)((uint8_t*)p)[i]=64+i;}
void net_irq_notify(void){}
void net_register_device(netdev_t*n){CHECK(commands[((ath_native_t*)((wifi_radio_t*)n->priv)->hw)->pci.dev&1]&0x400u);CHECK(n->irq==0xff&&!n->is_usb);registered++;n->present=!register_full;radio=n->priv;}
void net_unregister_device(netdev_t*n){n->present=0;}
void net_rx(netdev_t*n,const uint8_t*b,uint32_t len){CHECK(n==&radio->nd&&b&&len>=14&&len<=1514);received++;}
static unsigned card(struct athn_softc*s){ath_native_t*c=s->priv;CHECK(c->pci.dev<2);return c->pci.dev;}
static uint32_t ath_dma(struct athn_softc*s,const volatile void*p,size_t n){
 ath_native_t*c=s->priv;
 uintptr_t a=(uintptr_t)p,start=(uintptr_t)c;
 CHECK(a>=start&&a+n>=a&&a+n<=start+sizeof(*c));
 return 0x100000u*(card(s)+1)+(a-start);
}

static void complete(ath_native_t*c,unsigned flags){
 volatile struct ar_tx_status*d=&c->status[c->status_cur];
 CHECK(!(d->ds_status8&AR_TXS8_DONE));
 d->ds_info=0x168c0000u|AR_TXI_DESC_TX;
 d->ds_status1=(uint32_t)c->cookies[c->txclean]<<16;
 d->ds_status3=flags;
 __sync_synchronize();d->ds_status8=AR_TXS8_DONE;
}
static uint32_t ath_read(struct athn_softc*s,uint32_t reg){
 unsigned k=card(s);CHECK(reg<131072&&!(reg&3));
 if(reg>=0x2000&&reg<0x4000){rom_at=(reg-0x2000)/4;CHECK(rom_at<2048);return 0;}
 if(reg==AR_EEPROM_STATUS_DATA)return rom_stuck?AR_EEPROM_STATUS_DATA_BUSY:otp_mode?AR_EEPROM_STATUS_DATA_ABSENT_ACCESS:physical[rom_at*2]|(unsigned)physical[rom_at*2+1]<<8;
 if(reg>=AR_OTP_BASE(0)&&reg<=AR_OTP_BASE(255)){otp_at=(reg-AR_OTP_BASE(0))/4;return 0;}
 if(reg==AR_OTP_STATUS)return AR_OTP_STATUS_VALID;
 if(reg==AR_OTP_READ_DATA)return otp[otp_at];
 if(reg==AR_RTC_STATUS)return rtc_stuck?0:AR_RTC_STATUS_ON;
 if(reg==AR_RTC_RC&&reset_stuck)return AR_RTC_RC_MAC_WARM;
 if(reg==AR_GPIO_IN)return radio_off?0:65535;
 if(reg==AR_PHY_RFBUS_GRANT)return AR_PHY_RFBUS_GRANT_EN;
 for(unsigned i=0;i<3;i++){
  if(reg==AR_PHY_TX_IQCAL_STATUS_B(i))return iq_failed?AR_PHY_TX_IQCAL_STATUS_FAILED:0;
  for(unsigned j=0;j<3;j++)if(reg==AR_PHY_CHAN_INFO_TAB(i,j))
   return iq_results[j*2+!!(regs[k][AR_PHY_CHAN_INFO_MEMORY/4]&AR_PHY_CHAN_INFO_TAB_S2_READ)];
 }
 return regs[k][reg/4];
}
static void ath_write(struct athn_softc*s,uint32_t reg,uint32_t v){
 unsigned k=card(s);CHECK(reg<131072&&!(reg&3));
 CHECK(reg<0x2000||reg>=0x4000);writes++;
 regs[k][reg/4]=v;
 if(reg==AR_RTC_RC&&v){memset(fifo_count[k],0,sizeof(fifo_count[k]));memset(fifo_head[k],0,sizeof(fifo_head[k]));}
 if(reg==AR_PHY_AGC_CONTROL&&!cal_stuck)regs[k][reg/4]&=~(AR_PHY_AGC_CONTROL_CAL|AR_PHY_AGC_CONTROL_NF);
 if(reg==AR_PHY_TIMING4&&!cal_stuck)regs[k][reg/4]&=~AR_PHY_TIMING4_DO_CAL;
 if(reg==AR_PHY_TX_IQCAL_START&&!cal_stuck)regs[k][reg/4]&=~AR_PHY_TX_IQCAL_START_DO_CAL;
 if(reg==AR_QTXDP(0)){
  ath_native_t*c=s->priv;unsigned i=c->txclean;
  const volatile struct ar_tx_desc*d=&c->tx[i];
  CHECK(c->txused>=1&&c->txactive&&v==ath_dma(s,d,sizeof(*d)));
  CHECK(!d->ds_link&&d->ds_info==(0x168c0000u|23|AR_TXI_DESC_TX|AR_TXI_CTRL_STAT));
  CHECK(d->ds_segs[0].ds_data==ath_dma(s,c->txbuf[i],ATH_BUFSZ));
  for(unsigned j=1;j<4;j++)CHECK(!d->ds_segs[j].ds_data&&!d->ds_segs[j].ds_ctl);
  uint32_t sum=d->ds_info+d->ds_segs[0].ds_data+d->ds_segs[0].ds_ctl;
  CHECK((d->ds_ctl10&65535)==(((sum>>16)+(sum&65535))&65535));
  CHECK(d->ds_ctl10>>16==c->cookies[i]);
  txlen=MS(d->ds_segs[0].ds_ctl,AR_TXC_BUF_LEN);CHECK(txlen<=sizeof(txcopy)&&!(txlen&3));
  memcpy(txcopy,c->txbuf[i],txlen);
  if(!pending_tx)complete(c,failed_tx?AR_TXS3_EXCESSIVE_RETRIES:0);
 }
 if(reg==AR_LP_RXDP||reg==AR_HP_RXDP){
  unsigned q=reg==AR_HP_RXDP,cap=q?16:64;
  CHECK(fifo_count[k][q]<cap);
  fifo[k][q][(fifo_head[k][q]+fifo_count[k][q])%cap]=v;
  fifo_count[k][q]++;
 }
 if(reg==AR_Q_STATUS_RING_START||reg==AR_Q_STATUS_RING_END)CHECK(v);
}
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t off){
 (void)b;(void)f;CHECK(d<2&&!(off&1));if(!off)return 0x168c;
 if(off==4)return commands[d];
 return pcfg[d][off]|(uint16_t)pcfg[d][off+1]<<8;
}
void pci_write16(uint8_t b,uint8_t d,uint8_t f,uint8_t off,uint16_t v){
 (void)b;(void)f;CHECK(d<2&&!(off&1));
 if(off==4){commands[d]=v;return;}pcfg[d][off]=v;pcfg[d][off+1]=v>>8;
}
uintptr_t pci_bar(const pci_dev_t*p,int n,int*io){CHECK(n==0);*io=0;return(uintptr_t)regs[p->dev];}
void pci_enable_from(const pci_dev_t*p,const char*file){CHECK(p&&file);commands[p->dev]=(commands[p->dev]|7)&~0x400u;enabled++;}
static pci_dev_t pd={.vendor=0x168c,.device=0x0030,.class_code=2,.subclass=0x80};
int pci_scan(int(*cb)(const pci_dev_t*,void*),void*arg){
 for(unsigned i=0;i<2;i++){pci_dev_t p=pd;p.dev=i;int r=cb(&p,arg);if(r)return r;}return 0;
}

static unsigned block(unsigned end,unsigned ref,const uint8_t*data,unsigned len){
 CHECK(len<2048&&end>=len+5);
 uint8_t h[4]={(3u<<5)|(ref&31),((ref&32)<<2)|(len>>4),(len&15)<<4,0};
 uint16_t sum=0;for(unsigned i=0;i<len;i++)sum+=data[i];
 for(unsigned i=0;i<4;i++)physical[end--]=h[i];
 for(unsigned i=0;i<len;i++)physical[end--]=data[i];
 physical[end--]=sum;physical[end--]=sum>>8;return end;
}
static void init(void){
 while(nalloc)free(allocs[--nalloc]);
 ath_count=0;memset(ath_cards,0,sizeof(ath_cards));memset(regs,0,sizeof(regs));memset(physical,0,sizeof(physical));memset(pcfg,0,sizeof(pcfg));
 memset(fifo_count,0,sizeof(fifo_count));memset(fifo_head,0,sizeof(fifo_head));memset(otp,0,sizeof(otp));
 now=registered=received=enabled=txlen=rx_frames=writes=0;radio=0;
 reset_stuck=rtc_stuck=rom_stuck=cal_stuck=register_full=radio_off=pending_tx=failed_tx=otp_mode=iq_failed=0;
 commands[0]=commands[1]=7;
 for(unsigned k=0;k<2;k++)regs[k][AR_SREV/4]=(AR_SREV_VERSION_9380<<12)|(3u<<8)|255;
 const uint8_t mac_patch[]={2,6,2,0,0,0,0,1};block(1023,2,mac_patch,sizeof(mac_patch));
 int32_t vals[12]={100,1000,20,-100,1000,20,100,1000,20,100,1000,-20};
 uint32_t a[12];for(unsigned i=0;i<12;i++)a[i]=vals[i]&4095;
 iq_results[0]=a[0]|a[1]<<12|(a[2]&255)<<24;iq_results[1]=(a[2]>>8)|a[3]<<4;
 iq_results[2]=a[4]|a[5]<<12|(a[6]&255)<<24;iq_results[3]=(a[6]>>8)|a[7]<<4;
 iq_results[4]=a[8]|a[9]<<12|(a[10]&255)<<24;iq_results[5]=(a[10]>>8)|a[11]<<4;
}

static void raw_rom(const struct ar9380_eeprom*e){
 memset(physical,0,sizeof(physical));
 unsigned end=4095,len=sizeof(*e);const uint8_t*p=(const void*)e;
 uint8_t h[4]={0,len>>4,(len&15)<<4,0};uint16_t sum=0;
 for(unsigned i=0;i<len;i++)sum+=p[i];
 for(unsigned i=0;i<4;i++)physical[end--]=h[i];
 for(unsigned i=0;i<len;i++)physical[end--]=p[i];
 physical[end--]=sum;physical[end]=sum>>8;
}
static void template_rom(struct ar9380_eeprom*e){
 memcpy(e,ar9380_def_rom,sizeof(*e));e->macAddr[0]=2;e->macAddr[5]=1;
}
static void inject_q(const uint8_t*f,unsigned len,unsigned flags,unsigned q){
 ath_native_t*c=radio->hw;unsigned k=c->pci.dev,cap=q?16:64;
 CHECK(fifo_count[k][q]);
 uint32_t ptr=fifo[k][q][fifo_head[k][q]];fifo_head[k][q]=(fifo_head[k][q]+1)%cap;fifo_count[k][q]--;
 unsigned at=c->rxcur[q];CHECK(ptr==ath_dma(&c->hal,c->rxbuf[at],ATH_BUFSZ));
 uint8_t*buffer=c->rxbuf[at];volatile struct ar_rx_status*d=(volatile void*)buffer;
 unsigned h=ath_header(f,len),pad=(4-h%4)%4;
 CHECK(sizeof(*d)+len+pad+4<=ATH_BUFSZ);
 memset(buffer,0,ATH_BUFSZ);memcpy(buffer+sizeof(*d),f,h);memcpy(buffer+sizeof(*d)+h+pad,f+h,len-h);
 d->ds_info=0x168c0000;d->ds_status2=len+pad+4;d->ds_status5=50u<<24;
 d->ds_status11=AR_RXS11_DONE|AR_RXS11_FRAME_OK|flags;
 rx_frames++;radio->nd.poll(&radio->nd);
}
static void inject(const uint8_t*f,unsigned len,unsigned flags){inject_q(f,len,flags,0);}
static void expect_eapol(const uint8_t*b,unsigned n){CHECK(txlen>=32+n&&!memcmp(txcopy+32,b,n));}
int main(void){
 init();int rc=ath_attach(&pd);
 if(rc)printf("attach failed ok=%d ver=%x rev=%x flags=%x rom=%p tx/rx=%x/%x writes=%u\n",ath_cards[0]->hal.ok,ath_cards[0]->hal.mac_ver,ath_cards[0]->hal.mac_rev,ath_cards[0]->hal.flags,ath_cards[0]->hal.eep,ath_cards[0]->hal.txchainmask,ath_cards[0]->hal.rxchainmask,writes);
 CHECK(!rc&&registered==1&&enabled==1);
 ath_native_t*c=radio->hw;CHECK(c->hal.ok);
 CHECK(wifi_scan(radio)==0);inject(BEACON,sizeof(BEACON),0);CHECK(radio->nap==1);
 for(unsigned i=0;i<11;i++){now+=300;radio->nd.poll(&radio->nd);}
 CHECK(!wifi_connect(radio,(const uint8_t*)"TestNet",7,"12345678",0));
 inject(AUTH,sizeof(AUTH),0);inject(ASSOC,sizeof(ASSOC),0);CHECK(radio->state==WIFI_KEYS);
 inject(M1,sizeof(M1),0);expect_eapol(M2_EXPECTED,sizeof(M2_EXPECTED));
 inject(M3,sizeof(M3),0);expect_eapol(M4_EXPECTED,sizeof(M4_EXPECTED));CHECK(radio->state==WIFI_UP);
 uint8_t eth[60]={0};memset(eth,255,6);memcpy(eth+6,radio->nd.mac,6);eth[12]=8;
 CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK((txcopy[1]&0x40)&&!memcmp(txcopy+32,TX_CIPHER,sizeof(TX_CIPHER)));
 inject(UNICAST8,sizeof(UNICAST8),0);CHECK(received==1);
 inject_q(UNICAST9,sizeof(UNICAST9),0,1);CHECK(received==2);
 inject(GROUP1,sizeof(GROUP1),0);inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(UNICAST8,sizeof(UNICAST8),AR_RXS11_CRC_ERR);CHECK(received==3);
 for(unsigned i=0;i<100;i++)CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 pending_tx=1;CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 uint8_t saved[ATH_BUFSZ];memcpy(saved,c->txbuf[c->txclean],sizeof(saved));
 for(unsigned slot=1;slot<ATH_TXN;slot++)CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK(radio->nd.send(&radio->nd,eth,sizeof(eth))<0&&!memcmp(saved,c->txbuf[c->txclean],sizeof(saved)));
 pending_tx=0;complete(c,0);for(unsigned i=0;i<ATH_TXN;i++)ath_poll(radio);
 CHECK(!c->txused&&!c->txactive&&c->txclean==c->txprod);
 pending_tx=1;CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));now+=2100;ath_poll(radio);
 CHECK(!c->hal.ok&&!(commands[0]&4)&&radio->state==WIFI_DOWN&&!radio->pair.installed);

 #define BAD(flag) do{init();flag=1;CHECK(ath_attach(&pd)<0&&!enabled&&!registered&&!(commands[0]&4));}while(0)
 BAD(reset_stuck);BAD(rtc_stuck);BAD(rom_stuck);BAD(cal_stuck);BAD(iq_failed);
 init();register_full=1;CHECK(ath_attach(&pd)<0&&!(commands[0]&4));
 init();memset(physical,0,sizeof(physical));CHECK(ath_attach(&pd)<0&&!registered);
 init();physical[1018]^=1;CHECK(ath_attach(&pd)<0&&!registered);
 init();regs[0][AR_SREV/4]=(AR_SREV_VERSION_9380<<12)|(2u<<8)|255;CHECK(ath_attach(&pd)<0&&!registered&&!writes);
 init();pd.device=0x002a;CHECK(ath_attach(&pd)<0&&!nalloc);pd.device=0x0030;
 struct ar9380_eeprom e;
 for(unsigned mask=1;mask<=7;mask++){
  init();template_rom(&e);e.baseEepHeader.txrxMask=mask|(mask<<4);raw_rom(&e);
  int rc=ath_attach(&pd);CHECK((mask==1||mask==3||mask==7)?rc==0:rc<0);
  if(!rc){c=radio->hw;CHECK(c->hal.txchainmask==mask&&c->hal.rxchainmask==mask);
   CHECK(regs[0][AR_PHY_RX_CHAINMASK/4]==mask&&regs[0][AR_SELFGEN_MASK/4]==mask);}
 }
 init();regs[0][AR_ENT_OTP/4]=AR_ENT_OTP_CHAIN2_DISABLE;CHECK(!ath_attach(&pd));
 c=radio->hw;CHECK(c->hal.txchainmask==3&&c->hal.rxchainmask==3&&regs[0][AR_SELFGEN_MASK/4]==3);
 /* A failed calibration must restore the requested board mask too. */
 iq_failed=1;CHECK(ar9003_init_calib(&c->hal)!=0&&c->hal.txchainmask==3&&c->hal.rxchainmask==3);
 init();otp_mode=1;for(unsigned i=0;i<256;i++)otp[i]=physical[4*i]|(unsigned)physical[4*i+1]<<8|(unsigned)physical[4*i+2]<<16|(unsigned)physical[4*i+3]<<24;
 CHECK(!ath_attach(&pd)&&ath_cards[0]->hal.rom_bytes==1024);
 /* A board edge ceiling below the global cap must actually lower programmed power. */
 init();template_rom(&e);
 for(unsigned row=0;row<AR9380_NUM_CTLS_2G;row++)if((e.ctlIndex_2G[row]&15)==AR_CTL_11G)
  for(unsigned j=0;j<AR9380_NUM_BAND_EDGES_2G;j++)e.ctlPowerData_2G[row].ctlEdges[j]=(e.ctlPowerData_2G[row].ctlEdges[j]&0xc0)|3;
 raw_rom(&e);CHECK(!ath_attach(&pd));
 for(unsigned j=0;j<4;j++)CHECK(((regs[0][AR_PHY_PWRTX_RATE1/4]>>(j*8))&63)<=3);

 /* Hardware radio kill must decline attachment and stop only its own station. */
 init();template_rom(&e);e.baseEepHeader.rfSilent=AR_EEP_RFSILENT_ENABLED;raw_rom(&e);
 radio_off=1;CHECK(ath_attach(&pd)<0&&!registered&&!(commands[0]&4));
 init();template_rom(&e);e.baseEepHeader.rfSilent=AR_EEP_RFSILENT_ENABLED;raw_rom(&e);
 CHECK(!ath_attach(&pd));c=radio->hw;
 CHECK(regs[0][AR_GPIO_INPUT_EN_VAL/4]&AR_GPIO_INPUT_EN_VAL_RFSILENT_BB);
 CHECK(regs[0][AR_PHY_TEST/4]&AR_PHY_TEST_RFSILENT_BB);
 radio_off=1;now+=500;ath_poll(radio);
 CHECK(!c->hal.ok&&!(commands[0]&4));
 init();CHECK(!ath_attach(&pd));
 /* Full-width periodic calibration samples, including INT32_MIN. */
 c=radio->hw;
 for(unsigned i=0;i<3;i++){
  regs[0][AR_PHY_IQ_ADC_MEAS_0_B(i)/4]=512;
  regs[0][AR_PHY_IQ_ADC_MEAS_1_B(i)/4]=512;
  regs[0][AR_PHY_IQ_ADC_MEAS_2_B(i)/4]=0x80000000u;
 }
 ar9003_calib_iq(&c->hal);CHECK(c->hal.ok);
 uint32_t rng=0x93800222;
 for(unsigned trial=0;trial<1000;trial++){
  init();template_rom(&e);
  for(unsigned j=0;j<1+trial%8;j++){
   rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;((uint8_t*)&e)[rng%sizeof(e)]^=rng>>24;
  }
  raw_rom(&e);int rc=ath_attach(&pd);
  if(!rc)CHECK(ath_cards[0]->hal.ok&&ath_rom_valid(&ath_cards[0]->hal));
  else CHECK(!registered&&!(commands[0]&4));
 }
 /* Separate FIFOs exercise ownership/wrap and malformed lengths/chains/status. */
 init();CHECK(!ath_attach(&pd));c=radio->hw;
 for(unsigned trial=0;trial<20000;trial++){
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  unsigned q=trial&1,i=c->rxcur[q],cap=q?16:64;
  CHECK(fifo_count[0][q]);fifo_head[0][q]=(fifo_head[0][q]+1)%cap;fifo_count[0][q]--;
  memset(c->rxbuf[i],0,ATH_BUFSZ);struct ar_rx_status*d=(void*)c->rxbuf[i];
  d->ds_info=0x168c0000;d->ds_status2=rng&0x1fff;
  d->ds_status11=(rng>>16)|AR_RXS11_DONE;d->ds_status5=0xff000000u;
  ath_poll(radio);CHECK(c->hal.ok);
 }
 now+=31000;ath_poll(radio);now+=500;ath_poll(radio);CHECK(c->hal.ok&&!c->hal.cur_calib_mask);
 now+=250000;ath_poll(radio);now+=500;ath_poll(radio);CHECK(c->hal.ok);
 /* Corrupted separate completion ring metadata must not free/reuse active DMA. */
 for(unsigned kind=0;kind<3;kind++){
  init();CHECK(!ath_attach(&pd));c=radio->hw;pending_tx=1;
  CHECK(!ath_send(radio,BEACON,sizeof(BEACON)));complete(c,0);
  if(kind==0)c->status[c->status_cur].ds_status1^=1u<<16;
  if(kind==1)c->status[c->status_cur].ds_info^=1u<<8;
  if(kind==2)c->status[c->status_cur].ds_info^=1u<<16;
  ath_poll(radio);CHECK(!c->hal.ok&&!(commands[0]&4)&&c->txused==1);
 }
 init();CHECK(!ath_attach(&pd));c=radio->hw;
 ((struct ar_rx_status*)c->rxbuf[1])->ds_status11=AR_RXS11_DONE;
 ath_poll(radio);CHECK(!c->hal.ok&&!(commands[0]&4));
 init();ath9380_probe_all();CHECK(registered==2&&enabled==2);
 ath9380_probe_all();CHECK(registered==2&&enabled==2);
 ath_cards[0]->radio.state=ath_cards[1]->radio.state=WIFI_UP;
 ath_cards[0]->radio.pair.key[0]=0x42;ath_cards[1]->radio.pair.key[0]=0x43;
 ath_stop(ath_cards[0]);
 CHECK(ath_cards[0]->radio.state==WIFI_DOWN&&!ath_cards[0]->radio.pair.key[0]);
 CHECK(ath_cards[1]->hal.ok&&ath_cards[1]->radio.state==WIFI_UP&&ath_cards[1]->radio.pair.key[0]==0x43&&(commands[1]&4));
 init();printf("AR9380 native: %u checks passed\n",checks);return 0;
}
