/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layout.h"
#include "render.h"
#define W 200
#define H 80
static int checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
int main(void){
    dom_node_t base={0},button={0},frame={0};
    base.type=button.type=frame.type=DOM_ELEM;
    base.tag="body";button.tag="button";frame.tag="div";
    dl_item_t items[3]={{0}};layout_t layout={0};
    layout.items=items;layout.n=3;layout.width=W;layout.height=H;
    items[0]=(dl_item_t){.kind=DL_RECT,.w=W,.h=H,.color=0x112233,.node=&base};
    items[1]=(dl_item_t){.kind=DL_RECT,.x=30,.y=25,.w=140,.h=30,.color=0x445566,.node=&button};
    int radii[]={0,5,20,40},rings[]={0,1,3,12,80};
    uint32_t pixels[W*H];
    for(unsigned a=0;a<4;a++)for(unsigned b=0;b<5;b++)for(int clip=0;clip<2;clip++){
        items[2]=(dl_item_t){.kind=DL_RECT,.x=10,.y=10,.w=180,.h=60,
            .radius=radii[a],.ring=rings[b],.color=0x778899,.node=&frame,
            .has_clip=clip,.cx0=20,.cx1=150,.cy0=15,.cy1=65};
        render_page(&layout,pixels,W,W,H,0,0,W,H,0);
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){
            dom_node_t* expected=pixels[y*W+x]==0x778899?&frame:
                                 pixels[y*W+x]==0x445566?&button:&base;
            if(layout_hit(&layout,x,y)!=expected){
                fprintf(stderr,"geometry mismatch r=%d ring=%d clip=%d x=%d y=%d color=%x\n",
                        radii[a],rings[b],clip,x,y,pixels[y*W+x]);
                return 1;
            }
            checks++;
        }
    }
    items[2]=(dl_item_t){.kind=DL_TEXT,.x=20,.y=20,.w=20,.h=10,.node=&frame,
        .has_clip=1,.cx0=25,.cy0=25,.cx1=35,.cy1=30};
    CHECK(layout_hit(&layout,30,25)==&frame);
    CHECK(layout_hit(&layout,20,25)==&base);
    CHECK(layout_hit(&layout,30,22)==&base);
    CHECK(layout_hit(&layout,30,31)==&button);
    items[1]=(dl_item_t){.kind=DL_IFRAME,.x=30,.y=20,.w=100,.h=40,.node=&button,
        .has_clip=1,.cx0=35,.cy0=25,.cx1=125,.cy1=55};
    items[2]=(dl_item_t){.kind=DL_RECT,.x=50,.y=30,.w=40,.h=20,.node=&frame};
    CHECK(layout_hit_item(&layout,60,40)==&items[2]);
    CHECK(layout_hit_item(&layout,40,40)==&items[1]);
    CHECK(layout_hit_item(&layout,31,40)==&items[0]);
    CHECK(layout_hit_item(&layout,40,22)==&items[0]);
    printf("PASS: %d painted-pixel, clipping and frame stacking checks\n",checks);
}
