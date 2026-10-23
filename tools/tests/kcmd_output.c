/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"
typedef struct { int dummy; } app_proc_t;
#define KJ_OUT 4096
typedef struct { char out[KJ_OUT]; uint32_t oh, ot; int cancel; } kjob_t;
static int checks, limit, again, fail, sleeps;
static unsigned used;
static char output[8192];
#define CHECK(c) do {checks++;if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);exit(1);}}while(0)
static long fd_write(app_proc_t* p,int fd,const void* buf,unsigned long n) {
    (void)p;CHECK(fd==1);
    if(again){again=0;return -11;}
    if(fail)return -32;
    if(limit&&n>(unsigned)limit)n=(unsigned)limit;
    CHECK(used+n<=sizeof(output));memcpy(output+used,buf,n);used+=(unsigned)n;return (long)n;
}
void task_sleep_ms(uint32_t n) {CHECK(n==2);sleeps++;}
#include "kcmd_output.inc"
int main(void) {
    app_proc_t p={0};kjob_t j={0};char expected[4095];
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(char)(i*13);
    for(int wrapped=0;wrapped<2;wrapped++)for(int partial=0;partial<2;partial++) {
        memset(&j,0,sizeof(j));used=0;j.ot=j.oh=wrapped?3900:0;
        for(unsigned i=0;i<sizeof(expected);i++){j.out[j.oh]=expected[i];j.oh=(j.oh+1)%KJ_OUT;}
        limit=partial?17:0;again=1;kcmd_drain(&p,&j);
        CHECK(used==sizeof(expected)&&!memcmp(output,expected,used)&&j.ot==j.oh&&!j.cancel);
    }
    CHECK(sleeps==4);
    memset(&j,0,sizeof(j));used=0;j.out[0]='x';j.oh=1;fail=1;
    kcmd_drain(&p,&j);CHECK(j.cancel&&used==0);
    memset(&j,0,sizeof(j));j.cancel=1;j.oh=1;
    kcmd_drain(&p,&j);CHECK(j.ot==0);
    printf("kcmd output: %d checks passed\n",checks);return 0;
}
