/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "browser_test.h"
#include "task.h"
#include "kstring.h"
#include "../web/framework_test.h"
#include "../web/arena.h"
#include "../web/site_test.h"
#include "browser.h"
#include "remote_page.h"
#include "kheap.h"

/* Keep engine recursion off the shell's small stack. Collect results here
 * and print them in the calling shell, preserving its terminal/SSH output. */
static struct { char name[100],note[200];int ok; } rows[96];
static int running,done,nrows,overflow;
static int live_site;
static kjmp_buf_t recovery;
static void collect(const char* name,int ok,const char* note) {
    int n=__atomic_load_n(&nrows,__ATOMIC_RELAXED);
    if(n==96){overflow=1;return;}
    kstrlcpy(rows[n].name,name,sizeof(rows[n].name));
    kstrlcpy(rows[n].note,note,sizeof(rows[n].note));
    rows[n].ok=ok;
    __atomic_store_n(&nrows,n+1,__ATOMIC_RELEASE);
}
static void worker(void) {
    if(!kjmp_set(&recovery)) {
        task_set_recover(&recovery);
        if(live_site==2)web_isolation_tests(collect);
        else if(live_site==5)web_libjs_tests(collect);
        else if(live_site==6)web_libjs_dom_tests(collect);
        else if(live_site){
            char* data=NULL;uint32_t len=0;char final[8192];
            const char* target=live_site==7?"https://www.google.com/search?q=Banana%20OS":
                               live_site==3?"https://www.google.com/search?gbv=1&q=Banana%20OS":
                               (live_site==4||live_site==8)?"https://github.com/qcom-toolbox/Banana-OS/releases":BROWSER_SITE_TEST_URL;
            int rc=browser_fetch_document(target,NULL,0,&data,&len,final,sizeof(final));
            collect("live site fetched",!rc,final);
            page_t* p=data?(live_site>=7?remote_page_new_libjs(browser_env(),0):remote_page_new(browser_env(),0)):NULL;
            if(p){remote_page_load(p,final,data,len,800);
                collect("live site parsed in isolated renderer",p->title[0]!=0,p->title);
                if((live_site==3||live_site==7) && strstr(final,"consent.google.com")){
                    char consent[256]={0};
                    remote_page_eval(p,
                        "(function(){var b=document.querySelectorAll('button,input[type=submit]');for(var i=0;i<b.length;i++){"
                        "var t=b[i].value||b[i].textContent;if(t.indexOf('Alle ablehnen')>=0||t.indexOf('Tout refuser')>=0||t.indexOf('Reject all')>=0){"
                        "b[i].click();return 'Rejected optional cookies';}}return 'Consent button not found';})()",
                        consent,sizeof(consent));
                    collect("Google optional-cookie consent handled in fresh test session",p->nav_pending!=0,consent);
                    if(!p->nav_pending){
                        remote_page_eval(p,"(function(){var b=document.getElementsByTagName('input'),s='inputs='+b.length+' ';for(var i=0;i<b.length&&i<12;i++)s+=b[i].getAttribute('type')+':'+b[i].getAttribute('value')+'|';return s;})()",consent,sizeof(consent));
                        collect("Google consent form diagnostics",1,consent);
                    }
                }
                for(int attempt=0;attempt<3;attempt++){
                    remote_page_tick(p);
                    if(!p->nav_pending)break;
                    char next[8192];kstrlcpy(next,p->nav,sizeof(next));
                    uint32_t post_len=p->nav_post_len;char* post=NULL;
                    if(post_len){post=kmalloc(post_len+1);if(post){memcpy(post,p->nav_post,post_len);post[post_len]=0;}}
                    p->nav_pending=0;remote_page_free(p);p=NULL;kfree(data);data=NULL;
                    rc=browser_fetch_document(next,post,post?post_len:0,&data,&len,final,sizeof(final));
                    if(post)kfree(post);
                    p=data?(live_site>=7?remote_page_new_libjs(browser_env(),0):remote_page_new(browser_env(),0)):NULL;
                    if(!p)break;
                    remote_page_load(p,final,data,len,800);
                }
                if(p){
                    char text[256]={0};
                    remote_page_eval(p,"document.title",text,sizeof(text));
                    collect("live document title available",text[0]!=0,text);
                    if(live_site==7){
                        remote_page_update(p,800);
                        uint32_t* pixels=kmalloc(800*480*4);
                        int painted=0;
                        if(pixels){
                            remote_render(p->layout,pixels,800,800,480,0,0,800,480,0);
                            for(int i=0;i<800*480;i++)if(pixels[i]!=0xffffff){painted=1;break;}
                            kfree(pixels);
                        }
                        collect("Google viewport rendered",painted,p->status);
                    }
                    collect("live scripts completed",p->status[0]==0,p->status);
                    if((live_site==3||live_site==7)){
                        remote_page_eval(p,"(function(){var a=document.getElementsByTagName('a'),n=0;for(var i=0;i<a.length;i++){var h=a[i].href||'';if(h.indexOf('http')===0&&h.indexOf('google.')<0&&h.indexOf('gstatic.')<0)n++;}return n>=3&&document.body.textContent.indexOf('Banana')>=0;})()",text,sizeof(text));
                        collect("Google search returned query results",!strcmp(text,"true"),text);
                        if(live_site==7&&p->status[0]){
                            remote_page_eval(p,"'solve='+typeof solveSimpleChallenge+' scripts='+document.getElementsByTagName('script').length",text,sizeof(text));
                            collect("Google script-loading diagnostics",1,text);
                            remote_page_eval(p,"'grecaptcha='+typeof grecaptcha+' iframes='+document.getElementsByTagName('iframe').length+' callback='+__bananaDiagnostics()",text,sizeof(text));
                            collect("Google widget diagnostics",1,text);
                            for(int i=0;i<4;i++){
                                char code[512];
                                ksnprintf(code,sizeof(code),"(function(){var s=document.getElementsByTagName('script')[%d];return s?('type='+s.getAttribute('type')+' nomodule='+s.hasAttribute('nomodule')+' src='+s.getAttribute('src')+' text='+s.textContent.slice(0,100)):'none'})()",i);
                                remote_page_eval(p,code,text,sizeof(text));
                                collect("Google script source diagnostics",1,text);
                            }
                        }
                    }else if((live_site==4||live_site==8)){
                        remote_page_eval(p, "document.body.innerHTML.indexOf('/releases/download/')>=0" ,text,sizeof(text));
                        collect("GitHub release downloads rendered",!strcmp(text,"true"),text);
                        remote_page_eval(p,"(function(){var a=document.getElementsByTagName('a'),s='links='+a.length+' ';for(var i=0;i<a.length&&i<3;i++)s+=a[i].getAttribute('href')+'|';return s;})()",text,sizeof(text));
                        collect("GitHub link diagnostics",1,text);
                    }
                    remote_page_free(p);
                }
            }else collect("live isolated renderer",0,"unavailable");
            if(data)kfree(data);
        }
        else framework_tests_run(collect);
    } else collect("browser engine completed",0,task_crash_msg());
    task_set_recover(NULL);
    __atomic_store_n(&done,1,__ATOMIC_RELEASE);
}
static void run_tests(void (*report)(const char*,int,const char*),int live) {
    if(__atomic_exchange_n(&running,1,__ATOMIC_ACQUIRE)) {
        report("browser tests",0,"already running in another shell");return;
    }
    __atomic_store_n(&nrows,0,__ATOMIC_RELAXED);
    live_site=live;
    overflow=0;__atomic_store_n(&done,0,__ATOMIC_RELAXED);
    if(!web_yield_hook)web_yield_hook=task_maybe_yield;
    int id=task_create_stack("browser-test",worker,8u<<20);
    if(id<0) report("browser test task",0,"could not allocate its stack");
    else {
        int printed=0;
        for(;;) {
            int finished=__atomic_load_n(&done,__ATOMIC_ACQUIRE);
            int count=__atomic_load_n(&nrows,__ATOMIC_ACQUIRE);
            while(printed<count) {
                report(rows[printed].name,rows[printed].ok,rows[printed].note);printed++;
            }
            if(finished)break;
            task_sleep_ms(10);
        }
        if(overflow)report("browser test report",0,"too many results");
    }
    __atomic_store_n(&running,0,__ATOMIC_RELEASE);
}
void browser_test_run(void (*report)(const char*,int,const char*)) {run_tests(report,0);}
void browser_site_test_run(void (*report)(const char*,int,const char*)) {run_tests(report,1);}

void browser_isolation_test_run(void (*report)(const char*,int,const char*)) {run_tests(report,2);}

void browser_compatibility_test_run(int google,void (*report)(const char*,int,const char*)) {run_tests(report,google?3:4);}

void browser_libjs_test_run(void (*report)(const char*,int,const char*)) {run_tests(report,5);}

void browser_libjs_dom_test_run(void (*report)(const char*,int,const char*)) {run_tests(report,6);}

void browser_libjs_live_test_run(int google,void (*report)(const char*,int,const char*)) {run_tests(report,google?7:8);}
