/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../web/page.h"
char* worker_github_releases(page_env_t*,const char*,const char*,uint32_t,uint32_t*);
static int fetched,checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int fetch(void* ctx,const char* url,char** data,uint32_t* length,char* type,int tc,char* final,int fc,char* err,int ec){
    (void)ctx;(void)err;(void)ec;
    CHECK(!strcmp(url,"https://github.com/test/project/releases/expanded_assets/v1"));
    const char* fragment="<ul><li><a href='/test/project/releases/download/v1/a.zip'>a.zip</a></li>"
        "<li><a href='/test/project/archive/refs/tags/v1.zip'>Source code</a></li>"
        "<li><a href='javascript:bad/releases/download/no'>bad</a></li></ul>";
    *length=strlen(fragment);*data=strdup(fragment);
    snprintf(type,tc,"text/html");snprintf(final,fc,"%s",url);fetched++;return 0;
}
int main(int argc,char** argv){
    page_env_t env={.fetch=fetch};uint32_t length=0;
    const char* source="<html><body><div class='Box'><h2><a href='/test/project/releases/tag/v1'>Release &lt;one&gt;</a></h2>"
        "<div class='markdown-body'><p>Notes &amp; fixes</p></div>"
        "<include-fragment src='/test/project/signatures/v1'></include-fragment>"
        "<include-fragment src='/test/project/releases/expanded_assets/v1'></include-fragment></div>"
        "<a href='/test/project/releases/tag/v1'>duplicate</a><script>evil()</script></body></html>";
    char* result=worker_github_releases(&env,"https://github.com/test/project/releases",source,strlen(source),&length);
    CHECK(result&&length==strlen(result));
    CHECK(fetched==1);
    CHECK(strstr(result,"Release &lt;one&gt;"));
    CHECK(strstr(result,"Notes &amp; fixes"));
    CHECK(strstr(result,"https://github.com/test/project/releases/download/v1/a.zip"));
    CHECK(strstr(result,"https://github.com/test/project/archive/refs/tags/v1.zip"));
    CHECK(!strstr(result,"javascript:")&&!strstr(result,"evil()")&&!strstr(result,"<script"));
    free(result);
    CHECK(!worker_github_releases(&env,"https://github.com.evil/test/project/releases",source,strlen(source),&length));
    CHECK(!worker_github_releases(&env,"https://github.com/test/project/releases/download/v1/a.zip",source,strlen(source),&length));
    CHECK(!worker_github_releases(&env,"https://github.com/test/project/issues",source,strlen(source),&length));
    CHECK(!worker_github_releases(&env,"https://github.com/test/project/releases","<html>not releases</html>",25,&length));
    if(argc==2){
        FILE* f=fopen(argv[1],"rb");CHECK(f!=NULL);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);
        char* data=malloc(n+1);CHECK(fread(data,1,n,f)==(size_t)n);data[n]=0;fclose(f);
        /* A real captured release page is parsed without fetching from this fixture. */
        env.fetch=NULL;
        result=worker_github_releases(&env,"https://github.com/qcom-toolbox/Banana-OS/releases",data,n,&length);
        CHECK(result&&strstr(result,"Banana OS"));
        free(data);free(result);
    }
    printf("PASS: %d GitHub releases reader checks\n",checks);return 0;
}
