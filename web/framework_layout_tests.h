/* SPDX-License-Identifier: GPL-3.0-or-later */
static dom_node_t* box(page_t* p,const char* id) { return dom_find_id(p->doc,id); }
static const char* grid_note(page_t* p,char* note,int size) {
    dom_node_t *a=box(p,"a"),*b=box(p,"b"),*cc=box(p,"c"),*g=box(p,"grid");
    ksnprintf(note,size,"grid=%d; a=%d,%d %d; b=%d,%d %d; c=%d,%d %d",g?g->box_w:0,
        a?a->box_x:0,a?a->box_y:0,a?a->box_w:0,b?b->box_x:0,b?b->box_y:0,b?b->box_w:0,
        cc?cc->box_x:0,cc?cc->box_y:0,cc?cc->box_w:0);return note;
}
static void grid_tests(checks_t* c,page_env_t* env) {
    const char* html="<html><head><style>body{margin:0}#grid{display:grid;grid-template-columns:160px minmax(0,1fr);"
        "grid-template-columns:repeat(99,1fr);grid-template-columns:10px bad;grid-template-columns:2;"
        "grid-template-columns:repeat(16,2147483647px);gap:12px 20px}"
        "#a{height:40px}#b{height:20px}#c{height:25px}#d{height:30px}"
        "#heading{max-width:14ch;margin:0;font-size:60px}#inline{display:grid;grid-template-columns:160px 1fr}"
        "#inline span{display:block;padding:3px}"
        "@media(max-width:600px){#grid{grid-template-columns:minmax(0,1fr)}}"
        "</style></head><body><div id='grid'><div id='a'>A</div><div id='b'>B</div><div id='c'>C</div><div id='d'>D</div></div>"
        "<h1 id='heading'>Readable heading with its own font</h1><div id='inline'><span id='nested'>Nested measurement</span></div></body></html>";
    page_t* p=page_new(env,32u<<20);if(!p){check(c,"grid test page",0,"allocation failed");return;}
    char note[200];
    page_load(p,"about:frameworks/tests/grid",html,(uint32_t)strlen(html),800);
    dom_node_t *a=box(p,"a"),*b=box(p,"b"),*cc=box(p,"c"),*d=box(p,"d"),*h=box(p,"heading"),*nested=box(p,"nested");
    check(c,"grid fixed/fr columns, minmax and column gap",a && b && a->box_w==160 && b->box_x==180 && b->box_w==620 && b->box_y==a->box_y,grid_note(p,note,sizeof(note)));
    check(c,"grid automatic rows and row gap",cc && d && a && cc->box_x==0 && cc->box_y==a->box_y+52 && d->box_x==180 && d->box_y==cc->box_y,"");
    check(c,"ch lengths use the final font size",h && h->style && h->style->max_width==420 && h->box_w==420 && h->box_h<400,"");
    check(c,"measurement preserves nested DOM box coordinates",nested && nested->box_x>=0 && nested->box_y>50 && nested->box_w<800 && nested->box_h<100, nested? (ksnprintf(note,sizeof(note),"nested %d,%d %dx%d",nested->box_x,nested->box_y,nested->box_w,nested->box_h),note):"missing");
    page_update(p,480);
    check(c,"grid collapses to one column on resize",a && b && a->box_w==480 && b->box_x==0 && b->box_y>a->box_y,grid_note(p,note,sizeof(note)));
    page_update(p,800);
    eval(p,"document.getElementById('grid').style.gridTemplateColumns='repeat(2,minmax(0,1fr))'");
    check(c,"grid repeat distributes fractional tracks",a && b && a->box_w==390 && b->box_w==390 && b->box_x==410,grid_note(p,note,sizeof(note)));
    eval(p,"document.getElementById('grid').style.gridTemplateColumns='25% 1fr 2fr'");
    check(c,"grid percentages and weighted fractions",a && b && cc && a->box_w==200 && b->box_w==186 && cc->box_w==374 && cc->box_x==426,grid_note(p,note,sizeof(note)));
    eval(p,"document.getElementById('grid').style.gridTemplateColumns='minmax(300px,1fr) 3fr'");
    check(c,"grid minmax lower bounds redistribute free space",a && b && a->box_w==300 && b->box_w==480 && b->box_x==320,grid_note(p,note,sizeof(note)));
    page_free(p);
}
