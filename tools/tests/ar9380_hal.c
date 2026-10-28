#include "types.h"
#include "kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
extern void*calloc(size_t,size_t);
extern void free(void*);
static unsigned checks;
#define CHECK(c) do{checks++;if(!(c)){printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort();}}while(0)
#include "../../third_party/athn/ar9380_native.h"
#undef printf
#undef malloc
#undef free
static uint32_t now;
void*kmalloc(size_t n){return calloc(1,n);}
void kfree(void*p){free(p);}
void klog(const char*f,...){(void)f;}
uint32_t timer_ms(void){return now;}
static void ath_wait_us(unsigned n){now+=(n+999)/1000;}
static void ar9003_reset_txsring(struct athn_softc*s){(void)s;}
static uint8_t physical[4096];
static uint32_t read_reg(struct athn_softc*s,uint32_t reg){
 (void)s;
 static unsigned index;
 if(reg>=0x2000&&reg<0x4000){index=(reg-0x2000)/4;return 0;}
 if(reg==AR_EEPROM_STATUS_DATA){CHECK(index<2048);return physical[index*2]|(unsigned)physical[index*2+1]<<8;}
 return 0;
}
static void write_reg(struct athn_softc*s,uint32_t reg,uint32_t v){(void)s;(void)reg;(void)v;}
static void barrier(struct athn_softc*s){(void)s;}
static void reset(struct athn_softc*s){
 if(s->eep)free(s->eep);
 memset(s,0,sizeof(*s));memset(physical,0,sizeof(physical));
 s->ok=1;s->mac_ver=AR_SREV_VERSION_9380;s->mac_rev=3;
 s->ops.read=read_reg;s->ops.write=write_reg;s->ops.write_barrier=barrier;s->ops.get_rom_template=ar9380_get_rom_template;
}
static unsigned block(unsigned end,unsigned ref,const uint8_t*data,unsigned len){
 CHECK(len<2048&&end>=len+5);
 uint8_t h[4]={(3u<<5)|(ref&31),((ref&32)<<2)|(len>>4),(len&15)<<4,0};
 uint16_t sum=0;for(unsigned i=0;i<len;i++)sum+=data[i];
 for(unsigned i=0;i<4;i++)physical[end--]=h[i];
 for(unsigned i=0;i<len;i++)physical[end--]=data[i];
 physical[end--]=sum;physical[end--]=sum>>8;return end;
}
int main(void){
 struct athn_softc s={0};reset(&s);
 const uint8_t mac_patch[]={2,6,2,0,0,0,0,1};
 unsigned end=block(1023,2,mac_patch,sizeof(mac_patch));
 const uint8_t later[]={8,1,0xab};block(end,0,later,sizeof(later));
 CHECK(!ar9003_find_rom(&s)&&s.rom_bytes==1024&&s.eep_size==1088);
 CHECK(!ar9003_read_rom(&s)&&s.rom_valid);
 uint8_t expected[1088];memcpy(expected,ar9380_def_rom,sizeof(expected));memcpy(expected+2,mac_patch+2,6);expected[8]=0xab;
 CHECK(!memcmp(s.eep,expected,sizeof(expected)));
 /* EEPROM reads must preserve the little-endian word stream at either parity. */
 uint8_t out[9];for(unsigned i=0;i<16;i++)physical[i]=i;
 for(unsigned start=8;start<=9;start++){
  CHECK(!ar9003_read_eep_data(&s,start,out,9));
  for(unsigned j=0;j<9;j++)CHECK(out[j]==start-j);
 }
 CHECK(ar9003_read_eep_data(&s,3,out,5)!=0);
 CHECK(ar9003_read_eep_data(&s,4096,out,1)!=0);
 const uint8_t bad[]={255,255,255};uint8_t before[1088];memcpy(before,s.eep,sizeof(before));
 CHECK(ar9003_restore_rom_block(&s,3,2,bad,sizeof(bad))!=0&&!memcmp(before,s.eep,sizeof(before)));
 CHECK(ar9003_restore_rom_block(&s,7,2,bad,sizeof(bad))!=0);
 CHECK(ar9003_restore_rom_block(&s,3,63,bad,sizeof(bad))!=0);
 /* Match a floating-point-derived, near-zero-TX-mismatch reference case. */
 int32_t v[2][2][3]={{{100,1000,20},{-100,1000,20}},{{100,1000,20},{100,1000,-20}}};
 uint32_t a[12];unsigned k=0;
 for(unsigned i=0;i<2;i++)for(unsigned j=0;j<2;j++)for(unsigned q=0;q<3;q++)a[k++]=v[i][j][q]&4095;
 int32_t res[6]={a[0]|a[1]<<12|(a[2]&255)<<24,(a[2]>>8)|a[3]<<4,
  a[4]|a[5]<<12|(a[6]&255)<<24,(a[6]>>8)|a[7]<<4,
  a[8]|a[9]<<12|(a[10]&255)<<24,(a[10]>>8)|a[11]<<4};
 int32_t coeff[2];CHECK(!ar9003_get_iq_corr(&s,res,coeff)&&coeff[0]==0&&coeff[1]==15099);
 uint32_t rng=0x93800022;
 for(unsigned trial=0;trial<20000;trial++){
  uint8_t payload[128];unsigned len=trial%129;
  for(unsigned i=0;i<len;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;payload[i]=rng>>24;}
  memcpy(before,s.eep,sizeof(before));
  int rc=ar9003_restore_rom_block(&s,3,2,payload,len);
  if(rc)CHECK(!memcmp(before,s.eep,sizeof(before)));
  for(unsigned j=0;j<6;j++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;res[j]=rng;}
  rc=ar9003_get_iq_corr(&s,res,coeff);
  if(!rc)CHECK(coeff[0]>=0&&coeff[0]<=16383&&coeff[1]>=0&&coeff[1]<=16383);
 }
 free(s.eep);printf("AR9380 EEPROM/IQ: %u checks passed\n",checks);return 0;
}
