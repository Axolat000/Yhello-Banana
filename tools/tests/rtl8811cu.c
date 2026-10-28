/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wifi.h"
#include "usbcore.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void*calloc(size_t,size_t);
extern void free(void*);
#define CU_MODEL 1
#include "../../net/rtl8811cu.c"
#include "cu_test_firmware.h"
#include "station_vectors.h"
__asm__(".global cu_fw_start\n.set cu_fw_start,fw_bytes\n.global cu_fw_end\n.set cu_fw_end,fw_bytes+139472\n");
static unsigned checks,nalloc,registered,received,allocation_ops,fail_alloc,fail_open,opens;
static void*allocs[256];
static cu_native_t*current;
static u32 now;
static wifi_radio_t*radio;
#define CHECK(v) do{checks++;if(!(v)){printf("FAIL %d: %s\n",__LINE__,#v);fflush(0);abort();}}while(0)
void*kmalloc(size_t n){allocation_ops++;if(fail_alloc==allocation_ops)return NULL;void*p=calloc(1,n);CHECK(nalloc<256);if(p)allocs[nalloc++]=p;return p;}
void*kzalloc(size_t n){void*p=kmalloc(n);if(n==sizeof(cu_native_t))current=p;return p;}
void kfree(void*p){for(unsigned i=0;i<nalloc;i++)if(allocs[i]==p){free(p);allocs[i]=allocs[--nalloc];return;}}
void*usb_dma_alloc(u32 n){return kmalloc(n);}
void klog(const char*f,...){(void)f;}
u32 timer_ms(void){return now;}
void usb_delay_ms(u32 ms){now+=ms;}
void usb_wait(void){now++;}
void random_bytes(void*p,u32 n){for(u32 i=0;i<n;i++)((u8*)p)[i]=64+i;}
void net_irq_notify(void){}
void net_register_device(netdev_t*n){CHECK(n->irq==255&&n->is_usb);n->present=1;radio=n->priv;registered++;}
void net_unregister_device(netdev_t*n){n->present=0;}
void net_rx(netdev_t*n,const u8*b,u32 len){CHECK(n==&radio->nd&&b&&len>=14&&len<=1514);received++;}
static u8 regs[65536],physical[416],staged[4097],imem[0x40008],dmem[0x20008],emem[0x20008];
static unsigned staged_len,transfers,ops,fail_at,segment_copies;
static int stall,checksum_bad,ready_bad;
static u32 read32(unsigned a){return regs[a]|(u32)regs[a+1]<<8|(u32)regs[a+2]<<16|(u32)regs[a+3]<<24;}
static void put32(unsigned a,u32 v){for(unsigned i=0;i<4;i++)regs[a+i]=v>>(8*i);}
static int control(struct rtw_dev*d,u32 a,void*buf,unsigned n,int rd){
 (void)d;CHECK(a+n<=65536&&n&&n<=4);ops++;if(fail_at&&ops==fail_at)return -1;
 if(rd){memcpy(buf,regs+a,n);return 0;}
 u32 v=0;memcpy(&v,buf,n);memcpy(regs+a,buf,n);
 if(a==REG_SYS_PW_CTRL&&n==1)regs[a]&=~3u;
 if(a==REG_SYS_PW_CTRL+1&&n==1)regs[a]&=~3u;
 if(a==LTECOEX_ACCESS_CTRL&&n==4)put32(a,v|LTECOEX_READY);
 if(a==REG_EFUSE_CTRL&&n==4){
  unsigned at=FIELD_GET(BITS_EF_ADDR,v);CHECK(at<416&&!(v&BIT_EF_FLAG));
  put32(a,(v&~255u)|physical[at]|(stall?0:BIT_EF_FLAG));
 }
 if(a==REG_DDMA_CH0CTRL&&n==4&&(v&BIT_DDMACH0_OWN)){
  unsigned len=v&BIT_MASK_DDMACH0_DLEN,dst=read32(REG_DDMA_CH0DA),src=read32(REG_DDMA_CH0SA);
  CHECK(src==OCPBASE_TXBUF_88XX+48&&len<=staged_len);
  u8*target=0;unsigned cap=0,offset=0;
  if(dst>=0x200000&&dst<0x220008){target=dmem;cap=sizeof(dmem);offset=dst-0x200000;}
  else if(dst>=0x100000&&dst<0x120008){target=emem;cap=sizeof(emem);offset=dst-0x100000;}
  else if(dst>=0x30000&&dst<0x70008){target=imem;cap=sizeof(imem);offset=dst-0x30000;}
  CHECK(target&&offset+len<=cap);memcpy(target+offset,staged,len);segment_copies++;
  if(!stall)put32(a,(v&~BIT_DDMACH0_OWN)|(checksum_bad?BIT_DDMACH0_CHKSUM_STS:0));
 }
 if(a==REG_SYS_FUNC_EN+1&&(v&BIT_FEN_CPUEN)&&!ready_bad)
  put32(REG_MCUFW_CTRL,FW_READY);
 if(a==0xc90&&n==4){
  unsigned reg=(v>>20)&255;put32(0x2800+reg*4,v&0xfffff);
 }
 if(a==REG_AUTO_LLT_V1&&!stall)regs[a]&=~BIT_AUTO_INIT_LLT_V1;
 return 0;
}
static int old_tx(struct rtw_dev*d,const u8*b,unsigned n,unsigned qsel){
 CHECK(d->ok&&qsel==0x10&&n&&n<=4097);CHECK((n+48)%512);
 memcpy(staged,b,n);staged_len=n;transfers++;
 put32(REG_FIFOPAGE_CTRL_2,read32(REG_FIFOPAGE_CTRL_2)|BIT_BCN_VALID_V1);
 return 0;
}
static void wait_us(struct rtw_dev*d,unsigned us){(void)d;CHECK(us<=100000);}
static void reset(struct rtw_dev*d){
memset(regs,0,sizeof(regs));memset(physical,255,sizeof(physical));
 memset(imem,0,sizeof(imem));memset(dmem,0,sizeof(dmem));memset(emem,0,sizeof(emem));
 memset(d,0,sizeof(*d));d->ok=1;d->control=control;d->tx=old_tx;d->wait=wait_us;d->chip=&cu_chip;
 d->hci.type=RTW_HCI_TYPE_USB;d->hci.bulkout_num=3;d->hci.rpwm_addr=0xfe58;d->hal.cut_version=2;d->rfe=0;
 ops=transfers=staged_len=fail_at=segment_copies=0;stall=checksum_bad=ready_bad=0;
 put32(LTECOEX_ACCESS_CTRL,LTECOEX_READY);put32(REG_MCUFW_CTRL,0);
 regs[REG_CR]=0xea;regs[REG_SYS_PW_CTRL]=BIT(1); /* power-ready bit */
}

static unsigned submits,fail_submit,txlen;
static int pending_tx,pending_boot,iq_failed;
static u8 txcopy[4096];
int usb_control(usb_device_t*dev,u8 type,u8 request,u16 addr,u16 index,void*b,u16 n){
 CHECK(dev->present&&request==5&&index==0&&(type==0x40||type==0xc0));
 int rc=control(&current->hal,addr,b,n,!!(type&0x80));return rc<0?rc:n;
}
int usb_open_endpoint(usb_device_t*d,const usb_ep_desc_t*ep){CHECK(d->present&&ep);opens++;return fail_open==opens?-1:0;}
int usb_submit(usb_xfer_t*x){
 CHECK(x&&x->dev&&x->dev->present&&x->buf);submits++;
 if(fail_submit&&submits==fail_submit)return -1;
 x->actual=0;x->status=USB_XFER_PENDING;
 if(x->ep&0x80)return 0;
 if(!x->len){x->status=pending_tx?USB_XFER_PENDING:USB_XFER_OK;return 0;}
 const u8*b=x->buf;unsigned n=cu32(b)&65535,qsel=(cu32(b+4)>>8)&31;
 unsigned offset=qsel==0x13?48:(cu32(b)>>16)&255;
 CHECK(x->len==n+offset&&offset==48);
 CHECK(qsel==0x13?((cu32(b)>>16)&255)==0:1);
 u16 sum=0;for(unsigned i=0;i<32;i+=2)sum^=cu16(b+i);CHECK(!sum);
 if(qsel==0x10){
  CHECK(n<=4097);memcpy(staged,b+offset,n);staged_len=n;transfers++;
  put32(REG_FIFOPAGE_CTRL_2,read32(REG_FIFOPAGE_CTRL_2)|BIT_BCN_VALID_V1);
  if(pending_boot)return 0;
 }else if(qsel==0x13){
  CHECK(n==32&&b[offset]==1&&b[offset+1]==255);
  unsigned sub=cu16(b+offset+2);
  CHECK(sub==0x0d||sub==0x11||sub==0x0e);
  if(sub==0x0e){
   CHECK(b[offset+8]==1);
   put32(0x2800+8*4,0xabcde);put32(REG_IQKFAILMSK,iq_failed?1:0);
  }
 }else{
  CHECK(qsel==0x12&&n>=24&&n<=WIFI_FRAME_MAX);
  CHECK((cu32(b+12)&(BIT(8)|BIT(10)))==(BIT(8)|BIT(10)));
  CHECK(!cu32(b+8)&&!cu32(b+20)&&!cu32(b+32));
  CHECK(((cu32(b+36)>>12)&4095)==(cu16(b+offset+22)>>4));
  txlen=n;memcpy(txcopy,b+offset,n);if(pending_tx)return 0;
 }
 x->actual=x->len;x->status=USB_XFER_OK;return 0;
}
static const u8 config[]={
 9,2,39,0,1,1,0,0x80,50,
 9,4,0,0,3,255,255,255,0,
 7,5,0x81,2,0,2,0,
 7,5,0x02,2,0,2,0,
 7,5,0x03,2,0,2,0,
};
static usb_device_t device;
int usb_walk_config(usb_device_t*dev,int cfg,int(*cb)(const u8*,void*),void*ctx){
 if(cfg!=0||!dev->configs[cfg])return -1;
 const u8*p=dev->configs[cfg];unsigned size=cu16(p+2);
 for(unsigned at=0;at<size;){
  if(size-at<2||p[at]<2||p[at]>size-at)return -1;
  int rc=cb(p+at,ctx);if(rc)return rc;at+=p[at];
 }
 return 0;
}
static void physical_encode(const u8 logical[512]){
 memset(physical,255,416);unsigned at=0;
 for(unsigned block=0;block<64;block++){
  unsigned mask=15;
  for(unsigned j=0;j<4;j++)if(logical[block*8+j*2]!=255||logical[block*8+j*2+1]!=255)mask&=~BIT(j);
  if(mask==15)continue;
  if(block<16)physical[at++]=(block<<4)|mask;
  else{physical[at++]=((block&7)<<5)|15;physical[at++]=((block&0x78)<<1)|mask;}
  for(unsigned j=0;j<4;j++)if(!(mask&BIT(j))){
   physical[at++]=logical[block*8+j*2];physical[at++]=logical[block*8+j*2+1];CHECK(at<416);
  }
 }
}
static void init(void){
 while(nalloc)free(allocs[--nalloc]);
 struct rtw_dev dummy;reset(&dummy);cu_count=0;now=received=registered=0;radio=0;current=0;
 submits=fail_submit=txlen=allocation_ops=fail_alloc=fail_open=opens=0;pending_tx=pending_boot=iq_failed=0;
 memset(&device,0,sizeof(device));device.present=1;device.speed=USB_SPEED_HIGH;device.active_config=0;
 device.desc.idVendor=0x0bda;device.desc.idProduct=0xc811;device.configs[0]=(u8*)config;
 u8 logical[512];memset(logical,255,512);logical[0]=0x29;logical[1]=0x81;
 for(unsigned i=0;i<11;i++)logical[0x10+i]=40;
 logical[0xb9]=32;logical[0xbc]=logical[0xbd]=logical[0xc1]=logical[0xc2]=0;
 logical[0xca]=0;logical[0x107]=2;for(unsigned i=1;i<6;i++)logical[0x107+i]=i==5?1:0;
 physical_encode(logical);
 put32(REG_SYS_CFG1,2u<<12);
 /* Emulate reset power-ready status and automatic state-machine handshakes. */
 regs[REG_SYS_PW_CTRL+2]=BIT(1);
}
static void inject(const u8*f,unsigned n,unsigned flags){
 CHECK(current&&radio&&current->rx[0].status==USB_XFER_PENDING);
 usb_xfer_t*x=&current->rx[0];CHECK(n+24<=16384);
 memset(x->buf,0,n+24);cup32(x->buf,n|flags);memcpy(x->buf+24,f,n);
 x->actual=n+24;x->status=USB_XFER_OK;radio->nd.poll(&radio->nd);
}
static void expect_eapol(const u8*b,unsigned n){CHECK(txlen>=32+n&&!memcmp(txcopy+32,b,n));}
int main(void){
 init();CHECK(cu_probe(&device)==0);
 int rc=cu_attach(&device);
 if(rc)printf("attach failed ok=%d ops=%u submits=%u now=%u MCU=%x SYS=%x PHY=%x\n",current?current->hal.ok:-1,ops,submits,now,read32(REG_MCUFW_CTRL),read32(REG_SYS_PW_CTRL),read32(REG_IQKFAILMSK));
 CHECK(!rc&&registered==1);
 CHECK(current->hal.ok&&device.drvdata==current&&current->channel==1);
 CHECK(!wifi_scan(radio));inject(BEACON,sizeof(BEACON),0);CHECK(radio->nap==1);
 for(unsigned i=0;i<11;i++){now+=300;radio->nd.poll(&radio->nd);}
 CHECK(!wifi_connect(radio,(const u8*)"TestNet",7,"12345678",0));
 inject(AUTH,sizeof(AUTH),0);inject(ASSOC,sizeof(ASSOC),0);CHECK(radio->state==WIFI_KEYS);
 inject(M1,sizeof(M1),0);expect_eapol(M2_EXPECTED,sizeof(M2_EXPECTED));
 inject(M3,sizeof(M3),0);expect_eapol(M4_EXPECTED,sizeof(M4_EXPECTED));CHECK(radio->state==WIFI_UP);
 u8 eth[60]={0};memset(eth,255,6);memcpy(eth+6,radio->nd.mac,6);eth[12]=8;
 CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 CHECK((txcopy[1]&0x40)&&!memcmp(txcopy+32,TX_CIPHER,sizeof(TX_CIPHER)));
 inject(UNICAST8,sizeof(UNICAST8),0);CHECK(received==1);
 inject(UNICAST9,sizeof(UNICAST9),0);CHECK(received==2);
 inject(GROUP1,sizeof(GROUP1),0);inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(MULTICAST2,sizeof(MULTICAST2),0);CHECK(received==3);
 inject(UNICAST8,sizeof(UNICAST8),BIT(14));CHECK(received==3);


 /* Every frame length, including exact USB packet boundaries, keeps byte identity. */
 u8 frame[WIFI_FRAME_MAX];memset(frame,0,sizeof(frame));frame[0]=8;frame[4]=2;
 for(unsigned n=24;n<=WIFI_FRAME_MAX;n++){
  frame[22]=n&255;frame[23]=n>>8;
  CHECK(!cu_send(radio,frame,n)&&txlen==n&&!memcmp(frame,txcopy,n));
  cu_poll(radio);CHECK(current->hal.ok);
 }

 /* An OFDM-only AP cannot receive a hardcoded 1-Mbit/s management frame. */
 radio->ap.nrates=1;radio->ap.rates[0]=0x8c;
 CHECK(!cu_station(radio,radio->ap.bssid,1));
 CHECK((read32(REG_RRSR)&0xfff)==BIT(4));
 CHECK(!cu_send(radio,frame,60));CHECK(cu32(current->tx[0].buf+16)==4);
 cu_poll(radio);radio->ap.rates[0]=0x82;
 /* The host controller owns pending URBs; a full queue must preserve all bytes. */
 pending_tx=1;
 for(unsigned i=0;i<8;i++)CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 u8 saved[2112];memcpy(saved,current->tx[0].buf,sizeof(saved));
 CHECK(radio->nd.send(&radio->nd,eth,sizeof(eth))<0);
 CHECK(!memcmp(saved,current->tx[0].buf,sizeof(saved)));
 for(unsigned i=0;i<8;i++){current->tx[i].status=USB_XFER_OK;current->tx[i].actual=current->tx[i].len;}
 pending_tx=0;cu_poll(radio);CHECK(current->hal.ok);
 pending_tx=1;CHECK(!radio->nd.send(&radio->nd,eth,sizeof(eth)));
 memcpy(saved,current->tx[0].buf,sizeof(saved));
 now+=2100;cu_poll(radio);CHECK(!current->hal.ok&&radio->state==WIFI_DOWN&&!radio->pair.installed);
 CHECK(!memcmp(saved,current->tx[0].buf,sizeof(saved))&&current->tx[0].status==USB_XFER_PENDING);
 /* Enumerating unrelated or unsupported USB/silicon/board profiles must decline. */
 init();device.desc.idProduct=0x8179;CHECK(cu_probe(&device)<0&&!nalloc);
 init();device.speed=USB_SPEED_FULL;CHECK(cu_probe(&device)<0&&!nalloc);
 init();put32(REG_SYS_CFG1,BIT_RTL_ID|(2u<<12));CHECK(cu_attach(&device)<0&&!registered);
 init();put32(REG_SYS_CFG1,BIT_RF_TYPE_ID|(2u<<12));CHECK(cu_attach(&device)<0&&!registered);
 for(unsigned cut=0;cut<=4;cut++){
  init();put32(REG_SYS_CFG1,cut<<12);int r=cu_attach(&device);
  CHECK((cut==2||cut==3)?r==0:r<0);
 }
 init();physical[0]=0xff;CHECK(cu_attach(&device)<0&&!registered);
 init();iq_failed=1;CHECK(cu_attach(&device)<0&&!registered);
 /* Completion can arrive after an initialization timeout. Context is still alive. */
 init();pending_boot=1;CHECK(cu_attach(&device)<0&&!registered);
 CHECK(current&&current->queued&&current->boot.status==USB_XFER_PENDING);
 memset(current->boot.buf,0x5a,current->boot.len);current->boot.status=USB_XFER_GONE;
 /* Error at each firmware/packet submission, endpoint open and allocation. */
 init();CHECK(!cu_attach(&device));unsigned total_submits=submits,total_alloc=allocation_ops,total_opens=opens,total_ops=ops;
 for(unsigned at=1;at<=total_submits;at++){
  init();fail_submit=at;CHECK(cu_attach(&device)<0&&!registered&&!device.drvdata);
 }
 for(unsigned at=1;at<=total_opens;at++){
  init();fail_open=at;CHECK(cu_attach(&device)<0&&!registered);
 }
 for(unsigned at=1;at<=total_alloc;at++){
  init();fail_alloc=at;CHECK(cu_attach(&device)<0&&!registered);
 }
 /* Sample every control phase densely, plus all initial firmware-stage controls. */
 for(unsigned at=1;at<=total_ops;at+=at<400?1:17){
  init();fail_at=at;CHECK(cu_attach(&device)<0&&!registered&&!device.drvdata);
 }
 init();CHECK(!cu_attach(&device));
 /* Bad aggregate tail must prevent the valid first frame from being delivered. */
 u8 aggregate[256]={0};cup32(aggregate,sizeof(BEACON));memcpy(aggregate+24,BEACON,sizeof(BEACON));
 unsigned tail=(24+sizeof(BEACON)+7)&~7u;cup32(aggregate+tail,0x3fff);
 int before=radio->nap;
 CHECK(cu_rx(current,aggregate,tail+24)<0&&radio->nap==before);
 u32 rng=0x8811c811;
 for(unsigned trial=0;trial<20000;trial++){
  unsigned length=trial%256;
  for(unsigned j=0;j<length;j++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;aggregate[j]=rng>>24;}
  (void)cu_rx(current,aggregate,length);
  CHECK(current->hal.ok);
 }
 radio->state=WIFI_UP;radio->pair.key[0]=0x42;
 cu_detach(&device);CHECK(!device.drvdata&&current->detached&&!current->hal.ok&&!radio->pair.key[0]&&!radio->nd.present);
 /* Late RX completion remains safe after detach. */
 memset(current->rx[0].buf,0,16384);current->rx[0].status=USB_XFER_GONE;
 printf("RTL8811CU native station: %u checks passed\n",checks);init();return 0;
}
