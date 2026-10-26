/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../../crypto/wifi_crypto.h"
#include "../../kernel/kstring.h"
extern int printf(const char*,...);
extern int fflush(void*);
extern void abort(void);
static unsigned checks;
#define CHECK(c) do { checks++; if(!(c)) { printf("FAIL %d: %s\n",__LINE__,#c);fflush(0);abort(); } } while(0)
#include "vectors.h"
int main(void) {
    vector_tests();
    uint8_t key[32], zero[32]={0}, b[32]={0}, hdr[26]={8,0x41}, mic[8];
    CHECK(wifi_pmk("short",(const uint8_t*)"IEEE",4,key)<0 && !memcmp(key,zero,32));
    CHECK(wifi_pmk("12345678",(const uint8_t*)"",0,key)<0);
    CHECK(wifi_pmk("12345678",(const uint8_t*)"IEEE",33,key)<0);
    CHECK(wifi_pmk("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                   (const uint8_t*)"IEEE",4,key)==0);
    for(int i=0;i<32;i++) CHECK(key[i]==i);
    CHECK(wifi_pmk("z00102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                   (const uint8_t*)"IEEE",4,key)<0 && !memcmp(key,zero,32));
    CHECK(wifi_key_unwrap(key,b,16,b)<0 && wifi_key_unwrap(key,b,25,b)<0);
    CHECK(wifi_ccmp_seal(key,hdr,24,0,b,1,mic)<0);
    CHECK(wifi_ccmp_seal(key,hdr,24,(uint64_t)1<<48,b,1,mic)<0);
    CHECK(wifi_ccmp_seal(key,hdr,24,1,b,2305,mic)<0);
    CHECK(wifi_ccmp_seal(key,hdr,26,1,b,1,mic)<0);
    hdr[1]=0x43; CHECK(wifi_ccmp_seal(key,hdr,24,1,b,1,mic)<0);
    printf("WPA2 crypto: %u checks passed against independent vectors\n",checks);
}
