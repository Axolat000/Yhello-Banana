#include <stdio.h>
#include <stdlib.h>
#include "types.h"
#include "kheap.h"
#include "kstring.h"
#include "../../kernel/bootmenu_test.c"
static int checks,failed;
static void report(const char* label,int ok,const char* note) {
    checks++;failed+=!ok;printf("%s %s%s%s\n",ok?"ok  ":"FAIL",label,*note?" - ":"",note);
}
int main(int argc,char** argv) {
    if(argc==1) { bootmenu_tests_run(report);printf("Boot menu: %d checks, %d failed\n",checks,failed);return !!failed; }
    int w=argc>2?atoi(argv[2]):1024,h=argc>3?atoi(argv[3]):768;
    if(w<640 || w>8192 || h<480 || h>8192)return 1;
    unsigned char* pixels=calloc((size_t)w*h,4);if(!pixels)return 1;
    boot_ui_fb fb={pixels,w,h,w*4,16,8,0};
    boot_ui_menu menu={.count=9,.selected=argc>4?atoi(argv[4]):0,.seconds=argc>5?atoi(argv[5]):3,
        .firmware="UEFI / 64-bit",.status="Secure Boot: on",.video="Display: 1920 x 1080"};
    for(int i=0;i<5;i++)menu.labels[i]=boot_ui_kernel_label(i);
    menu.labels[5]="Boot the next device";menu.labels[6]="Firmware settings";menu.labels[7]="Enroll Secure Boot keys";menu.labels[8]="Restart computer";
    if(boot_ui_draw(&fb,&menu)) {free(pixels);return 1;}
    FILE* f=fopen(argv[1],"wb");if(!f){free(pixels);return 1;}
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int i=0;i<w*h;i++){uint32_t p=((uint32_t*)pixels)[i];unsigned char rgb[]={p>>16,p>>8,p};fwrite(rgb,1,3,f);}
    fclose(f);free(pixels);return 0;
}
