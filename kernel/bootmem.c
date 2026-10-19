#include "bootmem.h"

/* Read bytewise: firmware entries can have extended, unaligned strides. */
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static uint64_t rd64(const uint8_t* p) {
    return rd32(p) | (uint64_t)rd32(p+4)<<32;
}
static uint32_t kib(uint64_t bytes) {
    uint64_t n=bytes>>10;
    return n>0xffffffffu?0xffffffffu:(uint32_t)n;
}

uint32_t bootmem_kb_mb2(const void* data, uint32_t size) {
    if(!data || size<8)return 0;
    const uint8_t* p=data;
    uint32_t total=rd32(p),fallback=0,mapped=0;
    if(total<8 || total>size)return 0;
    for(uint32_t off=8;total-off>=8;) {
        uint32_t type=rd32(p+off),n=rd32(p+off+4);
        if(n<8 || n>total-off || !type)break;
        if(type==4 && n>=16) {
            uint64_t basic=(uint64_t)rd32(p+off+12)+1024;
            fallback=basic>0xffffffffu?0xffffffffu:(uint32_t)basic;
        }
        if(type==6 && n>=16) {
            uint32_t stride=rd32(p+off+8);
            if(stride>=24 && rd32(p+off+12)==0 && (n-16)%stride==0) {
                uint64_t usable=0;
                for(uint32_t pos=16;n-pos>=stride;pos+=stride) {
                    const uint8_t* e=p+off+pos;
                    uint64_t base=rd64(e),len=rd64(e+8);
                    if(rd32(e+16)!=1 || len>~(uint64_t)0-base)continue;
                    usable=len>~(uint64_t)0-usable?~(uint64_t)0:usable+len;
                }
                uint32_t count=kib(usable);
                if(count>mapped)mapped=count;
            }
        }
        /* Advance without overflowing or walking past the supplied buffer. */
        if(n>0xfffffff8u)break;
        uint32_t advance=(n+7u)&~7u;
        if(advance>total-off)break;
        off+=advance;
    }
    return mapped?mapped:fallback;
}

uint32_t bootmem_wake_page_mb2(const void* data,uint32_t size) {
    if(!data || size<8)return 0;
    const uint8_t* p=data;uint32_t total=rd32(p),page=0;
    if(total<8 || total>size)return 0;
    for(uint32_t off=8;total-off>=8;) {
        uint32_t type=rd32(p+off),n=rd32(p+off+4);
        if(n<8 || n>total-off || !type)break;
        if(type==6 && n>=16) {
            uint32_t stride=rd32(p+off+8);
            if(stride>=24 && !rd32(p+off+12) && (n-16)%stride==0)
                for(uint32_t pos=16;n-pos>=stride;pos+=stride) {
                    const uint8_t* e=p+off+pos;uint64_t base=rd64(e),len=rd64(e+8);
                    if(rd32(e+16)!=1 || len>~(uint64_t)0-base)continue;
                    uint64_t end=base+len;if(end>0x90000)end=0x90000;
                    if(end<0x11000)continue;
                    uint32_t candidate=(uint32_t)(end-4096)&~4095u;
                    if(candidate>=0x10000 && candidate>=base && candidate>page)page=candidate;
                }
        }
        if(n>0xfffffff8u)break;
        uint32_t advance=(n+7)&~7u;if(advance>total-off)break;off+=advance;
    }
    return page;
}
