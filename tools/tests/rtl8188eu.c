/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wifi.h"
#include "../../usb/usbcore.h"
#include "kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void* calloc(size_t,size_t);
extern void free(void*);
static unsigned checks,nalloc,registered,opened,received,fw_bytes,txlen,zlp_count;
static void* allocs[512];
static uint32_t now;
#define sizeof_payload 13872u
static uint8_t regs[65536],otp[256],txcopy[4096];
static uint32_t rf[256];
static wifi_radio_t*radio;
static usb_device_t*dev;
static int bad_read,bad_write,bad_ready,stuck_efuse,stuck_llt,bad_iq,ep_fail,submit_fail,bad_checksum;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();}}while(0)
#include "station_vectors.h"
void* kmalloc(size_t n){void*p=calloc(1,n);CHECK(nalloc<512);if(p)allocs[nalloc++]=p;return p;}
void* kzalloc(size_t n){return kmalloc(n);}
void kfree(void*p){for(unsigned i=0;i<nalloc;i++)if(allocs[i]==p){free(p);allocs[i]=allocs[--nalloc];return;}}
void* usb_dma_alloc(uint32_t n){return kmalloc(n);}
void klog(const char*f,...){(void)f;}
uint32_t timer_ms(void){return now;}
void usb_delay_ms(uint32_t n){now+=n;}
void usb_wait(void){now++;}
void usb_poll(void){}
void random_bytes(void*p,uint32_t n){for(uint32_t i=0;i<n;i++)((uint8_t*)p)[i]=64+i;}
void net_irq_notify(void){}
void net_register_device(netdev_t*nd){registered++;nd->present=1;radio=nd->priv;}
void net_unregister_device(netdev_t*nd){nd->present=0;}
void net_rx(netdev_t*nd,const uint8_t*b,uint32_t n){CHECK(nd==&radio->nd&&n<=1514&&b);received++;}
int usb_open_endpoint(usb_device_t*d,const usb_ep_desc_t*ep){CHECK(d==dev&&(ep->bEndpointAddress&15));opened++;return ep_fail?-1:0;}
int usb_walk_config(usb_device_t*d,int cfg,int(*cb)(const uint8_t*,void*),void*ctx){
 const uint8_t*b=d->configs[cfg];if(!b)return -1;unsigned n=b[2]|(unsigned)b[3]<<8;
 for(unsigned off=0;off<n;){if(n-off<2||b[off]<2||b[off]>n-off)return -1;if(cb(b+off,ctx))return 0;off+=b[off];}return 0;
}
#include "../../net/rtl8188eu.c"
#undef printf
extern const uint8_t eu_fw_start[],eu_fw_end[];
static uint32_t reg32(unsigned a){return eu32(regs+a);}
static void put32(unsigned a,uint32_t v){eup32(regs+a,v);}
int usb_control(usb_device_t*d,uint8_t type,uint8_t request,uint16_t a,uint16_t index,void*b,uint16_t n){
 CHECK(d==dev&&request==5&&!index&&(unsigned)a+n<=65536);
 if(type&USB_DIR_IN){
  if(bad_read)return n?n-1:-1;
  if(a==R92C_MCUFWDL&&n>=1){
   uint32_t v=reg32(a);
   if(fw_bytes==sizeof_payload&&!bad_checksum)v|=R92C_MCUFWDL_CHKSUM_RPT;
   if((v&R92C_MCUFWDL_RDY)&&fw_bytes==sizeof_payload&&!bad_ready)v|=R92C_MCUFWDL_WINTINI_RDY;
   put32(a,v);
  }
  memcpy(b,regs+a,n);return n;
 }
 if(bad_write)return -1;
 if(a>=R92C_FW_START_ADDR&&a<R92C_FW_START_ADDR+4096){
  unsigned page=MS(reg32(R92C_MCUFWDL),R92C_MCUFWDL_PAGE);
  unsigned off=page*4096+a-R92C_FW_START_ADDR;
  CHECK(off==fw_bytes&&off+n<=sizeof_payload&&!memcmp(b,eu_fw_start+32+off,n));fw_bytes+=n;
  return n;
 }
 memcpy(regs+a,b,n);
 if(a==R92C_MCUFWDL)regs[a]&=~R92C_MCUFWDL_CHKSUM_RPT;
 if(a==R92C_EFUSE_CTRL&&n==4&&!stuck_efuse){
  uint32_t v=reg32(a);unsigned addr=MS(v,R92C_EFUSE_CTRL_ADDR);CHECK(addr<256);
  put32(a,(v&~R92C_EFUSE_CTRL_DATA_M)|otp[addr]|R92C_EFUSE_CTRL_VALID);
 }
 if(a==R92C_LLT_INIT&&n==4&&!stuck_llt)put32(a,reg32(a)&~R92C_LLT_INIT_OP_M);
 if(a==R92C_APS_FSMCO&&n==2)eup16(regs+a,eu16(regs+a)&~R92C_APS_FSMCO_APFM_ONMAC);
 if(a==R92C_LSSI_PARAM(0)&&n==4){
  uint32_t v=reg32(a);unsigned addr=MS(v,R88E_LSSI_PARAM_ADDR);rf[addr]=MS(v,R92C_LSSI_PARAM_DATA);
 }
 if(a==R92C_HSSI_PARAM2(0)&&n==4){
  unsigned addr=MS(reg32(a),R92C_HSSI_PARAM2_READ_ADDR);
  put32(R92C_LSSI_READBACK(0),rf[addr]);put32(R92C_HSPI_READBACK(0),rf[addr]);
 }
 if(a==R92C_IQK_AGC_PTS&&n==4){
  put32(R92C_TX_POWER_BEFORE_IQK_A,0x100u<<16);put32(R92C_TX_POWER_AFTER_IQK_A,0);
  put32(R92C_RX_POWER_BEFORE_IQK_A_2,0x100u<<16);
  put32(0xeac,bad_iq?(1u<<28):0);
 }
 return n;
}
int usb_submit(usb_xfer_t*x){
 CHECK(x->dev==dev&&x->buf);
 if(submit_fail)return -1;
 if(x->ep&0x80){x->status=USB_XFER_PENDING;return 0;}
 if(x->len){
  CHECK(x->len<=sizeof(txcopy));memcpy(txcopy,x->buf,x->len);txlen=x->len;
  uint16_t sum=0;for(unsigned i=0;i<32;i+=2)sum^=eu16(txcopy+i);CHECK(!sum);
  CHECK(!(eu32(txcopy+16)&R92C_TXDW4_HWSEQ));
  CHECK(!MS(eu32(txcopy+4),R92C_TXDW1_CIPHER));
 }
 if(!x->len)zlp_count++;
 x->actual=x->len;x->status=USB_XFER_OK;return 0;
}
static void init(usb_device_t*d){
 while(nalloc)free(allocs[--nalloc]);
 memset(d,0,sizeof(*d));memset(regs,0,sizeof(regs));memset(rf,0,sizeof(rf));memset(otp,255,sizeof(otp));
 registered=opened=received=fw_bytes=txlen=zlp_count=now=0;radio=NULL;dev=d;
 bad_read=bad_write=bad_ready=stuck_efuse=stuck_llt=bad_iq=ep_fail=submit_fail=bad_checksum=0;
 static uint8_t cfg[]={9,2,39,0,1,1,0,0x80,50,9,4,0,0,3,255,255,255,0,
  7,5,0x81,2,0,2,0,7,5,2,2,0,2,0,7,5,3,2,0,2,0};
 d->present=1;d->speed=USB_SPEED_HIGH;d->configs[0]=cfg;
 d->desc.idVendor=0x0bda;d->desc.idProduct=0x8179;
 put32(R92C_APS_FSMCO,R92C_APS_FSMCO_SUS_HOST);
 struct r88e_rom rom;memset(&rom,255,sizeof(rom));
 rom.id=0x8129;rom.xtal=0x20;rom.rf_board_opt=0;rom.r88eu_rom.vid=0x0bda;rom.r88eu_rom.pid=0x8179;
 const uint8_t mac[]={2,0,0,0,0,1};memcpy(rom.r88eu_rom.macaddr,mac,6);
 memset(&rom.txpwr,0,sizeof(rom.txpwr));memset(rom.txpwr.cck_tx_pwr,20,6);memset(rom.txpwr.ht40_tx_pwr,18,5);
 const uint8_t*b=(const uint8_t*)&rom;unsigned pos=0;
 for(unsigned off=0;off<sizeof(rom)/8;off++){
  unsigned mask=15;for(unsigned i=0;i<4;i++)if(b[off*8+i*2]!=255||b[off*8+i*2+1]!=255)mask&=~(1u<<i);
  if(mask==15)continue;
  CHECK(pos+10<256);
  if(off<16)otp[pos++]=(off<<4)|mask;
  else{otp[pos++]=((off&7)<<5)|15;otp[pos++]=((off&0x78)<<1)|mask;}
  for(unsigned i=0;i<4;i++)if(!(mask&(1u<<i))){otp[pos++]=b[off*8+i*2];otp[pos++]=b[off*8+i*2+1];}
 }
}
static void inject(const uint8_t*frame,unsigned n,uint32_t flags){
 struct rtwn_softc*s=radio->hw;usb_xfer_t*x=&s->rx[0];uint8_t*b=x->buf;
 CHECK(x->status==USB_XFER_PENDING);memset(b,0,24+n);
 eup32(b,n|flags);eup32(b+8,1u<<16);memcpy(b+24,frame,n);
 x->actual=24+n;x->status=USB_XFER_OK;radio->nd.poll(&radio->nd);
}
static void expect_eapol(const uint8_t*b,unsigned n){CHECK(txlen>=64+n&&!memcmp(txcopy+64,b,n));}
int main(void){
 usb_device_t d;init(&d);
 CHECK(eu_probe(&d)==0);
 CHECK(eu_attach(&d)==0&&registered==1&&opened==3);
 printf("attached, fw=%u bytes\n",fw_bytes);fflush(0);
 CHECK(fw_bytes==sizeof_payload);
 CHECK(eu16(regs+R92C_SECCFG)==0&&regs[R92C_HWSEQ_CTRL]==0);
 CHECK(!radio->nd.link_up(&radio->nd));
 CHECK(eu_channel(radio,0)<0&&eu_channel(radio,12)<0&&eu_channel(radio,11)==0);
 CHECK(wifi_scan(radio)==0);inject(BEACON,sizeof(BEACON),0);CHECK(radio->nap==1);
 for(int i=0;i<11;i++){now+=300;radio->nd.poll(&radio->nd);}
 CHECK(wifi_connect(radio,(const uint8_t*)"TestNet",7,"12345678",0)==0);
 inject(AUTH,sizeof(AUTH),0);inject(ASSOC,sizeof(ASSOC),0);CHECK(radio->state==WIFI_KEYS);
 inject(M1,sizeof(M1),0);expect_eapol(M2_EXPECTED,sizeof(M2_EXPECTED));
 inject(M3,sizeof(M3),0);expect_eapol(M4_EXPECTED,sizeof(M4_EXPECTED));
 CHECK(radio->state==WIFI_UP&&radio->nd.link_up(&radio->nd));
 uint8_t eth[60]={0};memset(eth,255,6);memcpy(eth+6,radio->nd.mac,6);eth[12]=8;
 CHECK(radio->nd.send(&radio->nd,eth,sizeof(eth))==0);
 CHECK((txcopy[33]&0x40)&&!memcmp(txcopy+64,TX_CIPHER,sizeof(TX_CIPHER)));
 inject(UNICAST8,sizeof(UNICAST8),0);CHECK(received==1);
 inject(UNICAST9,sizeof(UNICAST9),0);CHECK(received==2);
 inject(GROUP1,sizeof(GROUP1),0);inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(UNICAST8,sizeof(UNICAST8),R92C_RXDW0_CRCERR);CHECK(received==3);

 /* Exact-size receive buffers, descriptor/aggregate mutations. */
 struct rtwn_softc*sc=radio->hw;
 uint8_t seed[160]={0};CHECK(24+sizeof(UNICAST9)<sizeof(seed));
 eup32(seed,sizeof(UNICAST9));eup32(seed+8,1u<<16);memcpy(seed+24,UNICAST9,sizeof(UNICAST9));
 uint32_t rng=0x188e9911;
 for(unsigned i=0;i<12000;i++){
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  unsigned n=i%2?24+sizeof(UNICAST9):rng%(25+sizeof(UNICAST9));
  uint8_t*b=calloc(1,n?n:1);CHECK(b);memcpy(b,seed,n);
  if(n)b[rng%n]^=rng>>16;
  eu_receive(sc,b,n);free(b);
 }
 /* RX shift/info spans, TX reports and a malformed second aggregate member. */
 uint8_t aggregate[256]={0};
 eup32(aggregate,24);eup32(aggregate+8,2u<<16);
 eup32(aggregate+128,8192);CHECK(eu_receive(sc,aggregate,sizeof(aggregate))<0);
 for(unsigned n=0;n<64;n++){
  uint8_t*b=calloc(1,n?n:1);CHECK(b);
  if(n>=24){eup32(b,n-24);eup32(b+12,1u<<14);}
  eu_receive(sc,b,n);free(b);
 }
 /* Ring backpressure must not reuse controller-owned TX or ZLP storage. */
 for(unsigned i=0;i<8;i++)sc->tx[i].status=USB_XFER_PENDING;
 CHECK(eu_send(radio,AUTH,sizeof(AUTH))<0);
 for(unsigned i=0;i<8;i++){sc->tx[i].status=USB_XFER_OK;sc->zlp[i].status=USB_XFER_PENDING;}
 CHECK(eu_send(radio,AUTH,sizeof(AUTH))<0);
 eu_detach(&d);CHECK(!radio->nd.present);
 CHECK(eu_send(radio,AUTH,sizeof(AUTH))<0);

 #define FAIL_ATTACH(flag) do{init(&d);flag=1;CHECK(eu_attach(&d)<0&&!registered);}while(0)
 FAIL_ATTACH(bad_read);FAIL_ATTACH(bad_write);FAIL_ATTACH(bad_ready);FAIL_ATTACH(stuck_efuse);
 FAIL_ATTACH(bad_checksum);FAIL_ATTACH(stuck_llt);FAIL_ATTACH(bad_iq);FAIL_ATTACH(ep_fail);FAIL_ATTACH(submit_fail);

 /* Corrupt extended OTP headers must not read beyond the physical stream or
  * overwrite the logical map. Neither case publishes a radio. */
 for(unsigned i=0;i<1000;i++){
  init(&d);uint32_t rng=i+1;
  for(unsigned j=0;j<256;j++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;otp[j]=rng;}
  CHECK(eu_attach(&d)<0&&!registered);
 }

 /* Never claim the physically-tested Ethernet dongle or unknown Wi-Fi IDs. */
 init(&d);d.desc.idProduct=0x8152;CHECK(eu_probe(&d)<0);
 init(&d);d.desc.idProduct=0x8178;CHECK(eu_probe(&d)<0);
 init(&d);
 uint8_t *cfg=d.configs[0];
 uint8_t saved=cfg[20];cfg[20]=0x80;CHECK(eu_probe(&d)<0);cfg[20]=saved;
 saved=cfg[27];cfg[27]=cfg[34];CHECK(eu_probe(&d)<0);cfg[27]=saved;
 saved=cfg[22];cfg[22]=127;CHECK(eu_probe(&d)<0);cfg[22]=saved;
 d.speed=USB_SPEED_LOW;CHECK(eu_probe(&d)<0);
 d.speed=USB_SPEED_FULL;CHECK(eu_probe(&d)<0); /* 512-byte packets illegal at FS. */
 d.speed=USB_SPEED_HIGH;
 /* Real descriptor variants: one/three bulk-OUT queues and USB full speed. */
 for(unsigned queues=1;queues<=3;queues++){
  init(&d);
  static uint8_t variant[]={9,2,0,0,1,1,0,0x80,50,9,4,0,0,0,255,255,255,0,
   7,5,0x81,2,0,2,0,7,5,2,2,0,2,0,7,5,3,2,0,2,0,7,5,4,2,0,2,0};
  variant[2]=18+7*(queues+1);variant[13]=queues+1;
  d.configs[0]=variant;
  for(unsigned i=0;i<queues+1;i++){variant[22+7*i]=0;variant[23+7*i]=2;}
  CHECK(eu_attach(&d)==0&&registered==1&&opened==queues+1);
  eu_detach(&d);
  init(&d);d.configs[0]=variant;d.speed=USB_SPEED_FULL;
  for(unsigned i=0;i<queues+1;i++){variant[22+7*i]=64;variant[23+7*i]=0;}
  CHECK(eu_attach(&d)==0&&registered==1);
  /* Explicit zero-length USB packet for a full-sized bulk transfer. */
  uint8_t frame[32]={0};frame[0]=0x08;
  CHECK(eu_send(radio,frame,sizeof(frame))==0&&txlen==64&&zlp_count==1);
  eu_detach(&d);
 }

 while(nalloc)free(allocs[--nalloc]);
 printf("RTL8188EU: %u checks passed (simulated USB/RF only)\n",checks);return 0;
}
