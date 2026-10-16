#include "framework_test.h"
#include "framework_fixtures.h"
#include "page.h"
#include "kheap.h"
#include "kstring.h"

typedef struct {
    void (*report)(const char*, int, const char*);
    int failed, fetches, logs;
    char last_log[200];
} checks_t;
static uint32_t clock_ms;
static uint32_t now_ms(void) { return clock_ms; }
static void check(checks_t* c,const char* name,int ok,const char* note) {
    if(!ok)c->failed++;
    c->report(name,!!ok,note ? note : "");
}
static void log_line(void* ctx,const char* text) {
    checks_t* c=ctx;kstrlcpy(c->last_log,text,sizeof(c->last_log));c->logs++;
}
static int resource(void* ctx,const char* url,char** data,uint32_t* len,char* type,int tc,
                    char* final,int fc,char* err,int ec) {
    checks_t* c=ctx;const char* src,*mime;c->fetches++;
    if(!framework_fixture(url,&src,len,&mime)) {
        kstrlcpy(err,"resource is not part of the offline suite",ec);return -1;
    }
    *data=kmalloc(*len+1);if(!*data)return -1;
    memcpy(*data,src,*len);(*data)[*len]=0;
    kstrlcpy(type,mime,tc);kstrlcpy(final,url,fc);return 0;
}
static void advance(page_t* p) {
    for(int i=0;i<10;i++) { clock_ms+=10;page_tick(p);page_update(p,p->width); }
}
static style_t* style(page_t* p,const char* id) {
    dom_node_t* e=dom_find_id(p->doc,id);return e?e->style:NULL;
}
static int text_is(page_t* p,const char* id,const char* want) {
    dom_node_t* e=dom_find_id(p->doc,id);
    return e && !strcmp(dom_text(&p->A,e),want);
}
static page_t* load(page_env_t* env,const char* url) {
    const char* html,*type;uint32_t len;
    if(!framework_fixture(url,&html,&len,&type))return NULL;
    page_t* p=page_new(env,96u<<20);if(!p)return NULL;
    p->view_h=600;page_load(p,url,html,len,800);advance(p);return p;
}
static int eval(page_t* p,const char* code) {
    int rc=script_run(p->js,code,(uint32_t)strlen(code),"framework-test");
    if(rc)kstrlcpy(p->status,script_error(p->js),sizeof(p->status));
    advance(p);return rc==0;
}
static void counter(checks_t* c,page_env_t* env,const char* name,const char* url) {
    char label[100];int logs=c->logs;page_t* p=load(env,url);
    ksnprintf(label,sizeof(label),"%s mounts its real offline library",name);
    check(c,label,p && text_is(p,"counter","Count 0"),p?p->status:"allocation failed");
    if(!p)return;
    int loaded_fetches=c->fetches;
    style_t* s=style(p,"counter");
    check(c,"framework inserts a stylesheet after load",s && s->bg==0x2563eb,"");
    int clicks=1,colors=1;
    for(int n=1;n<=2;n++) {
        dom_node_t* button=dom_find_id(p->doc,"counter");
        if(button)page_click(p,button->box_x+button->box_w/2,button->box_y+button->box_h/2);
        advance(p);char expected[32];ksnprintf(expected,sizeof(expected),"Count %d",n);
        clicks &= text_is(p,"counter",expected);
        s=style(p,"counter");colors &= s && s->bg==(n==1?0xb91c1cu:0x2563ebu);
    }
    ksnprintf(label,sizeof(label),"%s click events and reactive updates",name);
    check(c,label,clicks,p->status);
    check(c,"replaced stylesheet and CSS variables update",colors,"");
    check(c,"style changes reuse linked CSS without refetch",c->fetches==loaded_fetches,"");
    s=style(p,"arbitrary");
    check(c,"compiled Tailwind arbitrary selectors and values",s && s->width==240 && s->padding[0]==7 && s->bg==0xabcdef,"");
    s=style(p,"card");
    check(c,"Tailwind rem spacing and max-width",s && s->padding[0]==24 && s->max_width==576,"");
    dom_node_t* heading=dom_find_tag(p->doc,"h1");s=heading?heading->style:NULL;
    check(c,"Tailwind font size, weight and line height",s && s->font_px==24 && s->bold && s->lh_px==32,"");
    s=style(p,"half");check(c,"Tailwind escaped fraction class",s && s->width_pct==50,"");
    s=style(p,"row");int responsive=s && s->flex && s->flex_dir==FLEX_ROW && s->gap_col==16;
    page_update(p,640);s=style(p,"row");responsive &= s && s->flex_dir==FLEX_COL;
    page_update(p,800);s=style(p,"row");responsive &= s && s->flex_dir==FLEX_ROW;
    check(c,"Tailwind responsive variant on resize",responsive,"");
    check(c,"framework finishes without script errors",!p->status[0] && c->logs==logs,p->status);
    page_free(p);
}
static void mutations(checks_t* c,page_env_t* env,const char* path,const char* name,int expected_limit) {
    page_t* p=load(env,path);dom_node_t* result=p?dom_find_id(p->doc,"result"):NULL;
    const char* text=result?dom_text(&p->A,result):"missing result";
    check(c,name,result && !strncmp(text,"PASS",4) && (expected_limit || !p->status[0]),text);
    if(p)page_free(p);
}
static void styles(checks_t* c,page_env_t* env) {
    const char* html="<html><head><style>#target{width:10px}</style></head><body><div id='target'></div></body></html>";
    page_t* p=page_new(env,32u<<20);if(!p){check(c,"dynamic stylesheet page",0,"allocation failed");return;}
    page_load(p,"about:frameworks/tests/styles",html,(uint32_t)strlen(html),800);
    eval(p,"var s=document.createElement('style');s.id='dynamic';s.appendChild(document.createTextNode('#target{width:'));s.appendChild(document.createTextNode('20px}'));document.head.appendChild(s)");
    style_t* st=style(p,"target");check(c,"stylesheet combines appended text nodes",st && st->width==20,p->status);
    eval(p,"s.textContent='#target{width:30px}'");st=style(p,"target");
    check(c,"stylesheet replacement discards old rules",st && st->width==30,p->status);
    uint32_t bytes=p->SA.total;
    int stable=1;
    for(int i=0;i<12;i++) {
        eval(p,i%2?"s.textContent='#target{width:30px}'":"s.textContent='#target{width:31px}'");
        stable &= p->SA.total==bytes;
    }
    check(c,"stylesheet rebuilds reuse bounded storage",stable,"");
    eval(p,"s.setAttribute('media','(min-width:900px)')");st=style(p,"target");
    int media=st && st->width==10;page_update(p,1000);st=style(p,"target");media &= st && st->width==30;
    check(c,"stylesheet media attributes follow viewport",media,"");
    eval(p,"s.parentNode.removeChild(s)");st=style(p,"target");
    check(c,"removing a stylesheet removes its effect",st && st->width==10,p->status);
    eval(p,"var a=document.createElement('style');a.textContent='#target{--a:var(--b);--b:var(--a);width:var(--a,calc(1rem * (1 - 0) + 8px));padding:clamp(2px,1rem,24px)}';document.head.appendChild(a)");st=style(p,"target");
    check(c,"variable cycles, fallback and dimensional CSS math",st && st->width==24 && st->padding[0]==16,p->status);
    check(c,"stylesheet checks finish without script or memory errors",!p->status[0] && !p->A.oom && !p->LA.oom && !p->SA.oom,p->status);
    int identity=eval(p,"var same=document.createElement('div');function assertIdentity(ok){if(!ok)throw new Error('node identity mismatch')}assertIdentity(same.isSameNode(same));assertIdentity(!same.isSameNode(document.createElement('div')));assertIdentity(!same.isSameNode(same.cloneNode(true)));assertIdentity(!same.isSameNode(null));assertIdentity(!same.isSameNode(undefined));var missing=false;try{same.isSameNode()}catch(e){missing=e.name==='TypeError'}assertIdentity(missing);assertIdentity(document.isSameNode(document));var txt=document.createTextNode('same'),comment=document.createComment('same'),fragment=document.createDocumentFragment();assertIdentity(txt.isSameNode(txt));assertIdentity(comment.isSameNode(comment));assertIdentity(fragment.isSameNode(fragment));assertIdentity(!txt.isSameNode(comment));var refused=false;try{same.isSameNode({nodeType:1})}catch(e){refused=e.name==='TypeError'}assertIdentity(refused)");
    check(c,"DOM node identity for Astro slots and templates",identity,p->status);
    int globals=eval(p,"function checkWindow(ok){if(!ok)throw new Error('window global mismatch')}checkWindow(innerWidth===window.innerWidth);checkWindow(innerHeight===window.innerHeight);checkWindow(scrollY===window.scrollY);checkWindow(typeof definitelyAbsentWindowName==='undefined');(function(){let innerWidth=37;checkWindow(innerWidth===37)})();Object.defineProperty(window,'windowGetterProbe',{configurable:true,get:function(){return window.innerWidth+3}});checkWindow(windowGetterProbe===window.innerWidth+3)");
    page_update(p,640);
    globals &= eval(p,"checkWindow(innerWidth===640);checkWindow(windowGetterProbe===643)");
    page_update(p,1000);
    globals &= eval(p,"checkWindow(innerWidth===1000);checkWindow(windowGetterProbe===1003)");
    check(c,"live Window globals, lexical shadowing and resize",globals,p->status);
    page_free(p);
    html="<html><body><div id='result'></div><script>var written=0;var observer=new MutationObserver(function(records){for(var i=0;i<records.length;i++){var node=records[i].addedNodes[0];if(node&&node.id==='written')written++}});observer.observe(document.body,{childList:true});document.write('<span id=\"written\">ok</span>')</script><script>observer.disconnect();document.getElementById('result').textContent=String(written)</script></body></html>";
    p=page_new(env,32u<<20);
    if(!p){check(c,"document.write observer checkpoint",0,"allocation failed");return;}
    page_load(p,"about:frameworks/tests/writes",html,(uint32_t)strlen(html),800);advance(p);
    check(c,"document.write notifies before the following script",text_is(p,"result","1") && !p->status[0],p->status);
    page_free(p);
}
static void astro(checks_t* c,page_env_t* env) {
    int logs=c->logs;
    const char* html,*type;uint32_t len;
    int found=framework_fixture("about:frameworks/astro",&html,&len,&type);
    page_t* p=found?page_new(env,96u<<20):NULL;
    if(p) {
        p->js_disabled=1;
        page_load(p,"about:frameworks/astro",html,len,800);
        check(c,"Astro content renders with JavaScript disabled",text_is(p,"astro-load","Load 0") && text_is(p,"astro-idle","Idle 0") && !p->js,"");
        check(c,"Astro client-only fallback without JavaScript",text_is(p,"astro-fallback","Client island waiting") && !dom_find_id(p->doc,"astro-only"),"");
        page_free(p);
    } else check(c,"Astro static page",0,"allocation failed");
    p=load(env,"about:frameworks/astro");
    check(c,"Astro 5.13.2 generated HTML",p && text_is(p,"astro-static","Astro 5.13.2: generated HTML works without JavaScript."),p?p->status:"allocation failed");
    if(!p)return;
    style_t* st=style(p,"astro-scoped");
    style_t* card=style(p,"card");
    check(c,"Astro scoped CSS and compiled Tailwind",st && st->color==0x123456 && st->padding[0]==7 && card && card->padding[0]==24 && card->bg==0xffffff && card->max_width==576,"");
    check(c,"Astro client-only replaces its server fallback",dom_find_id(p->doc,"astro-only") && !dom_find_id(p->doc,"astro-fallback"),"");
    const char* ids[]={"astro-load","astro-idle","astro-only","astro-script"};
    const char* want[]={"Load 1","Idle 1","Only 1","Script 1"};
    for(int i=0;i<4;i++) {
        dom_node_t* e=dom_find_id(p->doc,ids[i]);
        if(e)page_click(p,e->box_x+e->box_w/2,e->box_y+e->box_h/2);
        advance(p);
        e=dom_find_id(p->doc,ids[i]);
        int hydrated=i==3 || (e && e->parent && !strcmp(e->parent->tag,"astro-island") && !dom_attr(e->parent,"ssr"));
        check(c,i==0?"Astro client:load island hydrates and responds":i==1?"Astro client:idle island hydrates and responds":i==2?"Astro client:only island renders and responds":"Astro bundled module click handler",hydrated && text_is(p,ids[i],want[i]),p->status[0]?p->status:c->last_log);
    }
    dom_node_t* e=dom_find_id(p->doc,"astro-load");
    if(e)page_click(p,e->box_x+e->box_w/2,e->box_y+e->box_h/2);
    advance(p);
    check(c,"Astro islands keep independent state",text_is(p,"astro-load","Load 2") && text_is(p,"astro-idle","Idle 1") && text_is(p,"astro-only","Only 1"),"");
    check(c,"Astro finishes without script errors",!p->status[0] && c->logs==logs,p->status[0]?p->status:c->last_log);
    page_free(p);
}
#include "framework_layout_tests.h"
#include "framework_image_tests.h"

int framework_tests_run(void (*report)(const char*,int,const char*)) {
    checks_t c={.report=report};clock_ms=1000;
    page_env_t env={.fetch=resource,.now_ms=now_ms,.yield=web_yield_hook,.log=log_line,.ctx=&c};
    counter(&c,&env,"React 18.3.1","about:frameworks");
    counter(&c,&env,"Vue 3.5.13","about:frameworks/vue");
    counter(&c,&env,"Alpine 3.14.9","about:frameworks/alpine");
    grid_tests(&c,&env);
    image_tests(&c);
    astro(&c,&env);
    styles(&c,&env);
    mutations(&c,&env,"about:frameworks/tests/mutations","MutationObserver records and old values",0);
    mutations(&c,&env,"about:frameworks/tests/mutations-async","MutationObserver asynchronous delivery",0);
    mutations(&c,&env,"about:frameworks/tests/mutations-limits","MutationObserver feedback and overflow limits",1);
    return c.failed;
}
