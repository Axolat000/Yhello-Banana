#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include "site_test.h"
#include "render.h"
#include "image_decode.h"
void host_fonts(void);
typedef struct {char* data;size_t len;} body_t;
static size_t receive(char* data,size_t size,size_t count,void* ctx) {
    body_t* b=ctx;size_t n=size*count;
    if(n>(8u<<20)-b->len)return 0;
    char* p=realloc(b->data,b->len+n+1);if(!p)return 0;b->data=p;
    memcpy(p+b->len,data,n);b->len+=n;p[b->len]=0;return n;
}
static int fetch(void* ctx,const char* url,char** data,uint32_t* len,char* type,int tc,char* final,int fc,char* err,int ec) {
    (void)ctx;body_t b={0};CURL* h=curl_easy_init();if(!h)return -1;
    curl_easy_setopt(h,CURLOPT_URL,url);curl_easy_setopt(h,CURLOPT_FOLLOWLOCATION,1L);
    curl_easy_setopt(h,CURLOPT_PROTOCOLS_STR,"http,https");curl_easy_setopt(h,CURLOPT_REDIR_PROTOCOLS_STR,"http,https");
    curl_easy_setopt(h,CURLOPT_TIMEOUT,20L);curl_easy_setopt(h,CURLOPT_CONNECTTIMEOUT,10L);
    curl_easy_setopt(h,CURLOPT_USERAGENT,"Mozilla/5.0 (Banana OS) BananaBrowser/1.0");
    curl_easy_setopt(h,CURLOPT_WRITEFUNCTION,receive);curl_easy_setopt(h,CURLOPT_WRITEDATA,&b);
    CURLcode rc=curl_easy_perform(h);long status=0;char* mime=NULL,*fin=NULL;
    curl_easy_getinfo(h,CURLINFO_RESPONSE_CODE,&status);curl_easy_getinfo(h,CURLINFO_CONTENT_TYPE,&mime);curl_easy_getinfo(h,CURLINFO_EFFECTIVE_URL,&fin);
    snprintf(type,tc,"%s",mime?mime:"");snprintf(final,fc,"%s",fin?fin:url);
    fprintf(stderr,"fetch %ld %zu %s\n",status,b.len,url);
    if(rc || status>=400) {
        snprintf(err,ec,"HTTP %ld: %s",status,curl_easy_strerror(rc));free(b.data);curl_easy_cleanup(h);return -1;
    }
    curl_easy_cleanup(h);if(!b.data)b.data=calloc(1,1);
    *data=b.data;*len=(uint32_t)b.len;return b.data?0:-1;
}
static void log_line(void* ctx,const char* s){(void)ctx;fprintf(stderr,"%s\n",s);}
static int passed,failed;
static void report(const char* name,int ok,const char* note) {
    printf("%s %s%s%s\n",ok?"ok  ":"FAIL",name,*note?" - ":"",note);fflush(stdout);
    if(ok)passed++;else failed++;
}
int main(int argc,char** argv) {
    host_fonts();curl_global_init(CURL_GLOBAL_DEFAULT);
    page_env_t env={.fetch=fetch,.log=log_line,.decode_image=web_decode_image};page_t* p=NULL;
    int rc=site_tests_run(&env,384u<<20,report,&p);
    if(p && argc>1 && p->layout) {
        int w=p->width,h=p->layout->height;if(h>2400)h=2400;if(h<800)h=800;
        uint32_t* pixels=calloc((size_t)w*h,4);
        if(pixels) {
            render_page(p->layout,pixels,w,w,h,0,0,w,h,0);
            FILE* f=fopen(argv[1],"wb");
            if(f){fprintf(f,"P6\n%d %d\n255\n",w,h);for(int i=0;i<w*h;i++){unsigned char rgb[]={pixels[i]>>16,pixels[i]>>8,pixels[i]};fwrite(rgb,1,3,f);}fclose(f);}
            free(pixels);
        }
    }
    if(p)page_free(p);
    curl_global_cleanup();
    printf("SWM: %d passed, %d failed\n",passed,failed);return rc || failed?1:0;
}
