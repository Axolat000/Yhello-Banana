/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wifi_crypto.h"
#include "aead.h"
#include "kstring.h"

static void wipe(void* p,uint32_t n) { volatile uint8_t* b=p; while(n--) *b++=0; }
static uint32_t rol(uint32_t v,unsigned n) { return v<<n|v>>(32-n); }
typedef struct { uint32_t h[5],used; uint64_t bytes; uint8_t block[64]; } wsha1_t;
static void wsha1_block(wsha1_t* s,const uint8_t* b) {
    uint32_t w[80];
    for(int i=0;i<16;i++) w[i]=(uint32_t)b[4*i]<<24|(uint32_t)b[4*i+1]<<16|
                                  (uint32_t)b[4*i+2]<<8|b[4*i+3];
    for(int i=16;i<80;i++) w[i]=rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=s->h[0],c=s->h[2],d=s->h[3],e=s->h[4],bb=s->h[1];
    for(int i=0;i<80;i++) {
        uint32_t f,k;
        if(i<20) { f=(bb&c)|(~bb&d); k=0x5a827999; }
        else if(i<40) { f=bb^c^d;k=0x6ed9eba1; }
        else if(i<60) { f=(bb&c)|(bb&d)|(c&d); k=0x8f1bbcdc; }
        else { f=bb^c^d;k=0xca62c1d6; }
        uint32_t t=rol(a,5)+f+e+k+w[i];
        e=d; d=c; c=rol(bb,30); bb=a; a=t;
    }
    s->h[0]+=a; s->h[1]+=bb; s->h[2]+=c; s->h[3]+=d; s->h[4]+=e;
    wipe(w,sizeof(w));
}
static void wsha1_init(wsha1_t* s) {
    memset(s,0,sizeof(*s));
    s->h[0]=0x67452301; s->h[1]=0xefcdab89; s->h[2]=0x98badcfe;
    s->h[3]=0x10325476; s->h[4]=0xc3d2e1f0;
}
static void wsha1_update(wsha1_t* s,const void* data,uint32_t n) {
    const uint8_t* b=data; s->bytes+=n;
    while(n) {
        uint32_t take=64-s->used; if(take>n) take=n;
        memcpy(s->block+s->used,b,take); s->used+=take;b+=take;n-=take;
        if(s->used==64) { wsha1_block(s,s->block); s->used=0; }
    }
}
static void wsha1_final(wsha1_t* s,uint8_t out[20]) {
    uint64_t bits=s->bytes*8;
    uint8_t end[72]={0x80};
    uint32_t pad=s->used<56?56-s->used:120-s->used;
    for(int i=0;i<8;i++) end[pad+i]=(uint8_t)(bits>>(56-8*i));
    wsha1_update(s,end,pad+8);
    for(int i=0;i<20;i++) out[i]=(uint8_t)(s->h[i/4]>>(24-8*(i%4)));
    wipe(s,sizeof(*s)); wipe(end,sizeof(end));
}
void wifi_hmac_sha1(const uint8_t* key,uint32_t keylen,const void* data,uint32_t n,uint8_t out[20]) {
    uint8_t pad[64]={0},inner[20];
    wsha1_t s;
    if(keylen>64) {
        wsha1_init(&s); wsha1_update(&s,key,keylen); wsha1_final(&s,pad);
    } else memcpy(pad,key,keylen);
    for(int i=0;i<64;i++) pad[i]^=0x36;
    wsha1_init(&s); wsha1_update(&s,pad,64); wsha1_update(&s,data,n); wsha1_final(&s,inner);
    for(int i=0;i<64;i++) pad[i]^=0x36^0x5c;
    wsha1_init(&s); wsha1_update(&s,pad,64); wsha1_update(&s,inner,20); wsha1_final(&s,out);
    wipe(pad,sizeof(pad)); wipe(inner,sizeof(inner));
}
static int hex(char c) {
    if(c>='0' && c<='9') return c-'0';
    if(c>='a' && c<='f') return c-'a'+10;
    if(c>='A' && c<='F') return c-'A'+10;
    return -1;
}
int wifi_pmk(const char* pass,const uint8_t* ssid,uint32_t ssidlen,uint8_t pmk[32]) {
    uint32_t n=strlen(pass); memset(pmk,0,32);
    if(!ssidlen || ssidlen>32) return -1;
    if(n==64) {
        for(int i=0;i<32;i++) {
            int a=hex(pass[2*i]),b=hex(pass[2*i+1]);
            if(a<0 || b<0) { wipe(pmk,32); return -1; }
            pmk[i]=(uint8_t)(a*16+b);
        }
        return 0;
    }
    if(n<8 || n>63) return -1;
    for(uint32_t i=0;i<n;i++) if((uint8_t)pass[i]<32 || (uint8_t)pass[i]>126) return -1;
    uint8_t salt[36],u[20],t[20];
    memcpy(salt,ssid,ssidlen); memset(salt+ssidlen,0,4);
    for(unsigned block=1;block<=2;block++) {
        salt[ssidlen+3]=(uint8_t)block;
        wifi_hmac_sha1((const uint8_t*)pass,n,salt,ssidlen+4,u); memcpy(t,u,20);
        for(int iteration=1;iteration<4096;iteration++) {
            wifi_hmac_sha1((const uint8_t*)pass,n,u,20,u);
            for(int j=0;j<20;j++) t[j]^=u[j];
        }
        memcpy(pmk+(block-1)*20,t,block==1?20:12);
    }
    wipe(salt,sizeof(salt)); wipe(u,sizeof(u)); wipe(t,sizeof(t)); return 0;
}
void wifi_ptk(const uint8_t pmk[32],const uint8_t aa[6],const uint8_t spa[6],
              const uint8_t an[32],const uint8_t sn[32],uint8_t ptk[48]) {
    uint8_t in[100],out[20];
    static const char label[]="Pairwise key expansion";
    uint32_t at=sizeof(label); memcpy(in,label,at); /* includes separating NUL */
    int mac_order=memcmp(aa,spa,6)<0, nonce_order=memcmp(an,sn,32)<0;
    memcpy(in+at,mac_order?aa:spa,6);at+=6;
    memcpy(in+at,mac_order?spa:aa,6);at+=6;
    memcpy(in+at,nonce_order?an:sn,32);at+=32;
    memcpy(in+at,nonce_order?sn:an,32);at+=32;
    for(unsigned i=0;i<3;i++) {
        in[at]=(uint8_t)i;
        wifi_hmac_sha1(pmk,32,in,at+1,out);
        memcpy(ptk+i*20,out,i==2?8:20);
    }
    wipe(in,sizeof(in)); wipe(out,sizeof(out));
}
/* AES inverse rounds use the existing AES-128 key schedule. AES encryption used by TLS is unchanged. */
static uint8_t mul(uint8_t a,uint8_t b) {
    uint8_t r=0;
    for(int i=0;i<8;i++) { if(b&1) r^=a; a=(uint8_t)((a<<1)^((a&0x80)?0x1b:0)); b>>=1; }
    return r;
}
static const uint8_t INV_SBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d,
};
static void addkey(uint8_t s[16],const aes128_t* a,int round) {
    for(int i=0;i<16;i++) s[i]^=(uint8_t)(a->rk[round*4+i/4]>>(24-(i%4)*8));
}
static void decrypt(const aes128_t* a,uint8_t b[16]) {
    uint8_t s[16],t[16]; memcpy(s,b,16); addkey(s,a,10);
    for(int round=9;round>=0;round--) {
        for(int r=0;r<4;r++) for(int c=0;c<4;c++) t[r+4*c]=INV_SBOX[s[r+4*((c-r+4)%4)]];
        memcpy(s,t,16); addkey(s,a,round);
        if(round) for(int c=0;c<4;c++) {
            uint8_t* v=s+4*c;
            t[0]=mul(v[0],14)^mul(v[1],11)^mul(v[2],13)^mul(v[3],9);
            t[1]=mul(v[0],9)^mul(v[1],14)^mul(v[2],11)^mul(v[3],13);
            t[2]=mul(v[0],13)^mul(v[1],9)^mul(v[2],14)^mul(v[3],11);
            t[3]=mul(v[0],11)^mul(v[1],13)^mul(v[2],9)^mul(v[3],14);
            memcpy(v,t,4);
        }
    }
    memcpy(b,s,16); wipe(s,16);wipe(t,16);
}
int wifi_key_unwrap(const uint8_t kek[16],const uint8_t* in,uint32_t len,uint8_t* out) {
    if(len<24 || len>512 || (len&7)) return -1;
    aes128_t aes; aes128_init(&aes,kek);
    uint8_t a[8],b[16]; memcpy(a,in,8); memmove(out,in+8,len-8);
    uint32_t n=len/8-1;
    for(int j=5;j>=0;j--) for(uint32_t i=n;i;i--) {
        memcpy(b,a,8);
        uint64_t t=n*(unsigned)j+i;
        for(int k=0;k<8;k++) b[7-k]^=(uint8_t)(t>>(8*k));
        memcpy(b+8,out+8*(i-1),8); decrypt(&aes,b);
        memcpy(a,b,8);memcpy(out+8*(i-1),b+8,8);
    }
    uint8_t bad=0; for(int i=0;i<8;i++) bad|=a[i]^0xa6;
    wipe(&aes,sizeof(aes));wipe(a,8);wipe(b,16);
    if(bad) { wipe(out,len-8); return -1; }
    return len-8;
}
static int ccmp_nonce(const uint8_t* h,uint32_t hlen,uint64_t pn,uint8_t nonce[13],uint8_t aad[32]) {
    if((hlen!=24 && hlen!=26) || !pn || pn>=((uint64_t)1<<48) ||
       (h[0]&0x0c)!=8 || (h[1]&3)==3 || !(h[1]&0x40) ||
       (hlen==26)!=((h[0]&0x80)!=0)) return -1;
    uint8_t tid=hlen==26?h[24]&15:0;
    nonce[0]=tid; memcpy(nonce+1,h+10,6);
    for(int i=0;i<6;i++) nonce[7+i]=(uint8_t)(pn>>(40-8*i));
    memset(aad,0,32); aad[1]=(uint8_t)(hlen==26?24:22);
    aad[2]=h[0]&0x8f; aad[3]=h[1]&(hlen==26?0x47:0xc7);
    memcpy(aad+4,h+4,18); aad[22]=h[22]&15;
    if(hlen==26) aad[24]=tid;
    return 0;
}
static void cbc(const aes128_t* aes,uint8_t mac[16],const uint8_t b[16]) {
    for(int i=0;i<16;i++) mac[i]^=b[i];
    aes128_encrypt(aes,mac,mac);
}
static void ccmp_mac(const aes128_t* aes,const uint8_t nonce[13],const uint8_t aad[32],
                     const uint8_t* p,uint32_t n,uint8_t mac[16]) {
    uint8_t b[16]={0x59}; memcpy(b+1,nonce,13); b[14]=(uint8_t)(n>>8);b[15]=(uint8_t)n;
    memset(mac,0,16);cbc(aes,mac,b);cbc(aes,mac,aad);cbc(aes,mac,aad+16);
    while(n) {
        uint32_t take=n<16?n:16;memset(b,0,16);memcpy(b,p,take);
        cbc(aes,mac,b);n-=take;p+=take;
    }
    wipe(b,16);
}
static void ccmp_ctr(const aes128_t* aes,const uint8_t nonce[13],uint8_t* p,uint32_t n,uint8_t s0[16]) {
    uint8_t a[16]={1},stream[16];memcpy(a+1,nonce,13);
    aes128_encrypt(aes,a,s0);
    unsigned count=1;
    while(n) {
        a[14]=(uint8_t)(count>>8);a[15]=(uint8_t)count++;
        aes128_encrypt(aes,a,stream);
        uint32_t take=n<16?n:16;
        for(uint32_t i=0;i<take;i++) p[i]^=stream[i];
        n-=take;p+=take;
    }
    wipe(a,16);wipe(stream,16);
}
int wifi_ccmp_seal(const uint8_t tk[16],const uint8_t* h,uint32_t hlen,uint64_t pn,
                   uint8_t* data,uint32_t n,uint8_t mic[8]) {
    uint8_t nonce[13],aad[32],mac[16],s0[16];
    if(n>2304 || ccmp_nonce(h,hlen,pn,nonce,aad)) return -1;
    aes128_t aes;aes128_init(&aes,tk);
    ccmp_mac(&aes,nonce,aad,data,n,mac);ccmp_ctr(&aes,nonce,data,n,s0);
    for(int i=0;i<8;i++) mic[i]=mac[i]^s0[i];
    wipe(&aes,sizeof(aes));wipe(mac,16);wipe(s0,16);
    return 0;
}
int wifi_ccmp_open(const uint8_t tk[16],const uint8_t* h,uint32_t hlen,uint64_t pn,
                   uint8_t* data,uint32_t n,const uint8_t mic[8]) {
    uint8_t nonce[13],aad[32],mac[16],s0[16],tag[8];
    if(n>2304 || ccmp_nonce(h,hlen,pn,nonce,aad)) return -1;
    aes128_t aes;aes128_init(&aes,tk);
    ccmp_ctr(&aes,nonce,data,n,s0);ccmp_mac(&aes,nonce,aad,data,n,mac);
    for(int i=0;i<8;i++) tag[i]=mac[i]^s0[i];
    int bad=ct_memcmp(tag,mic,8);
    wipe(&aes,sizeof(aes));wipe(mac,16);wipe(s0,16);wipe(tag,8);
    if(bad) wipe(data,n);
    return bad?-1:0;
}
