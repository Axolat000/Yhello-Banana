#include "mouse_capture.h"
void mouse_capture_tests(void (*report)(const char*,int,const char*)) {
    mouse_capture_t c;mouse_capture_reset(&c);int dx,dy;mouse_state_t m={.dx=17,.dy=-9};
    mouse_capture_motion(&c,&m,1920,1080,&dx,&dy);
    report("uncaptured movement stays on the desktop",!dx && !dy,"");
    report("background windows cannot capture",mouse_capture_begin(&c,2,7,0)<0 && c.win<0,"");
    report("foreground window acquires mouse capture",!mouse_capture_begin(&c,2,7,1) && c.win==2,"");
    mouse_capture_motion(&c,&m,1920,1080,&dx,&dy);
    report("relative movement uses right/down coordinates",dx==17 && dy==9,"");
    m.dx=12000;m.dy=8000;mouse_capture_motion(&c,&m,1920,1080,&dx,&dy);
    report("relative movement continues beyond screen edges",dx==12000 && dy==-8000,"");
    report("another owner cannot release capture",mouse_capture_end(&c,2,8)<0 && c.owner==7,"");
    report("another window cannot steal capture",mouse_capture_begin(&c,3,8,1)<0 && c.win==2,"");
    report("repeated capture requests preserve state",!mouse_capture_begin(&c,2,7,1),"");
    m=(mouse_state_t){.abs=1,.ax=30000,.ay=30000};mouse_capture_motion(&c,&m,1920,1080,&dx,&dy);
    report("absolute pointer starts without a camera jump",!dx && !dy,"");
    m.ax=65535;m.ay=65535;mouse_capture_motion(&c,&m,1920,1080,&dx,&dy);
    report("absolute motion is converted to relative pixels",dx==1920-1-(int)((int64_t)30000*1919/65535) && dy==1080-1-(int)((int64_t)30000*1079/65535),"");
    mouse_capture_motion(&c,&m,1280,720,&dx,&dy);
    report("display size changes do not move the camera",!dx && !dy,"");
    m=(mouse_state_t){0};mouse_capture_motion(&c,&m,1280,720,&dx,&dy);
    report("idle frames do not repeat movement",!dx && !dy,"");
    report("coalesced motion preserves totals",mouse_capture_add(120,-37)==83,"");
    report("extreme coalesced motion saturates safely",mouse_capture_add(2147483647,1)==2147483647 && mouse_capture_add(-2147483647,-20)==(-2147483647-1),"");
    report("owner releases and clears capture",!mouse_capture_end(&c,2,7) && c.win<0 && !c.abs_valid,"");
    mouse_capture_begin(&c,3,8,1);mouse_capture_reset(&c);
    report("forced release clears all button and position state",c.win<0 && !c.buttons && !c.abs_valid,"");
}
