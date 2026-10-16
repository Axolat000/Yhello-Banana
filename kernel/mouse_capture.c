#include "mouse_capture.h"
#include "kstring.h"
void mouse_capture_reset(mouse_capture_t* c) { memset(c,0,sizeof(*c));c->win=c->owner=-1; }
int mouse_capture_begin(mouse_capture_t* c,int win,int owner,int foreground) {
    if(win<0 || owner<0 || !foreground)return -1;
    if(c->win>=0)return c->win==win && c->owner==owner?0:-1;
    mouse_capture_reset(c);c->win=win;c->owner=owner;return 0;
}
int mouse_capture_end(mouse_capture_t* c,int win,int owner) {
    if(c->win!=win || c->owner!=owner)return -1;
    mouse_capture_reset(c);return 0;
}
int mouse_capture_add(int a,int b) {
    int64_t sum=(int64_t)a+b;
    return sum>2147483647?2147483647:sum<(-2147483647-1)?(-2147483647-1):(int)sum;
}
void mouse_capture_motion(mouse_capture_t* c,const mouse_state_t* m,int width,int height,int* dx,int* dy) {
    *dx=*dy=0;if(c->win<0)return;
    *dx=m->dx;*dy=mouse_capture_add(0,m->dy==(-2147483647-1)?2147483647:-m->dy);
    if(m->abs && width>0 && height>0) {
        int ax=m->ax<0?0:m->ax>65535?65535:m->ax,ay=m->ay<0?0:m->ay>65535?65535:m->ay;
        int x=(int)((int64_t)ax*(width-1)/65535),y=(int)((int64_t)ay*(height-1)/65535);
        if(c->abs_valid && c->screen_w==width && c->screen_h==height) {
            *dx=mouse_capture_add(*dx,x-c->ax);*dy=mouse_capture_add(*dy,y-c->ay);
        }
        c->ax=x;c->ay=y;c->screen_w=width;c->screen_h=height;c->abs_valid=1;
    } else if(m->dx || m->dy)c->abs_valid=0;
}
