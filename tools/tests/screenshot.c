/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "screenshot.h"
#include "rtc.h"
static int checks, live, allocs, fail_at;
#define CHECK(c) do { checks++; if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); exit(1); } } while (0)
void* kmalloc(size_t n) { if (++allocs == fail_at) return NULL; void* p=malloc(n); if(p)live++; return p; }
void* krealloc(void* p,size_t n) { if (++allocs == fail_at) return NULL; void* q=realloc(p,n); if(q&&!p)live++; return q; }
void kfree(void* p) { if(p){CHECK(live>0);live--;free(p);} }
size_t kstrlcpy(char* d,const char* s,size_t n) { size_t z=strlen(s);if(n){size_t k=z<n-1?z:n-1;memcpy(d,s,k);d[k]=0;}return z; }
int ksnprintf(char* b,size_t n,const char* f,...) {va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);return r;}
static uint32_t pixels[6]={0x00ff0000,0x0000ff00,0xdeadbeef,0x000000ff,0x00ffffff,0xdeadbeef};
static int graphics=1, mkdir_failure, write_failure, collisions, date_ok=1, writes;
static char saved_path[512];
static uint8_t saved_png[65536];
static uint32_t saved_len;
uint32_t* fb_target(int* s,int* w,int* h) {*s=3;*w=2;*h=2;return graphics?pixels:NULL;}
int rtc_read_datetime(rtc_datetime_t* d) {*d=(rtc_datetime_t){.year=2026,.month=10,.day=23};return date_ok?0:-1;}
int fs_mkdir_p(const char* p) {CHECK(p&&p[0]=='/');return mkdir_failure?-1:0;}
int fs_find_file(const char* p) {const char* n=strrchr(p,'-');unsigned k=n?(unsigned)atoi(n+1):0;return (k&&k<=(unsigned)collisions)||!strcmp(p,saved_path)?0:-1;}
int fs_find_dir(const char* p) {(void)p;return -1;}
int fs_write_path(const char* p,const void* b,uint32_t n) {
    writes++;if(write_failure)return -1;
    CHECK(n<sizeof(saved_png));kstrlcpy(saved_path,p,sizeof(saved_path));memcpy(saved_png,b,n);saved_len=n;return 0;
}
int main(void) {
    uint32_t len=77; uint8_t* png;
    CHECK(!screenshot_encode_png(NULL,1,1,4,1,&len)&&len==0);
    CHECK(!screenshot_encode_png((void*)pixels,2,2,3,1,&len));
    CHECK(!screenshot_encode_png((void*)pixels,0x7fffffff,0x7fffffff,12,1,&len));
    CHECK(!screenshot_encode_png((void*)pixels,2,2,-1,1,&len));
    for(int n=1;n<=25;n++) {
        allocs=0;fail_at=n;
        png=screenshot_encode_png((void*)pixels,2,2,12,1,&len);
        if(png)kfree(png);
        CHECK(live==0);
    }
    fail_at=0;
    CHECK(screenshot_set_directory("relative")==-1);
    char huge[512];memset(huge,'a',sizeof(huge));huge[0]='/';huge[511]=0;
    CHECK(screenshot_set_directory(huge)==-1);
    CHECK(screenshot_set_directory("/pictures/")==0);
    CHECK(!strcmp(screenshot_get_directory(NULL,0),"/pictures"));
    char path[512]; CHECK(screenshot_take(NULL,path,sizeof(path))==0);
    CHECK(!strcmp(path,"/pictures/Screenshot-20261023-0001.png"));
    FILE* f=fopen("build/test/screenshot/capture.png","wb");CHECK(f);CHECK(fwrite(saved_png,1,saved_len,f)==saved_len);fclose(f);
    CHECK(screenshot_take(NULL,path,sizeof(path))==0 && strstr(path,"0002.png"));
    collisions=9999;int old=writes;CHECK(screenshot_take(NULL,path,sizeof(path))==-1&&writes==old&&path[0]==0);
    collisions=0;date_ok=0; CHECK(screenshot_take(NULL,path,sizeof(path))==0&&strstr(path,"Screenshot-0001.png"));
    mkdir_failure=1;CHECK(screenshot_take(NULL,path,sizeof(path))==-1);mkdir_failure=0;
    write_failure=1;CHECK(screenshot_take(NULL,path,sizeof(path))==-1&&path[0]==0);write_failure=0;
    graphics=0;CHECK(screenshot_take(NULL,path,sizeof(path))==-1);
    CHECK(live==0);
    printf("screenshot: %d checks passed\n",checks);return 0;
}
