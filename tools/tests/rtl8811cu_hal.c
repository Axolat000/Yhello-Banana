
#include "../../third_party/rtw88/rtw8821c_native.h"
#include "cu_test_firmware.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void*calloc(size_t,size_t);
extern void free(void*);
static unsigned checks,allocated;
#define CHECK(v) do{checks++;if(!(v)){printf("FAIL %d: %s\n",__LINE__,#v);fflush(0);abort();}}while(0)
void*kmalloc(size_t n){allocated++;return calloc(1,n);}
void kfree(void*p){if(p){CHECK(allocated);allocated--;free(p);}}
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
 if(a==REG_AUTO_LLT_V1&&!stall)regs[a]&=~BIT_AUTO_INIT_LLT_V1;
 return 0;
}
static int tx(struct rtw_dev*d,const u8*b,unsigned n,unsigned qsel){
 CHECK(d->ok&&qsel==0x10&&n&&n<=4097);CHECK((n+48)%512);
 memcpy(staged,b,n);staged_len=n;transfers++;
 put32(REG_FIFOPAGE_CTRL_2,read32(REG_FIFOPAGE_CTRL_2)|BIT_BCN_VALID_V1);
 return 0;
}
static void wait_us(struct rtw_dev*d,unsigned us){(void)d;CHECK(us<=100000);}
static void reset(struct rtw_dev*d){
 CHECK(!allocated);memset(regs,0,sizeof(regs));memset(physical,255,sizeof(physical));
 memset(imem,0,sizeof(imem));memset(dmem,0,sizeof(dmem));memset(emem,0,sizeof(emem));
 memset(d,0,sizeof(*d));d->ok=1;d->control=control;d->tx=tx;d->wait=wait_us;d->chip=&cu_chip;
 d->hci.type=RTW_HCI_TYPE_USB;d->hci.bulkout_num=3;d->hci.rpwm_addr=0xfe58;d->hal.cut_version=2;d->rfe=0;
 ops=transfers=staged_len=fail_at=segment_copies=0;stall=checksum_bad=ready_bad=0;
 put32(LTECOEX_ACCESS_CTRL,LTECOEX_READY);put32(REG_MCUFW_CTRL,0);
 regs[REG_CR]=0xea;regs[REG_SYS_PW_CTRL]=BIT(1); /* power-ready bit */
}
int main(void){
 struct rtw_dev d;reset(&d);
 struct firmware image={fw_bytes,sizeof(fw_bytes)};struct rtw_fw_state fw={&image};
 CHECK(check_firmware_size(fw_bytes,sizeof(fw_bytes)));
 CHECK(!__rtw_download_firmware(&d,&fw)&&d.ok&&segment_copies>3&&!allocated);
 const struct rtw_fw_hdr*h=(const void*)fw_bytes;
 CHECK(!memcmp(dmem,fw_bytes+64,h->dmem_size+8));
 CHECK(!memcmp(imem,fw_bytes+64+h->dmem_size+8,h->imem_size+8));
 CHECK(!memcmp(emem,fw_bytes+64+h->dmem_size+8+h->imem_size+8,h->emem_size+8));
 unsigned nops=ops;
 for(unsigned which=0;which<3;which++){
  reset(&d);if(which==0)stall=1;if(which==1)checksum_bad=1;if(which==2)ready_bad=1;
  int rc=__rtw_download_firmware(&d,&fw);if(!rc&&d.ok)printf("fault %u unexpectedly succeeded\n",which);CHECK(rc<0||!d.ok);CHECK(!allocated);
 }
 for(unsigned at=1;at<=nops;at++){
  reset(&d);fail_at=at;int rc=__rtw_download_firmware(&d,&fw);
  CHECK(rc<0||!d.ok);CHECK(!allocated);
 }
 reset(&d);CHECK(!cu_phy_init(&d,32)&&d.ok);
 for(unsigned ch=1;ch<=11;ch++){rtw8821c_set_channel(&d,ch,0,0);CHECK(d.ok);}
 u8 logical[512];CHECK(!cu_efuse_read(&d,logical));for(unsigned i=0;i<512;i++)CHECK(logical[i]==255);
 static const u8 sparse[]={0x0e,0x29,0x81,0x2f,0x2e,2,0,255};
 memset(logical,0xab,sizeof(logical));CHECK(!cu_efuse_decode(sparse,sizeof(sparse),logical));
 CHECK(logical[0]==0x29&&logical[1]==0x81&&logical[0x88]==2&&logical[0x89]==0);
 unsigned rng=0x8821c811;
 for(unsigned trial=0;trial<20000;trial++){
  u8 corrupt[416],before[512];unsigned len=trial%417;
  for(unsigned i=0;i<len;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;corrupt[i]=rng>>24;}
  memcpy(before,logical,512);int rc=cu_efuse_decode(corrupt,len,logical);
  if(rc)CHECK(!memcmp(logical,before,512));
  u8 header[64];memcpy(header,fw_bytes,64);
  for(unsigned i=0;i<8;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;header[rng%64]^=rng>>24;}
  (void)check_firmware_size(header,sizeof(fw_bytes));
 }
 reset(&d);printf("RTL8821CU hardware core: %u checks passed\n",checks);return 0;
}
