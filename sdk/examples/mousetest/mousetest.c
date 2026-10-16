#include <banana.h>
#include <math.h>
#include <stdio.h>
static float yaw,pitch;
static int project(bwin_t* w,float x,float y,float z,int* sx,int* sy) {
    float a=x*cosf(yaw)-z*sinf(yaw),b=x*sinf(yaw)+z*cosf(yaw);
    float yy=y*cosf(pitch)-b*sinf(pitch),zz=y*sinf(pitch)+b*cosf(pitch);
    if(zz<0.25f)return 0;
    float px=w->w*0.5f+a*w->w*0.65f/zz,py=w->h*0.5f-yy*w->w*0.65f/zz;
    if(px < -2000 || px > 2000+w->w || py < -2000 || py > 2000+w->h)return 0;
    *sx=(int)px;*sy=(int)py;return 1;
}
static void scene(bwin_t* w) {
    bwin_clear(w,0x101827);
    for(int cube=0;cube<8;cube++) {
        float angle=cube*(float)M_PI/4,ox=sinf(angle)*6,oz=cosf(angle)*6;
        int xs[8],ys[8],visible[8];
        for(int i=0;i<8;i++)visible[i]=project(w,ox+((i&1)?1:-1),((i&2)?1:-1),oz+((i&4)?1:-1),&xs[i],&ys[i]);
        for(int i=0;i<8;i++)for(int bit=1;bit<=4;bit*=2)if(!(i&bit) && visible[i] && visible[i|bit])
            bwin_line(w,xs[i],ys[i],xs[i|bit],ys[i|bit],cube&1?0x65ddbe:0x73aaff);
    }
    bwin_line(w,w->w/2-9,w->h/2,w->w/2+9,w->h/2,0xffffff);
    bwin_line(w,w->w/2,w->h/2-9,w->w/2,w->h/2+9,0xffffff);
}
int main(void) {
    if(!BANANA_HAS(banana_api(),win_capture_mouse)) { puts("Mouse capture needs API version 20.");return 1; }
    bwin_t w;if(bwin_open(&w,"Mouse capture - click to look, Escape releases, Q quits",640,420))return 1;
    bwin_resizable(&w,400,260);
    int run=1,locked=0,buttons=0,wheel=0;long long total_x=0,total_y=0;
    while(run) {
        banana_event_t ev;
        while(bwin_event(&w,&ev)) {
            if(ev.type==BANANA_EV_CLOSE || (ev.type==BANANA_EV_KEY && (ev.key=='q' || ev.key=='Q')))run=0;
            if(ev.type==BANANA_EV_MOUSE_DOWN && ev.button==1 && !locked) {
                if(!bwin_capture_mouse(&w,1))locked=1;
            }
            if(ev.type==BANANA_EV_CAPTURE) { locked=ev.x;buttons=ev.buttons; }
            if(ev.type==BANANA_EV_MOUSE_DOWN || ev.type==BANANA_EV_MOUSE_UP)buttons=ev.buttons;
            if(ev.type==BANANA_EV_WHEEL)wheel+=ev.y;
            if(ev.type==BANANA_EV_MOUSE_REL) {
                total_x+=ev.x;total_y+=ev.y;buttons=ev.buttons;
                yaw=(float)fmod(yaw+ev.x*0.003,2*M_PI);pitch-=ev.y*0.003f;
                if(pitch>1.2f)pitch=1.2f;
                if(pitch < -1.2f)pitch=-1.2f;
            }
        }
        /* Query also covers a lost event if the app was not polling in time. */
        if(bwin_mouse_captured(&w)!=1) { locked=0;buttons=0; }
        scene(&w);char text[128];
        bwin_fill_rect(&w,0,0,w.w,62,0x1b2940);
        bwin_text(&w,14,12,locked?"CAPTURED - move freely; Escape releases":"RELEASED - click to capture; Q quits",0xffffff,BANANA_TRANSPARENT);
        snprintf(text,sizeof(text),"total dx=%lld dy=%lld   buttons=%d   wheel=%d",total_x,total_y,buttons,wheel);
        bwin_text(&w,14,34,text,0x9cc1ef,BANANA_TRANSPARENT);
        bwin_update(&w);banana_sleep(16);
    }
    bwin_capture_mouse(&w,0);bwin_close(&w);return 0;
}
