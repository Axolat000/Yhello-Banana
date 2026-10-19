/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Shared BIOS/UEFI boot menu. No allocator, firmware calls, floating point or
 * kernel dependencies. All drawing clips to a validated 32-bit framebuffer. */
#ifndef BOOT_UI_H
#define BOOT_UI_H
#include "boot_font.h"
#define BOOT_UI_MAX_ENTRIES 9
#define BOOT_BG 0x10141cu
#define BOOT_PANEL 0x171d28u
#define BOOT_SURFACE 0x202938u
#define BOOT_TEXT 0xf4f6fau
#define BOOT_MUTED 0xb0bacbu
#define BOOT_ACCENT 0xf7d154u
typedef struct {
    volatile unsigned char* pixels;
    int w,h,pitch,rp,gp,bp;
} boot_ui_fb;
typedef struct {
    const char* labels[BOOT_UI_MAX_ENTRIES];
    const char* firmware;
    const char* status;
    const char* video;
    int count,selected,seconds;
} boot_ui_menu;
typedef struct { int x,y,w,h,row,top,footer,compact; } boot_ui_layout;
/* Labels/firmware/status are immutable while the menu is open. Video is a
 * caller-owned temporary string, so keep our own copy. Reset valid on entry. */
typedef struct { boot_ui_fb fb; boot_ui_menu menu; char video[80]; int valid; } boot_ui_state;
static int boot_ui_valid(const boot_ui_fb* f) {
    return f && f->pixels && !((unsigned long)f->pixels&3) && f->w>=640 && f->h>=480 && f->w<=8192 && f->h<=8192 &&
        f->pitch>=f->w*4 && f->pitch<=65536 && !(f->pitch&3) &&
        f->rp>=0 && f->rp<=24 && f->gp>=0 && f->gp<=24 && f->bp>=0 && f->bp<=24 &&
        !(f->rp&7) && !(f->gp&7) && !(f->bp&7) && f->rp!=f->gp && f->rp!=f->bp && f->gp!=f->bp;
}
static const char* boot_ui_kernel_label(int i) {
    switch(i) {
    case 0:return "Start Banana OS (64-bit)";
    case 1:return "Start with one CPU core (64-bit)";
    case 2:return "Start Banana OS (32-bit)";
    case 3:return "Show boot messages (64-bit)";
    default:return "Show boot messages (32-bit)";
    }
}
static unsigned int boot_ui_pack(const boot_ui_fb* f,unsigned int rgb) {
    return ((rgb>>16)&255)<<f->rp | ((rgb>>8)&255)<<f->gp | (rgb&255)<<f->bp;
}
static void boot_ui_rect(const boot_ui_fb* f,int x,int y,int w,int h,unsigned int color) {
    if(x<0){w+=x;x=0;}if(y<0){h+=y;y=0;}
    if(x>=f->w || y>=f->h || w<=0 || h<=0)return;
    if(w>f->w-x)w=f->w-x;
    if(h>f->h-y)h=f->h-y;
    color=boot_ui_pack(f,color);
    for(int yy=0;yy<h;yy++) {
        volatile unsigned int* row=(volatile unsigned int*)(f->pixels+(unsigned long)(y+yy)*f->pitch)+x;
        for(int xx=0;xx<w;xx++)row[xx]=color;
    }
}
static unsigned int boot_ui_mix(unsigned int a,unsigned int b,int coverage) {
    unsigned int r=(((a>>16)&255)*(3-coverage)+((b>>16)&255)*coverage)/3;
    unsigned int g=(((a>>8)&255)*(3-coverage)+((b>>8)&255)*coverage)/3;
    unsigned int bl=((a&255)*(3-coverage)+(b&255)*coverage)/3;
    return (r<<16)|(g<<8)|bl;
}
static void boot_ui_text(const boot_ui_fb* f,int x,int y,const char* s,int scale,int max_width,unsigned int color,unsigned int bg) {
    int start=x;if(!s)return;
    for(;*s;s++) {
        unsigned int ch=(unsigned char)*s;if(ch<32 || ch>126)ch='?';
        const boot_glyph_t* g=&boot_glyphs[ch-32];
        if(x+g->advance*scale>start+max_width)break;
        for(int yy=0;yy<g->h;yy++) {
            int gy=y+(yy+g->y+16)*scale;
            if(gy>=f->h || gy+scale<=0)continue;
            for(int xx=0;xx<g->w;xx++) {
                unsigned int i=(unsigned int)yy*g->w+xx;
                int a=(boot_font_bits[g->offset+i/4]>>((i%4)*2))&3;
                if(a)boot_ui_rect(f,x+(xx+g->x)*scale,gy,scale,scale,boot_ui_mix(bg,color,a));
            }
        }
        x+=g->advance*scale;
    }
}
static boot_ui_layout boot_ui_geometry(const boot_ui_fb* f,int count) {
    boot_ui_layout l; l.compact=f->h<650;
    l.w=f->w-64;if(l.w>760)l.w=760;l.x=(f->w-l.w)/2;
    l.row=l.compact?27:44;l.top=l.compact?84:116;
    l.footer=l.top+count*l.row+16;l.h=l.footer+100;
    l.y=(f->h-l.h)/2;if(l.y<12)l.y=12;
    return l;
}
static void boot_ui_logo(const boot_ui_fb* f,int x,int y) {
    /* A small banana crescent, drawn from circles rather than a bitmap asset. */
    for(int yy=0;yy<40;yy++)for(int xx=0;xx<40;xx++) {
        int a=xx-18,b=yy-16,c=xx-24,d=yy-10;
        if(a*a+b*b<324 && c*c+d*d>289)boot_ui_rect(f,x+xx,y+yy,1,1,BOOT_ACCENT);
    }
}
static void boot_ui_wordmark(const boot_ui_fb* f,int x,int y) {
    for(int yy=0;yy<BOOT_WORDMARK_H;yy++)for(int xx=0;xx<BOOT_WORDMARK_W;xx++) {
        unsigned int i=(unsigned int)yy*BOOT_WORDMARK_W+xx;
        int a=(boot_wordmark_bits[i/4]>>((i%4)*2))&3;
        if(a)boot_ui_rect(f,x+xx,y+yy,1,1,boot_ui_mix(BOOT_BG,BOOT_TEXT,a));
    }
}
static int boot_ui_menu_valid(const boot_ui_fb* f,const boot_ui_menu* m) {
    return boot_ui_valid(f) && m && m->count>=1 && m->count<=BOOT_UI_MAX_ENTRIES && m->selected>=0 && m->selected<m->count;
}
static void boot_ui_option(const boot_ui_fb* f,const boot_ui_menu* m,int i,int x,int y,int w,int h) {
    int selected=i==m->selected;unsigned int bg=selected?BOOT_ACCENT:BOOT_PANEL;
    unsigned int fg=selected?BOOT_PANEL:BOOT_TEXT;
    boot_ui_rect(f,x,y,w,h,bg);
    if(selected)boot_ui_text(f,x+12,y+(h-20)/2,">",1,16,fg,bg);
    boot_ui_text(f,x+38,y+(h-20)/2,m->labels[i],1,w-56,fg,bg);
}
static void boot_ui_video(const boot_ui_fb* f,const boot_ui_menu* m,int x,int y,int w) {
    boot_ui_rect(f,x,y,w,24,BOOT_BG);
    boot_ui_text(f,x,y,m->video,1,w,BOOT_MUTED,BOOT_BG);
}
static void boot_ui_countdown(const boot_ui_fb* f,const boot_ui_menu* m,int x,int y,int w) {
    boot_ui_rect(f,x,y,w,34,BOOT_BG);
    if(m->seconds>=0) {
        char countdown[]="Starting in 0 seconds - any key pauses";
        countdown[12]=(char)('0'+(m->seconds>9?9:m->seconds));
        boot_ui_text(f,x,y,countdown,1,w,BOOT_TEXT,BOOT_BG);
        int bars=m->seconds>3?3:m->seconds;
        boot_ui_rect(f,x,y+30,w,3,BOOT_SURFACE);
        boot_ui_rect(f,x,y+30,w*bars/3,3,BOOT_ACCENT);
    } else boot_ui_text(f,x,y,"Automatic start paused",1,w,BOOT_TEXT,BOOT_BG);
}
static int boot_ui_draw(const boot_ui_fb* f,const boot_ui_menu* m) {
    if(!boot_ui_menu_valid(f,m))return -1;
    boot_ui_layout l=boot_ui_geometry(f,m->count);
    boot_ui_rect(f,0,0,f->w,f->h,BOOT_BG);
    boot_ui_logo(f,l.x,l.y+3);
    boot_ui_wordmark(f,l.x+52,l.y+6);
    boot_ui_text(f,l.x+2,l.y+49,"Choose how to start",1,l.w,BOOT_MUTED,BOOT_BG);
    if(!l.compact)boot_ui_text(f,l.x+2,l.y+76,m->firmware,1,l.w/2,BOOT_MUTED,BOOT_BG);
    int ry=l.y+l.top;
    for(int i=0;i<m->count;i++) {
        boot_ui_option(f,m,i,l.x,ry+i*l.row,l.w,l.row-4);
    }
    int fy=l.y+l.footer;
    boot_ui_video(f,m,l.x,fy,l.w);
    boot_ui_text(f,l.x,fy+24,"Up/Down  Choose    Left/Right  Display    Enter  Start",1,l.w,BOOT_MUTED,BOOT_BG);
    boot_ui_countdown(f,m,l.x,fy+50,l.w);
    if(m->status && !l.compact)boot_ui_text(f,l.x+l.w/2,l.y+76,m->status,1,l.w/2,BOOT_MUTED,BOOT_BG);
    return 0;
}
/* Compose each changed scanline completely before it reaches video memory.
 * A 3 KiB line buffer also fits the BIOS loader's small stack/address space. */
static void boot_ui_band(const boot_ui_fb* f,const boot_ui_menu* m,boot_ui_layout l,int which) {
    unsigned int pixels[760];
    boot_ui_fb line={(volatile unsigned char*)pixels,l.w,1,l.w*4,f->rp,f->gp,f->bp};
    int y=l.y+l.footer,h=24;
    if(which>=0){y=l.y+l.top+which*l.row;h=l.row-4;}
    else if(which==-2){y+=50;h=34;}
    for(int yy=0;yy<h;yy++) {
        if(which>=0)boot_ui_option(&line,m,which,0,-yy,l.w,h);
        else if(which==-1)boot_ui_video(&line,m,0,-yy,l.w);
        else boot_ui_countdown(&line,m,0,-yy,l.w);
        if(y+yy<0 || y+yy>=f->h)continue;
        volatile unsigned int* row=(volatile unsigned int*)(f->pixels+(unsigned long)(y+yy)*f->pitch)+l.x;
        for(int x=0;x<l.w;x++)row[x]=pixels[x];
    }
}
static int boot_ui_same_text(const char* a,const char* b) {
    if(!a)a="";
    if(!b)b="";
    while(*a && *a==*b){a++;b++;}return *a==*b;
}
static int boot_ui_update(const boot_ui_fb* f,const boot_ui_menu* m,boot_ui_state* s) {
    if(!s)return -1;
    if(!boot_ui_menu_valid(f,m)){s->valid=0;return -1;}
    boot_ui_fb* old=&s->fb;
    if(!s->valid || old->pixels!=f->pixels || old->w!=f->w || old->h!=f->h || old->pitch!=f->pitch ||
       old->rp!=f->rp || old->gp!=f->gp || old->bp!=f->bp || s->menu.count!=m->count ||
       s->menu.firmware!=m->firmware || s->menu.status!=m->status)boot_ui_draw(f,m);
    else {
        boot_ui_layout l=boot_ui_geometry(f,m->count);
        for(int i=0;i<m->count;i++)
            if(s->menu.labels[i]!=m->labels[i] || (s->menu.selected!=m->selected && (i==m->selected || i==s->menu.selected)))
                boot_ui_band(f,m,l,i);
        if(!boot_ui_same_text(s->video,m->video))boot_ui_band(f,m,l,-1);
        if(s->menu.seconds!=m->seconds)boot_ui_band(f,m,l,-2);
    }
    s->fb=*f;s->menu=*m;s->valid=1;
    const char* v=m->video;unsigned int i=0;
    if(v)for(;i<sizeof(s->video)-1 && v[i];i++)s->video[i]=v[i];
    s->video[i]=0;s->menu.video=s->video;
    return 0;
}
#endif
