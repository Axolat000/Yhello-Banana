#include "browser_test.h"
#include "task.h"
#include "kstring.h"
#include "../web/framework_test.h"
#include "../web/arena.h"
#include "../web/site_test.h"
#include "browser.h"

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
        if(live_site)site_tests_run(browser_env(),browser_page_mem(),collect,NULL);
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
