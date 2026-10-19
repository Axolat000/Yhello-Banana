/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "acpi_sleep.h"
#include "bootmem.h"
static void put32(uint8_t* p,uint32_t n) { for(int i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
static void put64(uint8_t* p,uint64_t n) { put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32)); }
static void wake_map(uint8_t p[80]) {
    for(int i=0;i<80;i++)p[i]=0;
    put32(p,80);put32(p+8,6);put32(p+12,64);put32(p+16,24);
    put64(p+32,0x90000);put32(p+40,1);
    put64(p+48,0x90000);put64(p+56,0x10000);put32(p+64,2);put32(p+76,8);
}
void acpi_sleep_tests(void (*report)(const char*,int,const char*)) {
    uint16_t a,b;
    uint8_t s3[]={8,0x5c,'_','S','3','_',0x12,6,4,1,1,0,0};
    report("root-qualified S3 package",acpi_sleep_types(s3,sizeof(s3),"_S3_",&a,&b) && a==1 && b==1,"");
    uint8_t plain[]={8,'_','S','3','_',0x12,4,2,0,1};
    report("unqualified S3 package",acpi_sleep_types(plain,sizeof(plain),"_S3_",&a,&b) && !a && b==1,"");
    uint8_t parent[]={8,0x5e,'_','S','3','_',0x12,6,2,0x0a,3,0x0a,4};
    report("parent-qualified byte constants",acpi_sleep_types(parent,sizeof(parent),"_S3_",&a,&b) && a==3 && b==4,"");
    uint8_t word[]={8,'_','S','3','_',0x12,8,2,0x0b,5,0,0x0b,6,0};
    report("word constants",acpi_sleep_types(word,sizeof(word),"_S3_",&a,&b) && a==5 && b==6,"");
    uint8_t wide[]={8,'_','S','3','_',0x12,16,2,0x0c,7,0,0,0,0x0e,2,0,0,0,0,0,0,0};
    report("dword and qword constants",acpi_sleep_types(wide,sizeof(wide),"_S3_",&a,&b) && a==7 && b==2,"");
    uint8_t multi[]={8,'_','S','3','_',0x12,0x45,0,2,1,1};
    report("multi-byte package length",acpi_sleep_types(multi,sizeof(multi),"_S3_",&a,&b) && a==1 && b==1,"");
    s3[4]='5';
    report("S5 package uses the same bounded parser",acpi_sleep_types(s3,sizeof(s3),"_S5_",&a,&b),"");
    report("S5 is not mistaken for S3",!acpi_sleep_types(s3,sizeof(s3),"_S3_",&a,&b),"");s3[4]='3';
    int ok=1;for(unsigned i=0;i<sizeof(s3);i++)if(acpi_sleep_types(s3,i,"_S3_",&a,&b))ok=0;
    report("truncated packages are rejected at every boundary",ok,"");
    uint8_t missing[]={ '_','S','3','_',0x12,4,2,1,1 };
    report("a sleep name without NameOp is rejected",!acpi_sleep_types(missing,sizeof(missing),"_S3_",&a,&b),"");
    plain[6]=0xff;report("oversized package is rejected",!acpi_sleep_types(plain,sizeof(plain),"_S3_",&a,&b),"");plain[6]=4;
    plain[7]=1;report("both sleep-type elements are required",!acpi_sleep_types(plain,sizeof(plain),"_S3_",&a,&b),"");plain[7]=2;
    plain[8]=0xff;report("unsupported AML expressions are rejected",!acpi_sleep_types(plain,sizeof(plain),"_S3_",&a,&b),"");
    parent[10]=8;report("sleep types outside 0..7 are rejected",!acpi_sleep_types(parent,sizeof(parent),"_S3_",&a,&b),"");
    wide[20]=1;report("large qword values do not truncate to a sleep type",!acpi_sleep_types(wide,sizeof(wide),"_S3_",&a,&b),"");
    report("empty buffers are rejected",!acpi_sleep_types(0,0,"_S3_",&a,&b) && !acpi_sleep_types(s3,0,"_S3_",&a,&b),"");
    uint8_t m[80];wake_map(m);
    report("wake code uses an available aligned conventional RAM page",bootmem_wake_page_mb2(m,sizeof(m))==0x8f000,"");
    put32(m+40,4);put64(m+48,0x20000);put64(m+56,0x10000);put32(m+64,1);
    report("firmware NVS pages are not borrowed for wake code",bootmem_wake_page_mb2(m,sizeof(m))==0x2f000,"");
    put64(m+48,0x2f800);put64(m+56,4096);
    report("partially available pages are rejected",!bootmem_wake_page_mb2(m,sizeof(m)),"");
    put64(m+48,1ull<<32);put64(m+56,0x10000);
    report("high RAM cannot be a real-mode waking vector",!bootmem_wake_page_mb2(m,sizeof(m)),"");
    wake_map(m);put32(m+8,4);put32(m+12,16);
    report("legacy memory counts do not authorize a wake page",!bootmem_wake_page_mb2(m,sizeof(m)),"");
    wake_map(m);report("truncated memory maps cannot supply wake pages",!bootmem_wake_page_mb2(m,sizeof(m)-1),"");
    put32(m+16,0);report("invalid map strides cannot supply wake pages",!bootmem_wake_page_mb2(m,sizeof(m)),"");
    wake_map(m);put64(m+24,~(uint64_t)0-4095);put64(m+32,8192);
    report("overflowing physical ranges cannot supply wake pages",!bootmem_wake_page_mb2(m,sizeof(m)),"");
}
