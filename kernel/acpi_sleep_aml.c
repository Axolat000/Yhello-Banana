#include "acpi_sleep.h"

static int integer(const uint8_t* p,uint32_t n,uint32_t* value) {
    if(!n)return 0;
    if(*p<=1){*value=*p;return 1;}
    unsigned bytes=*p==0x0a?1:*p==0x0b?2:*p==0x0c?4:*p==0x0e?8:0;
    if(!bytes || n<bytes+1)return 0;
    uint64_t v=0;for(unsigned i=0;i<bytes;i++)v|=(uint64_t)p[i+1]<<(8*i);
    if(v>7)return 0;
    *value=(uint32_t)v;return bytes+1;
}
int acpi_sleep_types(const uint8_t* aml,uint32_t len,const char name[4],uint16_t* a,uint16_t* b) {
    if(!aml || !name || !a || !b)return 0;
    for(uint32_t i=0;i<len;i++) {
        if(aml[i]!=8)continue;                     /* NameOp */
        uint32_t p=i+1;
        if(p<len && aml[p]==0x5c)p++;              /* root prefix */
        while(p<len && aml[p]==0x5e)p++;           /* parent prefixes */
        if(len-p<6)continue;
        if(aml[p]!=name[0] || aml[p+1]!=name[1] || aml[p+2]!=name[2] || aml[p+3]!=name[3] || aml[p+4]!=0x12)continue;
        p+=5;uint32_t start=p,extra=aml[p]>>6,bytes=extra+1;
        if(len-p<bytes)continue;
        uint32_t size=aml[p]&(extra?15:63);
        for(uint32_t j=1;j<=extra;j++)size|=(uint32_t)aml[p+j]<<(4+(j-1)*8);
        if(size<bytes+1 || size>len-start)continue;
        uint32_t end=start+size;p+=bytes;
        if(aml[p++]<2)continue;
        uint32_t va,vb;int n=integer(aml+p,end-p,&va);if(!n)continue;p+=(uint32_t)n;
        n=integer(aml+p,end-p,&vb);if(!n)continue;
        *a=(uint16_t)va;*b=(uint16_t)vb;return 1;
    }
    return 0;
}
