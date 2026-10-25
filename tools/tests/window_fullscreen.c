/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../../kernel/winframe.h"
#include "../../kernel/fb.h"
static fb_info_t screen={.width=1024,.height=768};
const fb_info_t* fb_info(void){return &screen;}
uint32_t timer_ms(void){return 1000;}
int main(void){
 win_geom_t g={.x=80,.y=60,.w=640,.h=480,.min_w=200,.min_h=120};
 for(int mode=0;mode<3;mode++){
  g.maxed=mode==1;g.snapped=mode==2;g.sx=45;g.sy=35;g.sw=400;g.sh=300;
  win_geom_t before=g;
  win_toggle_fullscreen(&g);
  assert(g.fullscreen&&g.x+3==0&&g.y+23==0);
  assert(g.w-6==1024&&g.h-26==768);
  assert(!win_grip_press(&g,1023,767)&&!win_title_press(&g,10,2));
  assert(!win_mouse(&g,500,500,1));
  win_toggle_fullscreen(&g);
  assert(!g.fullscreen&&g.x==before.x&&g.y==before.y&&g.w==before.w&&g.h==before.h);
  assert(g.maxed==before.maxed&&g.snapped==before.snapped&&g.sw==400&&g.sh==300);
 }
 win_toggle_fullscreen(&g);screen.width=800;screen.height=600;win_clamp(&g);
 assert(g.w==806&&g.h==626);win_toggle_fullscreen(&g);
 assert(g.x>=0&&g.y>=0&&g.x+g.w<=800&&g.y+g.h<=572);
 puts("PASS: fullscreen normal/maximized/snapped restore and resolution clamping");
}
