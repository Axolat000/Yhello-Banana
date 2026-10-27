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
#include "../../net/ar9280.c"
#undef abs
static uint32_t regs[2][16384];
static uint16_t rom[4096],commands[2];
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
static uint32_t ath_read(struct athn_softc*s,uint32_t reg){
 unsigned k=card(s);CHECK(reg<65536&&!(reg&3));
 if(reg>=0x2000&&reg<0x4000){rom_at=(reg-0x2000)/4;CHECK(rom_at<4096);return 0;}
 if(reg==AR_EEPROM_STATUS_DATA)return rom_stuck?AR_EEPROM_STATUS_DATA_BUSY:rom[rom_at];
 if(reg==AR_RTC_STATUS)return rtc_stuck?0:AR_RTC_STATUS_ON;
 if(reg==AR_RTC_RC&&reset_stuck)return AR_RTC_RC_MAC_WARM;
 if(reg==AR_GPIO_IN_OUT&&radio_off)return 0;
 return regs[k][reg/4];
}
static void ath_write(struct athn_softc*s,uint32_t reg,uint32_t v){
 unsigned k=card(s);CHECK(reg<65536&&!(reg&3));
 CHECK(reg<0x2000||reg>=0x4000);writes++;
 regs[k][reg/4]=v;
 if(reg==AR_PHY_AGC_CONTROL&&!cal_stuck)regs[k][reg/4]&=~(AR_PHY_AGC_CONTROL_CAL|AR_PHY_AGC_CONTROL_NF);
 if(reg==AR_PHY_TIMING_CTRL4_0&&!cal_stuck)regs[k][reg/4]&=~AR_PHY_TIMING_CTRL4_DO_CAL;
 if(reg==AR_Q_TXE&&(v&1)){
  ath_native_t*c=s->priv;unsigned i=c->txclean;
  CHECK(c->txused>=1&&c->txactive&&regs[k][AR_QTXDP(0)/4]==ath_dma(s,&c->tx[i],sizeof(c->tx[i])));
  CHECK(!c->tx[i].ds_link&&!c->tx[i].ds_ctl6&&!(c->tx[i].ds_ctl0&AR_TXC0_DEST_IDX_VALID));
  txlen=MS(c->tx[i].ds_ctl1,AR_TXC1_BUF_LEN);CHECK(txlen<=sizeof(txcopy));
  memcpy(txcopy,c->txbuf[i],txlen);
  if(!pending_tx){
   c->tx[i].ds_status1=failed_tx?AR_TXS1_EXCESSIVE_RETRIES:AR_TXS1_FRM_XMIT_OK;c->tx[i].ds_status9=AR_TXS9_DONE;
   regs[k][reg/4]=0;
  }
 }
 if(reg==AR_RXDP&&(commands[k]&4))CHECK(v);
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
static pci_dev_t pd={.vendor=0x168c,.device=0x002a,.class_code=2,.subclass=0x80};
int pci_scan(int(*cb)(const pci_dev_t*,void*),void*arg){
 for(unsigned i=0;i<2;i++){pci_dev_t p=pd;p.dev=i;int r=cb(&p,arg);if(r)return r;}return 0;
}
static void rom_checksum(void){
 struct ar5416_eeprom*e=(void*)(rom+AR5416_EEP_START_LOC);
 e->baseEepHeader.checksum=0;uint16_t sum=0;
 for(unsigned i=0;i<sizeof(*e)/2;i++)sum^=rom[AR5416_EEP_START_LOC+i];
 e->baseEepHeader.checksum=sum^65535;
}
static void init(void){
 while(nalloc)free(allocs[--nalloc]);
 ath_count=0;memset(ath_cards,0,sizeof(ath_cards));memset(regs,0,sizeof(regs));memset(rom,0,sizeof(rom));memset(pcfg,0,sizeof(pcfg));
 now=registered=received=enabled=txlen=rx_frames=writes=0;radio=0;
 reset_stuck=rtc_stuck=rom_stuck=cal_stuck=register_full=radio_off=pending_tx=failed_tx=0;
 commands[0]=commands[1]=7;
 for(unsigned k=0;k<2;k++)regs[k][AR_SREV/4]=(AR_SREV_VERSION_9280<<12)|(AR_SREV_REVISION_9280_20<<8)|255;
 rom[AR_EEPROM_MAGIC_OFFSET]=AR_EEPROM_MAGIC;
 struct ar5416_eeprom*e=(void*)(rom+AR5416_EEP_START_LOC);
 e->baseEepHeader.length=sizeof(*e);e->baseEepHeader.version=(AR_EEP_VER<<12)|21;
 e->baseEepHeader.opCapFlags=AR_OPFLAGS_11G;e->baseEepHeader.rxMask=e->baseEepHeader.txMask=3;
 e->baseEepHeader.macAddr[0]=2;e->baseEepHeader.macAddr[5]=1;
 e->baseEepHeader.pwrTableOffset=(uint8_t)-5;
 e->modalHeader[1].xpdGain=1;e->modalHeader[1].pdGainOverlap=2;
 for(unsigned i=0;i<AR5416_NUM_2G_CAL_PIERS;i++)e->calFreqPier2G[i]=112+i*25;
 for(unsigned i=0;i<AR5416_NUM_2G_CCK_TARGET_POWERS;i++){
  e->calTargetPowerCck[i].bChannel=112+i*25;memset(e->calTargetPowerCck[i].tPow2x,30,4);
 }
 for(unsigned i=0;i<AR5416_NUM_2G_20_TARGET_POWERS;i++){
  e->calTargetPower2G[i].bChannel=112+i*20;memset(e->calTargetPower2G[i].tPow2x,30,4);
  e->calTargetPower2GHT20[i].bChannel=112+i*20;memset(e->calTargetPower2GHT20[i].tPow2x,30,8);
 }
 for(unsigned chain=0;chain<2;chain++)for(unsigned pier=0;pier<AR5416_NUM_2G_CAL_PIERS;pier++)
  for(unsigned gain=0;gain<4;gain++)for(unsigned j=0;j<AR5416_PD_GAIN_ICEPTS;j++){
   e->calPierData2G[chain][pier].pwrPdg[gain][j]=20+j*8;
   e->calPierData2G[chain][pier].vpdPdg[gain][j]=30+j*20+pier*2;
  }
 rom_checksum();
}
static void inject(const uint8_t*f,unsigned len,unsigned flags){
 ath_native_t*c=radio->hw;unsigned i=c->rxcur;volatile struct ar_rx_desc*d=&c->rx[i];
 CHECK(len+8<=ATH_BUFSZ);
 unsigned h=ath_header(f,len),pad=(4-h%4)%4;
 memset(c->rxbuf[i],0,ATH_BUFSZ);memcpy(c->rxbuf[i],f,h);memcpy(c->rxbuf[i]+h+pad,f+h,len-h);
 d->ds_status1=len+pad+4;d->ds_status4=50u<<24;d->ds_status8=AR_RXS8_DONE|AR_RXS8_FRAME_OK|flags;
 rx_frames++;radio->nd.poll(&radio->nd);
}
static void expect_eapol(const uint8_t*b,unsigned n){CHECK(txlen>=32+n&&!memcmp(txcopy+32,b,n));}
int main(void){
 init();CHECK(!ath_attach(&pd)&&registered==1&&enabled==1);
 ath_native_t*c=radio->hw;CHECK(c->hal.ok&&c->hal.ntxchains==2&&c->hal.nrxchains==2);
 CHECK(c->radio.nd.mac[0]==2);
 CHECK((regs[0][AR_DIAG_SW/4]&(AR_DIAG_ENCRYPT_DIS|AR_DIAG_DECRYPT_DIS))==(AR_DIAG_ENCRYPT_DIS|AR_DIAG_DECRYPT_DIS));
 CHECK(wifi_scan(radio)==0);inject(BEACON,sizeof(BEACON),0);CHECK(radio->nap==1);
 for(unsigned i=0;i<11;i++){now+=300;radio->nd.poll(&radio->nd);}
 CHECK(!wifi_connect(radio,(const uint8_t*)"TestNet",7,"12345678",0));
 inject(AUTH,sizeof(AUTH),0);inject(ASSOC,sizeof(ASSOC),0);CHECK(radio->state==WIFI_KEYS);
 inject(M1,sizeof(M1),0);expect_eapol(M2_EXPECTED,sizeof(M2_EXPECTED));
 inject(M3,sizeof(M3),0);expect_eapol(M4_EXPECTED,sizeof(M4_EXPECTED));
 CHECK(radio->state==WIFI_UP);
 uint8_t eth[60]={0};memset(eth,255,6);memcpy(eth+6,radio->nd.mac,6);eth[12]=8;
 CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK((txcopy[1]&0x40)&&!memcmp(txcopy+32,TX_CIPHER,sizeof(TX_CIPHER)));
 inject(UNICAST8,sizeof(UNICAST8),0);CHECK(received==1);
 inject(UNICAST9,sizeof(UNICAST9),0);CHECK(received==2);
 inject(GROUP1,sizeof(GROUP1),0);inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(UNICAST8,sizeof(UNICAST8),AR_RXS8_CRC_ERR);CHECK(received==3);
 CHECK(ath_channel(radio,0)<0&&ath_channel(radio,12)<0);

 for(int bin=-900;bin<=900;bin+=100){
  ar5008_set_viterbi_mask(&c->hal,bin);
  CHECK(!(regs[0][AR_PHY_MASK_CTL/4]&(1u<<30)));
 }
 /* Repeated valid packets exercise hardware descriptor ownership and wrap. */
 for(unsigned i=0;i<100;i++)CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK(c->txprod<ATH_TXN);
 /* The hardware may still own a submitted descriptor: never overwrite it. */
 pending_tx=1;CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 uint8_t saved[ATH_BUFSZ];memcpy(saved,c->txbuf[c->txclean],sizeof(saved));
 for(unsigned slot=1;slot<ATH_TXN;slot++)CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK(radio->nd.send(&radio->nd,eth,sizeof(eth))<0);
 CHECK(!memcmp(saved,c->txbuf[c->txclean],sizeof(saved)));
 CHECK(c->txused==ATH_TXN);
 pending_tx=0;c->tx[c->txclean].ds_status1=AR_TXS1_FRM_XMIT_OK;
 c->tx[c->txclean].ds_status9=AR_TXS9_DONE;regs[0][AR_Q_TXE/4]=0;
 for(unsigned i=0;i<ATH_TXN;i++)ath_poll(radio);
 CHECK(!c->txused&&!c->txactive&&c->txclean==c->txprod);
 pending_tx=1;CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 now+=2100;ath_poll(radio);CHECK(!c->hal.ok&&!(commands[0]&4)&&radio->state==WIFI_DOWN&&!radio->pair.installed);
 #define BAD(flag) do{init();flag=1;CHECK(ath_attach(&pd)<0&&!enabled&&!registered&&!(commands[0]&4));}while(0)
 BAD(reset_stuck);BAD(rtc_stuck);BAD(rom_stuck);BAD(cal_stuck);
 init();register_full=1;CHECK(ath_attach(&pd)<0&&!(commands[0]&4));
 init();rom[0]=0;CHECK(ath_attach(&pd)<0&&!registered);
 init();rom[AR5416_EEP_START_LOC+1]^=1;CHECK(ath_attach(&pd)<0&&!registered);
 init();regs[0][AR_SREV/4]=(AR_SREV_VERSION_9280<<12)|255;CHECK(ath_attach(&pd)<0&&!registered&&!writes);
 init();pd.device=0x0030;CHECK(ath_attach(&pd)<0&&!nalloc);pd.device=0x002a;
 init();struct ar5416_eeprom*invalid=(void*)(rom+AR5416_EEP_START_LOC);
 invalid->baseEepHeader.rxMask=2;rom_checksum();CHECK(ath_attach(&pd)<0&&!registered);
 /* Checksum-correct EEPROM corruption must not escape software validation. */
 uint32_t rng=0x928011dd;
 for(unsigned trial=0;trial<1000;trial++){
  init();
  uint8_t*bytes=(void*)(rom+AR5416_EEP_START_LOC);
  for(unsigned j=0;j<1+trial%8;j++){
   rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
   bytes[rng%sizeof(struct ar5416_eeprom)]^=rng>>24;
  }
  rom_checksum();
  int rc=ath_attach(&pd);
  if(rc==0)CHECK(ath_cards[0]->hal.ok&&ath_rom_valid(&ath_cards[0]->hal));
  else CHECK(!registered&&!(commands[0]&4));
 }

 /* Signed offsets on both sides of the default exercise clamped PDADC shifts. */
 for(int offset=-10;offset<=0;offset++){
  init();struct ar5416_eeprom*e=(void*)(rom+AR5416_EEP_START_LOC);
  e->baseEepHeader.pwrTableOffset=(uint8_t)offset;rom_checksum();
  CHECK(!ath_attach(&pd));
  const unsigned powers[]={AR_PHY_POWER_TX_RATE1,AR_PHY_POWER_TX_RATE2,AR_PHY_POWER_TX_RATE3,AR_PHY_POWER_TX_RATE4};
  for(unsigned reg=0;reg<4;reg++)for(unsigned byte=0;byte<4;byte++)
   CHECK(((regs[0][powers[reg]/4]>>(byte*8))&63)<=(unsigned)(14-2*offset));
 }
 init();struct ar5416_eeprom*e=(void*)(rom+AR5416_EEP_START_LOC);
 e->baseEepHeader.openLoopPwrCntl=1;
 for(unsigned chain=0;chain<2;chain++){
  struct ar_cal_data_per_freq_olpc*data=(void*)e->calPierData2G[chain];
  for(unsigned pier=0;pier<AR5416_NUM_2G_CAL_PIERS;pier++){
   memset(&data[pier],0,sizeof(data[pier]));data[pier].pwrPdg[0][0]=40;
   data[pier].vpdPdg[0][0]=180;data[pier].pcdac[0][0]=30;
  }
 }
 rom_checksum();CHECK(!ath_attach(&pd));c=radio->hw;
 regs[0][AR_PHY_TX_PWRCTRL4/4]=220u<<1;now+=31000;ath_poll(radio);CHECK(c->hal.ok&&c->hal.pdadc==180);
 now+=500;ath_poll(radio);CHECK(c->hal.ok);
 init();regs[0][AR_SREV/4]=(AR_SREV_VERSION_9280<<12)|(2u<<8)|255;CHECK(!ath_attach(&pd));
 init();regs[0][AR_SREV/4]=(AR_SREV_VERSION_9280<<12)|(3u<<8)|255;CHECK(ath_attach(&pd)<0&&!registered);
  /* Ring/length/fragment fuzz runs the native receive path with fixed buffers. */
 init();CHECK(!ath_attach(&pd));c=radio->hw;
 for(unsigned trial=0;trial<20000;trial++){
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  unsigned i=c->rxcur;
  memset(c->rxbuf[i],0,ATH_BUFSZ);c->rxbuf[i][0]=8;
  c->rx[i].ds_status1=rng&0x1fff;c->rx[i].ds_status8=(rng>>16)|AR_RXS8_DONE;
  c->rx[i].ds_status4=0xff000000u;
  ath_poll(radio);CHECK(c->hal.ok);
 }
 /* Recover a missing status-DONE bit when a later descriptor completed. */
 unsigned at=c->rxcur,next=(at+1)%ATH_RXN;
 c->rx[at].ds_status8=0;c->rx[next].ds_status8=AR_RXS8_DONE;c->rx[next].ds_status1=0;
 ath_poll(radio);CHECK(c->rxcur==(next+1)%ATH_RXN&&c->hal.ok);
 /* Periodic IQ/noise-floor calibrations remain bounded. */
 now+=31000;ath_poll(radio);CHECK(c->hal.ok);
 now+=500;ath_poll(radio);CHECK(c->hal.ok&&!c->hal.cur_calib_mask);
 now+=250000;ath_poll(radio);CHECK(c->hal.ok);
 now+=500;ath_poll(radio);CHECK(c->hal.ok);
 init();ath9280_probe_all();CHECK(registered==2&&enabled==2);
 ath9280_probe_all();CHECK(registered==2&&enabled==2);
 ath_cards[0]->radio.state=ath_cards[1]->radio.state=WIFI_UP;
 ath_cards[0]->radio.pair.key[0]=0x42;ath_cards[1]->radio.pair.key[0]=0x43;
 ath_stop(ath_cards[0]);
 CHECK(ath_cards[0]->radio.state==WIFI_DOWN&&!ath_cards[0]->radio.pair.key[0]);
 CHECK(ath_cards[1]->hal.ok&&ath_cards[1]->radio.state==WIFI_UP&&ath_cards[1]->radio.pair.key[0]==0x43&&(commands[1]&4));
 init();printf("AR9280 native: %u checks passed\n",checks);return 0;
}
