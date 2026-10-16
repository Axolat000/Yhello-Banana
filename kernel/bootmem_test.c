#include "bootmem_test.h"
#include "bootmem.h"

typedef struct { uint8_t data[256]; uint32_t size,map; } fixture;
static void wr32(uint8_t* p,uint32_t n) { for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8)); }
static void wr64(uint8_t* p,uint64_t n) { wr32(p,(uint32_t)n);wr32(p+4,(uint32_t)(n>>32)); }
static void start(fixture* f) { for(unsigned i=0;i<sizeof(f->data);i++)f->data[i]=0;f->size=8;f->map=0; }
static void basic(fixture* f) {
    uint8_t* p=f->data+f->size;wr32(p,4);wr32(p+4,16);wr32(p+8,640);
    wr32(p+12,3u*1024*1024-1024);f->size+=16;
}
static void entry(uint8_t* p,uint64_t base,uint64_t len,uint32_t type) { wr64(p,base);wr64(p+8,len);wr32(p+16,type); }
static void map(fixture* f,uint32_t stride,uint32_t gib) {
    const uint64_t g=1ull<<30;f->map=f->size;
    uint8_t* p=f->data+f->size;wr32(p,6);wr32(p+4,16+3*stride);wr32(p+8,stride);
    entry(p+16,0,3*g,1);entry(p+16+stride,3*g,g,2);entry(p+16+2*stride,4*g,(gib-3)*g,1);
    f->size+=(16+3*stride+7)&~7u;
}
static uint32_t finish(fixture* f) { wr32(f->data+f->size+4,8);f->size+=8;wr32(f->data,f->size);return bootmem_kb_mb2(f->data,f->size); }
void bootmem_tests_run(void (*report)(const char*,int,const char*)) {
    fixture f;const uint32_t g=1024u*1024;
    start(&f);basic(&f);map(&f,24,8);
    report("8 GiB map overrides earlier 3 GiB basic information",finish(&f)==8*g,"");
    start(&f);map(&f,24,8);basic(&f);
    report("later basic information does not replace the full map",finish(&f)==8*g,"");
    start(&f);basic(&f);map(&f,24,24);
    report("24 GiB includes RAM remapped above 4 GiB",finish(&f)==24*g,"");
    start(&f);map(&f,24,64);
    report("64 GiB totals do not overflow a 32-bit byte counter",finish(&f)==64*g,"");
    start(&f);map(&f,24,4);
    report("PCI holes and reserved regions are not counted as RAM",finish(&f)==4*g,"");
    start(&f);basic(&f);report("basic-only firmware retains its fallback",finish(&f)==3*g,"");
    start(&f);map(&f,32,8);report("extended memory-map entries are supported",finish(&f)==8*g,"");
    start(&f);map(&f,25,8);report("unaligned entry strides are read safely",finish(&f)==8*g,"");
    start(&f);basic(&f);map(&f,24,8);wr32(f.data+f.map+8,0);
    report("invalid entry sizes fall back without looping",finish(&f)==3*g,"");
    start(&f);basic(&f);map(&f,24,8);wr32(f.data+f.map+4,16+3*24-1);
    report("truncated entries are not read",finish(&f)==3*g,"");
    start(&f);basic(&f);map(&f,24,8);wr32(f.data+f.map+12,1);
    report("unknown map versions retain basic information",finish(&f)==3*g,"");
    start(&f);basic(&f);map(&f,24,8);wr64(f.data+f.map+16+2*24+8,~(uint64_t)0);
    report("overflowing physical ranges are ignored",finish(&f)==3*g,"");
    start(&f);map(&f,24,8);wr64(f.data+f.map+16+2*24+8,1ull<<53);
    report("totals beyond the KiB field saturate instead of wrapping",finish(&f)==0xffffffffu,"");
    start(&f);basic(&f);finish(&f);
    report("truncated boot buffers are rejected",bootmem_kb_mb2(f.data,f.size-1)==0,"");
    start(&f);wr32(f.data+8,4);wr32(f.data+12,0xffffffffu);f.size=16;
    report("oversized tags stay inside the supplied buffer",finish(&f)==0,"");
    start(&f);report("missing memory information stays unknown",finish(&f)==0 && !bootmem_kb_mb2(0,0) && !bootmem_kb_mb2(f.data,4),"");
}
