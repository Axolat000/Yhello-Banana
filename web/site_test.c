#include "site_test.h"
#include "kheap.h"
#include "kstring.h"

typedef struct {
    page_env_t* base;
    void (*report)(const char*,int,const char*);
    int fails,requests,errors,logs;
    char error[200],log[200];
} site_check_t;
static uint32_t site_clock;
static uint32_t now(void){return site_clock;}
static void check(site_check_t* c,const char* name,int ok,const char* note) {
    c->report(name,!!ok,note?note:"");if(!ok)c->fails++;
}
static void log_line(void* ctx,const char* s) {
    site_check_t* c=ctx;c->logs++;kstrlcpy(c->log,s,sizeof(c->log));
    if(c->base->log)c->base->log(c->base->ctx,s);
}
static int fetch(void* ctx,const char* url,char** data,uint32_t* len,char* ct,int cc,
                 char* final,int fc,char* err,int ec) {
    site_check_t* c=ctx;
    int rc;
    if(++c->requests>128) {kstrlcpy(err,"live test resource limit",ec);rc=-1;}
    else rc=c->base->fetch(c->base->ctx,url,data,len,ct,cc,final,fc,err,ec);
    if(rc) {c->errors++;ksnprintf(c->error,sizeof(c->error),"%.110s: %.80s",url,err);}
    return rc;
}
static int decode(void* ctx,const uint8_t* data,uint32_t len,img_data_t* img,arena_t* a) {
    site_check_t* c=ctx;
    return c->base->decode_image?c->base->decode_image(c->base->ctx,data,len,img,a):-1;
}
static void advance(page_t* p,int ms) {
    for(int t=0;t<ms;t+=10){site_clock+=10;page_tick(p);page_update(p,p->width);WEB_TICK();}
}
static dom_node_t* query(page_t* p,const char* selector) {return css_query(&p->A,p->doc,selector);}
static void click(page_t* p,dom_node_t* e) {
    /* Dispatch the DOM event directly so a broken CSS position cannot hide a
     * separate script failure. Visual layout checks are reported separately. */
    if(e)jsdom_dispatch(p,e,"click");
    advance(p,50);
}
static int attr_is(dom_node_t* e,const char* name,const char* value) {
    const char* a=e?dom_attr(e,name):NULL;return a && !strcmp(a,value);
}
static void images(dom_node_t* n,int* count,int* failed) {
    if(n->type==DOM_ELEM && !strcmp(n->tag,"img")){(*count)++;if(!n->img || n->img->failed)(*failed)++;}
    for(dom_node_t* c=n->first;c;c=c->next)images(c,count,failed);
}
int site_tests_run(page_env_t* base,uint32_t memory,void (*report)(const char*,int,const char*),page_t** out) {
    if(out)*out=NULL;
    site_check_t c={.base=base,.report=report};site_clock=1000;
    char* html=NULL;uint32_t len=0;char type[96]={0},url[PAGE_URL_MAX]={0},err[200]={0};
    int rc=base && base->fetch?fetch(&c,BROWSER_SITE_TEST_URL,&html,&len,type,sizeof(type),url,sizeof(url),err,sizeof(err)):-1;
    check(&c,"SWM HTTPS document",rc==0 && len>0,rc?err:url);
    if(rc || !html)return c.fails;
    page_env_t env={.fetch=fetch,.now_ms=now,.yield=base->yield,.log=log_line,.ctx=&c};
    if(base->decode_image)env.decode_image=decode;
    page_t* p=page_new(&env,memory);
    if(!p){kfree(html);check(&c,"SWM page allocation",0,"");return c.fails;}
    p->view_h=800;page_load(p,url[0]?url:BROWSER_SITE_TEST_URL,html,len,1280);kfree(html);advance(p,250);
    check(&c,"SWM page title and content",strstr(p->title,"SuperWebManager") && dom_find_id(p->doc,"overview") && dom_find_id(p->doc,"install"),p->title);
    dom_node_t* meta=query(p,"meta[name=generator]");
    check(&c,"SWM Astro generator",meta && strstr(dom_attr(meta,"content")?dom_attr(meta,"content"):"","Astro"),meta?dom_attr(meta,"content"):"missing");
    dom_node_t* body=dom_body(p->doc);
    char note[200];
    ksnprintf(note,sizeof(note),"background #%06x, text #%06x",body && body->style?body->style->bg:0,body && body->style?body->style->color:0);
    check(&c,"SWM light theme CSS",body && body->style && body->style->bg==0xf4f5f8 && body->style->color==0x10131a,note);
    dom_node_t* overview=dom_find_id(p->doc,"overview");
    page_update(p,640);
    check(&c,"SWM Tailwind calculated spacing",overview && overview->style && overview->style->padding[0]==24,"");
    dom_node_t* sidebar=query(p,".sidebar");int responsive=sidebar && sidebar->style && sidebar->style->flex_dir==FLEX_ROW;
    page_update(p,1280);responsive &= sidebar && sidebar->style && sidebar->style->flex_dir==FLEX_COL;
    check(&c,"SWM sidebar switches layout on resize",responsive,"");
    dom_node_t* main=query(p,"main"),*heading=query(p,"h1");
    ksnprintf(note,sizeof(note),"sidebar width %d, content x=%d width=%d",sidebar?sidebar->box_w:0,main?main->box_x:0,main?main->box_w:0);
    check(&c,"SWM desktop grid keeps sidebar beside content",sidebar && main && sidebar->box_w>=200 && sidebar->box_w<=320 && main->box_x>=sidebar->box_x+sidebar->box_w && main->box_w>=600,note);
    ksnprintf(note,sizeof(note),"heading box %dx%d",heading?heading->box_w:0,heading?heading->box_h:0);
    check(&c,"SWM heading has readable wrapping",heading && heading->box_w>=300 && heading->box_h>0 && heading->box_h<600,note);
    dom_node_t* island=query(p,"astro-island");
    check(&c,"SWM Astro island hydration",island && !dom_attr(island,"ssr"),p->status[0]?p->status:c.log);
    int tabs=1;const char* names[]={"mac","freebsd","android","linux"};
    for(int i=0;i<4;i++) {
        char id[64];ksnprintf(id,sizeof(id),"os-tab-%s",names[i]);dom_node_t* tab=dom_find_id(p->doc,id);click(p,tab);
        ksnprintf(id,sizeof(id),"os-panel-%s",names[i]);dom_node_t* panel=dom_find_id(p->doc,id);
        tabs &= tab && attr_is(tab,"aria-selected","true") && panel && !dom_attr(panel,"hidden");
        for(int j=0;j<4;j++)if(j!=i) {
            ksnprintf(id,sizeof(id),"os-panel-%s",names[j]);panel=dom_find_id(p->doc,id);
            tabs &= panel && dom_attr(panel,"hidden")!=NULL;
        }
    }
    check(&c,"SWM install tabs switch every OS panel",tabs,p->status);
    page_update(p,640);dom_node_t* menu=query(p,"[data-menu-toggle]");dom_node_t* nav=dom_find_id(p->doc,"mobile-nav");
    click(p,menu);int toggles=attr_is(menu,"aria-expanded","true") && nav && !dom_attr(nav,"hidden");
    click(p,menu);toggles &= attr_is(menu,"aria-expanded","false") && nav && dom_attr(nav,"hidden");
    check(&c,"SWM mobile menu opens and closes",toggles,p->status);
    dom_node_t* english=query(p,"h1 .i18n-en"),*french=query(p,"h1 .i18n-fr");
    dom_node_t* root=dom_find_tag(p->doc,"html");const char* locale=root?dom_attr(root,"data-locale"):NULL;
    check(&c,"SWM locale hides the other heading",english && french && english->style && french->style && locale &&
        (!strcmp(locale,"en")?(english->style->display!=DISP_NONE && french->style->display==DISP_NONE):
         !strcmp(locale,"fr") && french->style->display!=DISP_NONE && english->style->display==DISP_NONE),locale?locale:"");
    check(&c,"SWM scripts finish without errors",!p->status[0] && !c.logs,p->status[0]?p->status:c.log);
    ksnprintf(note,sizeof(note),"%d requests, %d failed",c.requests,c.errors);
    check(&c,"SWM resources load",!c.errors,c.errors?c.error:note);
    if(base->decode_image) {
        int count=0,failed=0;images(p->doc,&count,&failed);
        ksnprintf(note,sizeof(note),"%d of %d images decoded",count-failed,count);
        check(&c,"SWM image decoding",!failed,note);
    }
    ksnprintf(note,sizeof(note),"%u MiB document arena used",p->A.total>>20);
    check(&c,"SWM stays within page memory limits",!p->A.oom && !p->LA.oom && !p->SA.oom,note);
    page_update(p,1280);
    /* env and instrumentation are stack-owned; preserve only the base env. */
    p->env=base;
    if(out)*out=p;else page_free(p);
    return c.fails;
}
