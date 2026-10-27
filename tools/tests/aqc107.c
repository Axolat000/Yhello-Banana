/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdarg.h>
#include "netdev.h"
#include "pci.h"
#include "kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern int posix_memalign(void**,size_t,size_t);
extern void free(void*);
static unsigned checks,allocn,registered,received,enabled;
static void*allocs[64];
static uint32_t now;
static uint16_t commands[2];
static uint8_t pcfg[256];
static uint16_t pm_written;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();}}while(0)
#include "../../net/aqc107.c"
extern int vsnprintf(char*,size_t,const char*,va_list);
int ksnprintf(char*b,size_t n,const char*f,...){va_list a;va_start(a,f);int k=vsnprintf(b,n,f,a);va_end(a);return k;}
static uint8_t regs[65536] __attribute__((aligned(4096)));
static uint8_t regs1[65536] __attribute__((aligned(4096)));
static int irq_stuck,fw_stuck,sem_stuck,mbox_stuck,no_mac,register_full;
static uint32_t aq_mmio_read(struct aq_softc*s,uint32_t a){
 CHECK((s->mmio==regs||s->mmio==regs1)&&a<=65532&&!(a&3));return *(uint32_t*)(s->mmio+a);
}
static void put(unsigned a,uint32_t v){memcpy(regs+a,&v,4);}
static void put_sc(struct aq_softc*s,unsigned a,uint32_t v){memcpy((void*)(s->mmio+a),&v,4);}
static void aq_mmio_write(struct aq_softc*s,uint32_t a,uint32_t v){
 CHECK((s->mmio==regs||s->mmio==regs1)&&a<=65532&&!(a&3));put_sc(s,a,v);
 if(a==AQ_FW_GLB_CTL2_REG&&v==0x180e0&&!fw_stuck)put_sc(s,FW_MPI_DAISY_CHAIN_STATUS_REG,0x10);
 if(a==AQ_INTR_CTRL_REG&&(v&AQ_INTR_CTRL_RESET_IRQ)&&!irq_stuck)put_sc(s,a,v&~AQ_INTR_CTRL_RESET_IRQ);
 if(a==AQ_FW_MBOX_CMD_REG&&(v&AQ_FW_MBOX_CMD_EXECUTE)&&!mbox_stuck){
  uint32_t addr=aq_mmio_read(s,AQ_FW_MBOX_ADDR_REG),data=0;
  if(!no_mac&&addr==0x1000+160)data=0x02000000;
  if(!no_mac&&addr==0x1000+164)data=s->mmio==regs1?0x00020000:0x00010000;
  put_sc(s,AQ_FW_MBOX_VAL_REG,data);put_sc(s,AQ_FW_MBOX_ADDR_REG,addr+4);put_sc(s,a,v&~AQ_FW_MBOX_CMD_BUSY);
 }
 if(a==AQ_FW_SEM_RAM_REG&&sem_stuck)put(a,0);
}
static void aq_wait_us(struct aq_softc*s,uint32_t n){(void)s;now+=(n+999)/1000;}
uint32_t timer_ms(void){return now;}
void* kmalloc_aligned(size_t n,size_t align){
 void*p=0;if(posix_memalign(&p,align,n))return 0;
 CHECK(allocn<64);allocs[allocn++]=p;return p;
}
void klog(const char*f,...){(void)f;}
void net_register_device(netdev_t*n){CHECK(commands[((aq_native_t*)n->priv)->pci.dev&1]&0x400u);registered++;n->present=!register_full;}
void net_rx(netdev_t*n,const uint8_t*b,uint32_t len){CHECK(n&&b&&len>=14&&len<=1514);received++;}
uint16_t pci_read16(uint8_t b,uint8_t d,uint8_t f,uint8_t off){(void)b;(void)d;(void)f;if(off==0)return 0x1d6a;if(off==4)return commands[d&1];CHECK(!(off&1));return pcfg[off]|(uint16_t)pcfg[off+1]<<8;}
void pci_write16(uint8_t b,uint8_t d,uint8_t f,uint8_t off,uint16_t v){(void)b;(void)d;(void)f;CHECK(!(off&1));if(off==4)commands[d&1]=v;else {pcfg[off]=v;pcfg[off+1]=v>>8;if(off==0x44)pm_written=v;}}
uintptr_t pci_bar(const pci_dev_t*p,int n,int*io){(void)p;CHECK(n==0);*io=0;return (uintptr_t)(p->dev&1?regs1:regs);}
void pci_enable_from(const pci_dev_t*p,const char*file){CHECK(p&&file);enabled++;commands[p->dev&1]=(commands[p->dev&1]|7)&~0x400u;}
int pci_scan(int(*cb)(const pci_dev_t*,void*),void*arg){
 for(unsigned i=0;i<2;i++){
  pci_dev_t p={.dev=i,.vendor=0x1d6a,.device=0x07b1,.class_code=2};
  int rc=cb(&p,arg);if(rc)return rc;
 }return 0;
}
static pci_dev_t pd={.vendor=0x1d6a,.device=0x07b1,.class_code=2};
static void init(void){
 while(allocn)free(allocs[--allocn]);
 aq_count=0;memset(aq_cards,0,sizeof(aq_cards));memset(regs,0,sizeof(regs));
 now=registered=received=enabled=0;commands[0]=commands[1]=7;memset(pcfg,0,sizeof(pcfg));pm_written=0;
 irq_stuck=fw_stuck=sem_stuck=mbox_stuck=no_mac=register_full=0;
 put(AQ_FW_VERSION_REG,0x02010001);put(AQ_HW_REVISION_REG,0xa);
 put(FW_MPI_MBOX_ADDR_REG,0x2000);put(FW2X_MPI_EFUSEADDR_REG,0x1000);put(AQ_FW_SEM_RAM_REG,1);
 memcpy(regs1,regs,sizeof(regs));
}
static void inject(aq_native_t*c,unsigned n,uint16_t status,uint32_t type){
 unsigned i=c->rxcur;struct aq_rx_desc_wb*w=(void*)&c->rx[i];
 memset(c->rxbuf[i],0x7a,AQ_BUFSZ);memset(w,0,sizeof(*w));w->pkt_len=n;w->status=status;w->type=type;
 put_sc(&c->hw,RX_DMA_DESC_HEAD_PTR_REG(0),(i+1)%AQ_RXN);aq_poll(&c->nd);
}
int main(void){
 init();CHECK(aq_attach(&pd)==0&&enabled==1&&registered==1);
 aq_native_t*c=aq_cards[0];CHECK(c->nd.mac[0]==2&&c->nd.mac[5]==1);
 now+=250;CHECK(!aq_carrier(&c->nd));
 put(FW2X_MPI_STATE_REG,FW2X_CTRL_RATE_1G);now+=250;CHECK(aq_carrier(&c->nd));
 uint8_t frame[60]={0};CHECK(aq_send(&c->nd,frame,60)==0);
 CHECK(c->tx[0].buf_addr==(uintptr_t)c->txbuf[0]&&c->txused==1);
 put(TX_DMA_DESC_HEAD_PTR_REG(0),1);aq_poll(&c->nd);CHECK(!c->txused);
 for(unsigned i=0;i<AQ_TXN-1;i++)CHECK(aq_send(&c->nd,frame,60)==0);
 CHECK(aq_send(&c->nd,frame,60)<0);
 put(TX_DMA_DESC_HEAD_PTR_REG(0),c->txprod);aq_poll(&c->nd);CHECK(!c->txused);
 for(unsigned i=0;i<300;i++)inject(c,60,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP,0);
 CHECK(received==300);
 inject(c,5000,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP,0);
 inject(c,60,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP|AQ_RXDESC_STATUS_MACERR,0);
 inject(c,60,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP,AQ_RXDESC_TYPE_DMA_ERR);
 CHECK(received==300);
 inject(c,60,AQ_RXDESC_STATUS_DD,0);
 inject(c,60,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP,0);CHECK(received==300);
 inject(c,60,AQ_RXDESC_STATUS_DD|AQ_RXDESC_STATUS_EOP,0);CHECK(received==301);

 /* Descriptor fuzz, including packet chains and lengths independent of buffer
  * size. Valid frames still go through the native net_rx boundary. */
 uint32_t rng=0x107abcd;
 for(unsigned i=0;i<20000;i++){
  rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
  unsigned n=rng&0x3fff;
  uint16_t st=(uint16_t)(rng>>16)|AQ_RXDESC_STATUS_DD;
  inject(c,n,st,rng&0x1fff);
 }
 /* Reap never accepts a completion outside the outstanding TX window. */
 put(TX_DMA_DESC_HEAD_PTR_REG(0),AQ_TXN+1);aq_poll(&c->nd);
 CHECK(!c->hw.ok&&!(commands[0]&4));
 #define BAD(flag) do{init();flag=1;CHECK(aq_attach(&pd)<0&&!(commands[0]&4)&&!enabled);}while(0)
 BAD(irq_stuck);BAD(fw_stuck);BAD(mbox_stuck);BAD(no_mac);
 init();register_full=1;CHECK(aq_attach(&pd)<0&&!(commands[0]&4));
 init();put(AQ_FW_VERSION_REG,0x01050006);CHECK(aq_attach(&pd)<0&&!registered);
 init();put(AQ_HW_REVISION_REG,1);CHECK(aq_attach(&pd)<0&&!registered);
 init();put(AQ_HW_REVISION_REG,7);CHECK(aq_attach(&pd)<0&&!registered);

 init();put(AQ_FW_VERSION_REG,0x03010022);put(AQ_HW_REVISION_REG,2);
 CHECK(aq_attach(&pd)==0&&registered);
 c=aq_cards[0];put(FW2X_MPI_STATE_REG,FW2X_CTRL_RATE_10G);now+=250;CHECK(aq_carrier(&c->nd));
 put(AQ_FW_VERSION_REG,0xffffffffu);now+=250;CHECK(!aq_carrier(&c->nd)&&!(commands[0]&4));
 init();put(AQ_FW_SEM_RAM_REG,0);sem_stuck=1;CHECK(aq_attach(&pd)<0&&!registered);
 init();pd.device=0x8125;CHECK(aq_attach(&pd)<0&&!enabled);pd.device=0x07b1;

 init();pcfg[6]=16;pcfg[0x34]=0x40;
 pcfg[0x40]=1;pcfg[0x41]=0x50;pcfg[0x44]=3;pcfg[0x45]=0x80;
 pcfg[0x50]=5;pcfg[0x51]=0x60;pcfg[0x52]=1;
 pcfg[0x60]=0x11;pcfg[0x62]=7;pcfg[0x63]=0x80;
 CHECK(aq_attach(&pd)==0&&!(pm_written&0x8003)&&!pcfg[0x52]&&pcfg[0x62]==7&&!pcfg[0x63]);
 init();pcfg[6]=16;pcfg[0x34]=0x40;pcfg[0x40]=5;pcfg[0x41]=0x40;
 CHECK(aq_attach(&pd)<0&&!registered&&!(commands[0]&4));

 init();aqc107_probe_all();CHECK(registered==2&&enabled==2);
 aqc107_probe_all();CHECK(registered==2&&enabled==2);
 CHECK(aq_cards[0]->nd.mac[5]==1&&aq_cards[1]->nd.mac[5]==2);
 for(unsigned i=0;i<2;i++){
  put_sc(&aq_cards[i]->hw,FW2X_MPI_STATE_REG,FW2X_CTRL_RATE_1G);
 }
 now+=250;
 CHECK(aq_send(&aq_cards[0]->nd,frame,60)==0);
 CHECK(aq_send(&aq_cards[1]->nd,frame,60)==0);
 CHECK(aq_cards[0]->tx[0].buf_addr!=aq_cards[1]->tx[0].buf_addr);
 put_sc(&aq_cards[0]->hw,TX_DMA_DESC_HEAD_PTR_REG(0),AQ_TXN+1);
 aq_poll(&aq_cards[0]->nd);
 CHECK(!(commands[0]&4)&&(commands[1]&4)&&aq_cards[1]->hw.ok);
 CHECK(aq_send(&aq_cards[1]->nd,frame,60)==0);
 init();printf("AQC107 native: %u checks passed\n",checks);return 0;
}
