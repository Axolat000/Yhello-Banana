/* SPDX-License-Identifier: GPL-3.0-or-later */
#define REMOTE_PAGE_IMPLEMENTATION
#include "remote_page.h"
#include "web_worker.h"
#include "web_policy.h"
#include "app.h"
#include "fs.h"
#include "image.h"
#include "browser.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "task.h"
#include "serial.h"
#include "../sdk/include/banana_web_worker.h"
#define WORKERS 4
#define TRANSFER_BUDGET (64u << 20)
typedef struct remote_page {
    page_t p;                       /* kernel-owned compatibility proxy */
    layout_t layout;
    dom_node_t focus;
    int app,slot,dead,busy;
    uint32_t generation;
    volatile int stage,resource_ready;
    ww_command_t cmd;
    ww_state_t state;
    const uint8_t* input;
    uint8_t* output;
    uint32_t out_cap,out_used;
    uint8_t* navbody;
    uint32_t nav_used;
    ww_resource_t* request;
    ww_response_t response;
    char* resource_data;
    uint32_t transferred,requests;
    page_env_t environment;
    char principal[8192];
    char grants[16][FS_PATH_LEN];
    int ngrants;
} remote_page_t;
static remote_page_t* workers[WORKERS];
static int terminated(const char* s,uint32_t n) { for(uint32_t i=0;i<n;i++)if(!s[i])return 1;return 0; }
static remote_page_t* channel(int app,uint32_t generation) {
    for(int i=0;i<WORKERS;i++)if(workers[i]&&workers[i]->app==app&&workers[i]->generation==generation&&!workers[i]->dead)return workers[i];
    return NULL;
}
int web_worker_ipc(int app,uint32_t generation,int op,const void* in,uint32_t len,void* out,uint32_t cap) {
    remote_page_t* r=channel(app,generation);
    if(!r)return -1;
    switch(op){
    case WW_WAIT:
        if(len||cap!=sizeof(ww_command_t))return -1;
        if(r->stage!=1){task_sleep_ms(10);return 0;}
        memcpy(out,&r->cmd,sizeof(r->cmd));r->stage=2;return sizeof(r->cmd);
    case WW_INPUT: {
        if(r->stage!=2||len!=sizeof(ww_io_t))return -1;
        ww_io_t h;memcpy(&h,in,sizeof(h));
        const void* data=NULL;uint32_t size=0;
        if(!h.kind){data=r->input;size=r->cmd.length;}
        else if(h.kind==1&&r->resource_ready==2){data=r->resource_data;size=r->response.length;}
        else return -1;
        if(h.offset>size||cap>size-h.offset||(!data&&cap))return -1;
        if(cap)memcpy(out,(const uint8_t*)data+h.offset,cap);
        return cap;
    }
    case WW_OUTPUT: {
        if(r->stage!=2||len<sizeof(ww_io_t)||len>WW_CHUNK+sizeof(ww_io_t))return -1;
        ww_io_t h;memcpy(&h,in,sizeof(h));uint32_t n=len-sizeof(h);
        if(h.kind==WW_RESULT){
            if(h.offset!=r->out_used||n>r->out_cap-r->out_used||(!r->output&&n))return -1;
            if(n)memcpy(r->output+r->out_used,(const uint8_t*)in+sizeof(h),n);r->out_used+=n;
        }else if(h.kind==WW_NAVBODY){
            if(h.offset!=r->nav_used||n>WW_POST-r->nav_used)return -1;
            if(!r->navbody){r->navbody=kmalloc(WW_POST+1);if(!r->navbody)return -1;}
            memcpy(r->navbody+r->nav_used,(const uint8_t*)in+sizeof(h),n);r->nav_used+=n;r->navbody[r->nav_used]=0;
        }else return -1;
        return n;
    }
    case WW_FINISH: {
        if(r->stage!=2||len!=sizeof(ww_state_t))return -1;
        ww_state_t s;memcpy(&s,in,sizeof(s));
        if(s.version!=WW_VERSION||s.height<0||s.height>500000||s.nav_post_len>r->nav_used||
           !terminated(s.title,sizeof(s.title))||!terminated(s.status,sizeof(s.status))||
           !terminated(s.alert,sizeof(s.alert))||!terminated(s.nav,sizeof(s.nav))||
           !terminated(s.nav_post_type,sizeof(s.nav_post_type)))return -1;
        for(const char* t=s.nav_post_type;*t;t++)if((uint8_t)*t<32||*t==127)return -1;
        r->state=s;__sync_synchronize();r->stage=3;return 0;
    }
    case WW_RESOURCE: {
        if(r->stage!=2||len!=sizeof(ww_resource_t)||r->resource_ready==1||r->requests>=512)return -1;
        ww_resource_t* q=kmalloc(sizeof(*q));if(!q)return -1;memcpy(q,in,sizeof(*q));
        if(q->length>WW_REQUEST_BODY||!terminated(q->url,sizeof(q->url))||
           !terminated(q->method,sizeof(q->method))||!terminated(q->type,sizeof(q->type))){kfree(q);return -1;}
        if(r->request)kfree(r->request);if(r->resource_data)kfree(r->resource_data);
        r->resource_data=NULL;r->request=q;memset(&r->response,0,sizeof(r->response));
        r->requests++;__sync_synchronize();r->resource_ready=1;return 0;
    }
    case WW_RESPONSE:
        if(r->stage!=2||len||cap!=sizeof(ww_response_t))return -1;
        if(r->resource_ready!=2)return 0;
        memcpy(out,&r->response,sizeof(r->response));return sizeof(r->response);
    default:return -1;
    }
}
static int granted(remote_page_t* r,const char* path) {
    for(int i=0;i<r->ngrants;i++)if(!strcmp(path,r->grants[i]))return 1;
    return 0;
}
static void serve(remote_page_t* r) {
    ww_resource_t* q=r->request;ww_response_t* a=&r->response;
    a->result=-1;
    kstrlcpy(a->error,"Browser permission or resource quota denied",sizeof(a->error));
    if(r->transferred>=TRANSFER_BUDGET)goto done;
    switch(q->kind){
    case WW_FETCH: case WW_REQUEST:
        if(web_network_url(q->url)){
            if(q->kind==WW_REQUEST&&!web_same_origin(r->principal,q->url))break;
            if(strcmp(q->method,"GET")&&strcmp(q->method,"POST"))break;
            int valid=1;for(const char* t=q->type;*t;t++)if((uint8_t)*t<32||*t==127)valid=0;
            if(!valid)break;
            a->result=browser_worker_fetch(r->principal,q->kind==WW_REQUEST,q->url,q->method,
                (char*)q->body,q->length,q->type,&r->resource_data,&a->length,a->type,sizeof(a->type),
                a->url,sizeof(a->url),a->error,sizeof(a->error));
        }else if(!strncmp(q->url,"file://",7)&&granted(r,q->url+7)&&r->environment.read_file){
            a->result=r->environment.read_file(r->environment.ctx,q->url+7,&r->resource_data,&a->length);
            kstrlcpy(a->type,page_mime_of(q->url+7),sizeof(a->type));kstrlcpy(a->url,q->url,sizeof(a->url));
        }else if(!strncmp(r->principal,"about:",6)&&!strncmp(q->url,"about:",6)&&r->environment.fetch){
            a->result=r->environment.fetch(r->environment.ctx,q->url,&r->resource_data,&a->length,
                a->type,sizeof(a->type),a->url,sizeof(a->url),a->error,sizeof(a->error));
        }
        break;
    case WW_FILE:
        if(granted(r,q->url)&&r->environment.read_file)
            a->result=r->environment.read_file(r->environment.ctx,q->url,&r->resource_data,&a->length);
        break;
    case WW_COOKIE_GET:
        if(web_same_origin(r->principal,q->url)&&r->environment.cookie_get){
            r->resource_data=kzalloc(8192);
            if(r->resource_data){r->environment.cookie_get(r->environment.ctx,q->url,r->resource_data,8192);
                a->length=strlen(r->resource_data);a->result=0;}
        }break;
    case WW_COOKIE_SET:
        if(web_same_origin(r->principal,q->url)&&q->length<2048&&r->environment.cookie_set){
            char text[2048];memcpy(text,q->body,q->length);text[q->length]=0;
            r->environment.cookie_set(r->environment.ctx,q->url,text);a->result=0;
        }break;
    case WW_MESSAGE:
        if(q->length<8192&&r->environment.message){
            char* text=kmalloc(q->length+1);
            if(text){memcpy(text,q->body,q->length);text[q->length]=0;
                r->environment.message(r->environment.ctx,text);kfree(text);a->result=0;}
        }break;
    default:break;
    }
    if(a->length>WW_DOCUMENT||a->length>TRANSFER_BUDGET-r->transferred){
        if(r->resource_data)kfree(r->resource_data);r->resource_data=NULL;a->length=0;a->result=-1;
    }else r->transferred+=a->length;
done:
    __sync_synchronize();r->resource_ready=2;
}
static void crashed(remote_page_t* r,const char* why) {
    if(!r->dead)app_web_kill(r->app,r->generation);
    if(r->slot>=0&&workers[r->slot]==r)workers[r->slot]=NULL;
    r->slot=-1;r->dead=1;r->stage=0;r->p.js=NULL;r->p.focus=NULL;r->p.dirty=0;
    r->p.nav_pending=r->p.alert_pending=r->p.file_pick_pending=0;
    kstrlcpy(r->p.status,why,sizeof(r->p.status));r->layout.height=0;
    klog("browser worker: %s (app %d)\n",why,r->app);
}
static void apply(remote_page_t* r) {
    page_t* p=&r->p;ww_state_t* s=&r->state;
    r->layout.height=s->height;p->dirty=!!s->dirty;p->js=s->js?(interp_t*)p:NULL;
    p->focus=s->focus?&r->focus:NULL;r->focus.tag=s->textarea?"textarea":"input";
    p->live_keys=!!s->live_keys;p->A.oom=!!s->oom;
    p->alert_pending=!!s->alert_pending;p->nav_pending=!!s->nav_pending;p->nav_newtab=!!s->nav_newtab;
    p->file_pick_pending=!!s->file_pick_pending;p->file_pick=s->file_pick_pending?&r->focus:NULL;
    p->scroll_req=s->scroll_req>=-1&&s->scroll_req<500000?s->scroll_req:-1;
    kstrlcpy(p->title,s->title,sizeof(p->title));kstrlcpy(p->status,s->status,sizeof(p->status));
    kstrlcpy(p->alert,s->alert,sizeof(p->alert));kstrlcpy(p->nav,s->nav,sizeof(p->nav));
    kstrlcpy(p->nav_post_type,s->nav_post_type,sizeof(p->nav_post_type));
    p->nav_post_len=s->nav_post_len;p->nav_post=s->nav_post_len?(char*)r->navbody:NULL;
    if(p->nav_pending&&web_network_url(r->principal)&&!web_network_url(p->nav)&&strcmp(p->nav,"about:blank")){
        p->nav_pending=0;kstrlcpy(p->status,"Blocked navigation to a local or privileged URL",sizeof(p->status));
    }
}
static int call(remote_page_t* r,unsigned int op,const int* args,const char* url,const void* data,
                uint32_t n,void* out,uint32_t cap) {
    if(!r||r->dead||r->busy||n>WW_DOCUMENT)return -1;
    r->busy=1;
    if(r->navbody)kfree(r->navbody);r->navbody=NULL;r->nav_used=0;r->p.nav_post=NULL;r->p.nav_post_len=0;
    memset(&r->cmd,0,sizeof(r->cmd));r->cmd.version=WW_VERSION;r->cmd.op=op;r->cmd.length=n;
    if(args)memcpy(r->cmd.a,args,10*sizeof(int));r->cmd.a[10]=r->p.view_h;
    r->cmd.a[11]=(r->p.nav_pending?1:0)|(r->p.alert_pending?2:0)|(r->p.file_pick_pending?4:0)|
        (r->p.status[0]?8:0)|(r->p.scroll_req>=0?16:0)|(r->p.dirty?32:0);
    if(url)kstrlcpy(r->cmd.url,url,sizeof(r->cmd.url));
    r->input=data;r->output=out;r->out_cap=cap;r->out_used=0;
    app_web_budget(r->app,r->generation,(op==WW_LOAD||op==WW_DECODE)?90000u:10000u,(op==WW_LOAD||op==WW_DECODE)?30000u:2000u);
    __sync_synchronize();r->stage=1;uint32_t start=timer_ms();
    for(;;){
        if(r->resource_ready==1)serve(r);
        if(r->stage==3)break;
        if(!app_web_alive(r->app,r->generation)||timer_ms()-start>((op==WW_LOAD||op==WW_DECODE)?95000u:12000u)){
            crashed(r,"Page stopped: worker crash, memory limit or execution quota");r->busy=0;return -1;
        }
        task_sleep_ms(1);
    }
    __sync_synchronize();apply(r);r->stage=0;r->input=NULL;r->output=NULL;r->busy=0;
    app_web_budget(r->app,r->generation,0,0);
    return r->state.result;
}
page_t* remote_page_new(page_env_t* env,uint32_t limit) {
    (void)limit;int slot=-1;
    for(int i=0;i<WORKERS;i++)if(!workers[i]){slot=i;break;}
    if(slot<0)return NULL;
    remote_page_t* r=kzalloc(sizeof(*r));if(!r)return NULL;
    r->environment=*env;r->slot=slot;r->app=-1;r->p.env=&r->environment;r->p.layout=&r->layout;
    r->layout.A=(arena_t*)r;r->p.scroll_req=-1;
    char err[128];
    r->app=app_spawn_web_worker("/apps/webworker/app-x86_64",err,sizeof(err));
    if(r->app<0){klog("browser worker: %s\n",err);kfree(r);return NULL;}
    r->generation=app_web_generation(r->app);
    workers[slot]=r;
    if(call(r,WW_INIT,NULL,NULL,NULL,0,NULL,0)<0){remote_page_free(&r->p);return NULL;}
    return &r->p;
}
void remote_page_free(page_t* p) {
    if(!p)return;remote_page_t* r=(remote_page_t*)p;
    r->dead=1;app_web_kill(r->app,r->generation);
    if(r->slot>=0&&workers[r->slot]==r)workers[r->slot]=NULL;
    /* Reclaim the old process before another page consumes its pool share.
     * The generation prevents a reused app slot from being killed here. */
    uint32_t start=timer_ms();
    while(app_web_alive(r->app,r->generation)&&timer_ms()-start<2000u)task_sleep_ms(1);
    if(r->navbody)kfree(r->navbody);if(r->request)kfree(r->request);if(r->resource_data)kfree(r->resource_data);
    kfree(r);
}
void remote_page_load(page_t* p,const char* url,const char* html,uint32_t len,int width) {
    remote_page_t* r=(remote_page_t*)p;
    kstrlcpy(r->principal,url,sizeof(r->principal));kstrlcpy(p->url,url,sizeof(p->url));
    if(!strncmp(url,"file://",7)&&strlen(url+7)<FS_PATH_LEN){kstrlcpy(r->grants[0],url+7,FS_PATH_LEN);r->ngrants=1;}
    int a[10]={width};call(r,WW_LOAD,a,url,html,len,NULL,0);
}
void remote_page_update(page_t* p,int width){int a[10]={width};call((remote_page_t*)p,WW_UPDATE,a,NULL,NULL,0,NULL,0);}
void remote_page_click(page_t* p,int x,int y){int a[10]={x,y};call((remote_page_t*)p,WW_CLICK,a,NULL,NULL,0,NULL,0);}
int remote_page_key(page_t* p,char c){int a[10]={(unsigned char)c};return call((remote_page_t*)p,WW_KEY,a,NULL,NULL,0,NULL,0)>0;}
int remote_page_key_live(page_t* p,int down,const char* key,const char* code,int kc,int mods){
    int a[10]={down,kc,mods};char text[128]={0};kstrlcpy(text,key,64);kstrlcpy(text+64,code,64);
    return call((remote_page_t*)p,WW_LIVE,a,NULL,text,sizeof(text),NULL,0)>0;
}
int remote_page_tick(page_t* p){return call((remote_page_t*)p,WW_TICK,NULL,NULL,NULL,0,NULL,0)>0;}
int remote_page_link_at(page_t* p,int x,int y,char* out,int cap){
    int a[10]={x,y};char text[8192]={0};int rc=call((remote_page_t*)p,WW_LINK,a,NULL,NULL,0,text,sizeof(text));
    text[sizeof(text)-1]=0;if(cap>0)kstrlcpy(out,text,cap);return rc>0;
}
void remote_page_set_files(page_t* p,dom_node_t* node,const char* path){
    (void)node;remote_page_t* r=(remote_page_t*)p;
    if(path&&strlen(path)<FS_PATH_LEN&&r->ngrants<16)kstrlcpy(r->grants[r->ngrants++],path,FS_PATH_LEN);
    call(r,WW_PICK,NULL,NULL,path?path:"",path?strlen(path)+1:1,NULL,0);
}
int remote_page_eval(page_t* p,const char* code,char* out,int cap) {
    char* text=kzalloc(65536);if(!text)return -1;
    int rc=call((remote_page_t*)p,WW_EVAL,NULL,NULL,code,strlen(code),text,65536);
    if(out&&cap>0){text[65535]=0;kstrlcpy(out,text,cap);}kfree(text);return rc;
}
void remote_page_post(page_t* p,const char* text){call((remote_page_t*)p,WW_POSTMSG,NULL,NULL,text,strlen(text),NULL,0);}
static remote_page_t* from_layout(const layout_t* l){return l?(remote_page_t*)l->A:NULL;}
void remote_render(const layout_t* l,uint32_t* pixels,int stride,int bw,int bh,int vx,int vy,int vw,int vh,int scroll) {
    if(!pixels||stride<bw||vw<=0||vh<=0||vx<0||vy<0||vx>bw-vw||vy>bh-vh||(uint64_t)vw*vh>WW_PIXELS)return;
    remote_page_t* r=from_layout(l);
    for(int y=vy;y<vy+vh;y++)memset32(pixels+(size_t)y*stride+vx,0xffffff,vw);
    if(!r||r->dead)return;
    int a[10]={vw,vh,scroll,l->sel_on,l->sel_i0,l->sel_o0,l->sel_i1,l->sel_o1};
    uint32_t bytes=(uint32_t)vw*vh*4u;
    uint32_t* temp=(vx||vy||stride!=vw)?kmalloc(bytes):pixels;
    if(!temp)return;
    if(call(r,WW_RENDER,a,NULL,NULL,0,temp,bytes)==0&&r->out_used==bytes&&temp!=pixels)
        for(int y=0;y<vh;y++)memcpy(pixels+(size_t)(vy+y)*stride+vx,temp+(size_t)y*vw,(size_t)vw*4);
    if(temp!=pixels)kfree(temp);
}
int remote_text_pos(layout_t* l,int x,int y,int* item,int* off){
    int a[10]={x,y},pos[2]={0,0};int rc=call(from_layout(l),WW_TEXTPOS,a,NULL,NULL,0,pos,sizeof(pos));
    if(rc>0){*item=pos[0];*off=pos[1];}return rc>0;
}
uint32_t remote_text_range(layout_t* l,int i0,int o0,int i1,int o1,char* out,uint32_t cap){
    int a[10]={i0,o0,i1,o1};char* text=kzalloc(65536);if(!text)return 0;
    int n=call(from_layout(l),WW_TEXTRANGE,a,NULL,NULL,0,text,65536);text[65535]=0;
    if(n<0)n=0;if(cap)kstrlcpy(out,text,cap);kfree(text);return cap?(uint32_t)n<cap?(uint32_t)n:cap-1:0;
}

/* All nontrivial image parsing/decoding is user code, including SDK images
 * and wallpapers. The kernel validates only dimensions and byte counts. */
int web_worker_decode(const uint8_t* data,uint32_t len,image_t* out,char* err,uint32_t errcap) {
    memset(out,0,sizeof(*out));
    if(!len||len>WW_DOCUMENT){kstrlcpy(err,"Image input exceeds the 8 MiB quota",errcap);return -1;}
    page_t* p=remote_page_new(browser_env(),0);
    if(!p){kstrlcpy(err,"No isolated image worker is available (64-bit kernel required)",errcap);return -1;}
    remote_page_t* r=(remote_page_t*)p;
    uint8_t* pixels=kmalloc(WW_PIXELS*3u);
    if(!pixels){remote_page_free(p);kstrlcpy(err,"Not enough memory for image output",errcap);return -1;}
    int rc=call(r,WW_DECODE,NULL,NULL,data,len,pixels,WW_PIXELS*3u);
    int dims[2]={0,0};
    if(r->nav_used==sizeof(dims))memcpy(dims,r->navbody,sizeof(dims));
    uint64_t count=(uint64_t)(uint32_t)dims[0]*(uint32_t)dims[1];
    if(rc||dims[0]<=0||dims[1]<=0||dims[0]>8192||dims[1]>8192||
       count>WW_PIXELS||count*3u!=r->out_used){
        if(rc && r->out_used && r->out_used<=128 && !pixels[r->out_used-1])
            kstrlcpy(err,(const char*)pixels,errcap);
        else kstrlcpy(err,"Image decoder rejected data or exceeded its quota",errcap);
        kfree(pixels);remote_page_free(p);return -1;
    }
    uint8_t* smaller=krealloc(pixels,(size_t)count*3u);
    out->rgb=smaller?smaller:pixels;out->w=dims[0];out->h=dims[1];
    remote_page_free(p);return 0;
}

void web_isolation_tests(void (*report)(const char*,int,const char*)) {
    page_env_t* env=browser_env();
    page_t* canary=remote_page_new(env,0);
    report("isolated renderer starts",canary!=NULL,"256 MiB per worker, shared pool <= half free RAM");
    if(!canary)return;
    const char* doc="<html><head><title>Canary</title></head><body><form method=post action='/submit'><input name=q value=manual><button id=go type=submit>Go</button></form><p id=x>alive</p><script>var counter=7;</script></body></html>";
    remote_page_load(canary,"https://example.com/",doc,strlen(doc),640);
    report("HTML and JS run outside the kernel",!strcmp(canary->title,"Canary"),canary->status);
    char text[256]={0};
    int rc=remote_page_eval(canary,"counter+5",text,sizeof(text));
    report("JavaScript evaluation crosses IPC",!rc&&!strcmp(text,"12"),text);
    rc=remote_page_eval(canary,"document.title='Changed'; document.getElementById('x').textContent='updated'; 'ok'",text,sizeof(text));
    remote_page_update(canary,640);
    report("DOM updates cross IPC",!rc&&!strcmp(canary->title,"Changed"),canary->status);
    rc=remote_page_eval(canary,"document.getElementById('go').click(); 'submitted'",text,sizeof(text));
    report("scripted form clicks preserve POST bodies",!rc&&canary->nav_pending&&canary->nav_post_len&&
           strstr(canary->nav_post,"q=manual"),canary->nav_post_type);
    canary->nav_pending=0;
    remote_page_eval(canary,"location.href='file:///etc/hostname'; 'queued'",text,sizeof(text));
    report("remote page cannot navigate into local files",!canary->nav_pending,canary->status);
    rc=remote_page_eval(canary,"var secret=''; try { var x=new XMLHttpRequest(); x.open('GET','file:///etc/hostname',false); x.send(); secret=x.responseText; } catch(e){}; secret",text,sizeof(text));
    report("remote page cannot read arbitrary local files",rc==0&&text[0]==0,text);
    int a[10]={4};
    rc=call((remote_page_t*)canary,WW_TEST,a,NULL,NULL,0,NULL,0);
    report("file, command and process syscalls denied",rc==0,canary->status);
    for(int test=1;test<=3;test++){
        page_t* victim=remote_page_new(env,0);
        if(!victim){report("hostile worker starts",0,"no worker slot");break;}
        a[0]=test;
        rc=call((remote_page_t*)victim,WW_TEST,a,NULL,NULL,0,NULL,0);
        report(test==1?"worker CPU fault stays isolated":test==2?"infinite loop hits CPU/time quota":"memory exhaustion hits physical quota",
               rc<0&&((remote_page_t*)victim)->dead,victim->status);
        remote_page_free(victim);
        rc=remote_page_eval(canary,"counter+1",text,sizeof(text));
        report("independent renderer survives hostile worker",!rc&&!strcmp(text,"8"),text);
    }
    static const uint8_t broken_png[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,255,255,255,255};
    image_t image;char err[160];
    rc=web_worker_decode(broken_png,sizeof(broken_png),&image,err,sizeof(err));
    report("malformed decoder input stays isolated",rc<0,err);
    rc=remote_page_eval(canary,"counter",text,sizeof(text));
    report("renderer survives a rejected image",!rc&&!strcmp(text,"7"),text);
    uint32_t* pixels=kmalloc(640u*200u*4u);
    if(pixels){remote_render(canary->layout,pixels,640,640,200,0,0,640,200,0);
        int painted=0;for(int i=0;i<640*200;i++)if(pixels[i]!=0xffffff){painted=1;break;}
        report("validated pixel rendering crosses IPC",painted,canary->status);kfree(pixels);}
    remote_page_free(canary);
}
