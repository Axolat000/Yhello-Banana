/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "bootmenu_test.h"
#include "kheap.h"
#include "kstring.h"
#include "../loader/boot_ui.h"
static int guards(const uint8_t* mem,int pitch,int height) {
    for(int i=0;i<32;i++)if(mem[i]!=0xa5 || mem[32+(size_t)pitch*height+i]!=0xa5)return 0;
    for(int y=0;y<height;y++)for(int i=640*4;i<pitch;i++)if(mem[32+(size_t)y*pitch+i]!=0xa5)return 0;
    return 1;
}
static void updates(int width,int height,void (*report)(const char*,int,const char*)) {
    int pitch=width*4+16;size_t size=(size_t)pitch*height+64;
    uint8_t* pixels=kmalloc(size);uint8_t* reference=kmalloc(size);
    if(!pixels || !reference){report("boot menu update buffers",0,"not enough memory");kfree(pixels);kfree(reference);return;}
    memset(pixels,0xa5,size);memset(reference,0xa5,size);
    boot_ui_fb fb={pixels+32,width,height,pitch,16,8,0},ref=fb;ref.pixels=reference+32;
    char video[80]="Display: 1920 x 1080";
    const char* note=width==640?"640x480":"1024x768";
    boot_ui_menu m={.count=9,.selected=0,.seconds=3,.firmware="UEFI / 64-bit",.status="Secure Boot: off",.video=video};
    for(int i=0;i<9;i++)m.labels[i]=i<5?boot_ui_kernel_label(i):"Firmware action";
    boot_ui_state state={0};
    boot_ui_update(&fb,&m,&state);boot_ui_draw(&ref,&m);
    report("first menu update draws a complete frame",!memcmp(pixels,reference,size),note);
    boot_ui_layout l=boot_ui_geometry(&fb,m.count);
    uint32_t* header=(uint32_t*)fb.pixels;
    uint32_t* untouched=(uint32_t*)(fb.pixels+(size_t)(l.y+l.top+4*l.row+1)*pitch)+l.x+1;
    *header=0x123456;*untouched=0x123456;
    m.selected=1;m.seconds=-1;boot_ui_update(&fb,&m,&state);
    report("selection updates leave the header and other options untouched",*header==0x123456 && *untouched==0x123456,note);
    *header=BOOT_BG;*untouched=BOOT_PANEL;boot_ui_draw(&ref,&m);
    report("buffered selection and pause match a complete frame",!memcmp(pixels,reference,size),note);
    uint32_t* selected=(uint32_t*)(fb.pixels+(size_t)(l.y+l.top+l.row+1)*pitch)+l.x+1;
    *selected=0x123456;boot_ui_update(&fb,&m,&state);
    report("unchanged input does not repaint the menu",*selected==0x123456,note);
    m.seconds=2;boot_ui_update(&fb,&m,&state);
    report("countdown updates leave boot options untouched",*selected==0x123456,note);
    *selected=BOOT_ACCENT;boot_ui_draw(&ref,&m);
    report("countdown updates match a complete frame",!memcmp(pixels,reference,size),note);
    kstrlcpy(video,"Display: auto",sizeof(video));
    boot_ui_update(&fb,&m,&state);boot_ui_draw(&ref,&m);
    report("display changes replace reused temporary text without stale pixels",!memcmp(pixels,reference,size),note);
    m.labels[3]="A different boot option";boot_ui_update(&fb,&m,&state);boot_ui_draw(&ref,&m);
    report("changed option labels repaint correctly",!memcmp(pixels,reference,size),note);
    fb.rp=ref.rp=0;fb.bp=ref.bp=16;
    boot_ui_update(&fb,&m,&state);boot_ui_draw(&ref,&m);
    report("framebuffer format changes invalidate the cached frame",!memcmp(pixels,reference,size),note);
    m.count=7;boot_ui_update(&fb,&m,&state);boot_ui_draw(&ref,&m);
    report("menu geometry changes clear the previous layout",!memcmp(pixels,reference,size),note);
    fb.w=320;int rejected=boot_ui_update(&fb,&m,&state)<0 && !state.valid;
    fb.w=width;*header=0x123456;boot_ui_update(&fb,&m,&state);
    report("fallback invalidates the cache before graphics resumes",rejected && !memcmp(pixels,reference,size),note);
    kfree(reference);kfree(pixels);
}
void bootmenu_tests_run(void (*report)(const char*,int,const char*)) {
    int pitch=640*4+16,height=480;size_t size=(size_t)pitch*height+64;
    uint8_t* memory=kmalloc(size);
    if(!memory){report("boot menu test buffer",0,"not enough memory");return;}
    memset(memory,0xa5,size);
    boot_ui_fb fb={memory+32,640,height,pitch,16,8,0};
    boot_ui_menu menu={.count=9,.selected=0,.seconds=3,.firmware="UEFI / 64-bit",
        .status="Secure Boot: setup",.video="Display: 1920 x 1080"};
    for(int i=0;i<5;i++)menu.labels[i]=boot_ui_kernel_label(i);
    menu.labels[5]="Boot next device";menu.labels[6]="Firmware settings";
    menu.labels[7]="Enroll Secure Boot keys";menu.labels[8]="Restart computer";
    report("graphical boot menu at 640x480",!boot_ui_draw(&fb,&menu),"");
    boot_ui_layout l=boot_ui_geometry(&fb,menu.count);
    uint32_t* first=(uint32_t*)(fb.pixels+(size_t)(l.y+l.top+1)*pitch)+l.x+1;
    uint32_t* last=(uint32_t*)(fb.pixels+(size_t)(l.y+l.top+8*l.row+1)*pitch)+l.x+1;
    report("selection is a visible full-row highlight",*first==BOOT_ACCENT && *last==BOOT_PANEL,"");
    report("all boot choices and hints fit the minimum screen",l.x>=0 && l.x+l.w<=fb.w && l.y+l.footer+84<=fb.h,"");
    report("framebuffer stride and guard bytes stay intact",guards(memory,pitch,height),"");
    menu.selected=8;menu.seconds=-1;boot_ui_draw(&fb,&menu);
    report("last firmware action can be highlighted",*first==BOOT_PANEL && *last==BOOT_ACCENT,"");
    uint32_t* bar=(uint32_t*)(fb.pixels+(size_t)(l.y+l.footer+80)*pitch)+l.x;
    report("paused countdown clears the old progress bar",*bar==BOOT_BG,"");
    menu.seconds=0;boot_ui_draw(&fb,&menu);
    report("completed countdown leaves an empty progress track",*bar==BOOT_SURFACE,"");
    int good=1;
    for(unsigned int i=0;i<95;i++) { const boot_glyph_t* g=&boot_glyphs[i];if(!g->advance || g->offset+((unsigned int)g->w*g->h+3)/4>sizeof(boot_font_bits))good=0; }
    report("embedded antialiased font has valid glyph bounds",good,"");
    fb.rp=0;fb.bp=16;boot_ui_draw(&fb,&menu);
    report("RGB and BGR framebuffers receive the right colors",*(uint32_t*)fb.pixels==boot_ui_pack(&fb,BOOT_BG) && *last==boot_ui_pack(&fb,BOOT_ACCENT),"");
    fb.rp=fb.gp;
    report("unsupported color layouts use the text fallback",boot_ui_draw(&fb,&menu)<0,"");
    fb.rp=16;fb.bp=0;fb.w=320;
    report("small firmware screens use the text fallback",boot_ui_draw(&fb,&menu)<0,"");fb.w=640;
    menu.count=10;report("invalid entry counts are rejected",boot_ui_draw(&fb,&menu)<0,"");menu.count=9;
    menu.selected=9;report("invalid selections are rejected",boot_ui_draw(&fb,&menu)<0,"");
    boot_ui_rect(&fb,-15,-8,30,16,BOOT_ACCENT);
    report("clipped drawing stays inside the framebuffer",*(uint32_t*)fb.pixels==BOOT_ACCENT && guards(memory,pitch,height),"");
    fb.pixels++;report("unaligned framebuffers use the text fallback",!boot_ui_valid(&fb),"");
    kfree(memory);
    updates(640,480,report);
    updates(1024,768,report);
}
