#ifndef MOUSE_CAPTURE_H
#define MOUSE_CAPTURE_H
#include "usb.h"
typedef struct {
    int win, owner, buttons;
    int abs_valid, ax, ay, screen_w, screen_h;
} mouse_capture_t;
void mouse_capture_reset(mouse_capture_t* c);
int mouse_capture_begin(mouse_capture_t* c,int win,int owner,int foreground);
int mouse_capture_end(mouse_capture_t* c,int win,int owner);
void mouse_capture_motion(mouse_capture_t* c,const mouse_state_t* m,int width,int height,int* dx,int* dy);
int mouse_capture_add(int a,int b); /* saturating coalescing, never signed overflow */
void mouse_capture_tests(void (*report)(const char*,int,const char*));
#endif
