/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <banana.h>
#include <banana_web_worker.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../../ports/libjs/probe.h"
static ww_command_t command;
static char result[8192];
static unsigned used;
static void report(const char* name,int ok){
    if(used<sizeof(result)-1)
        used+=(unsigned)snprintf(result+used,sizeof(result)-used,"%s %s\n",ok?"ok":"FAIL",name);
    if(used>=sizeof(result))used=sizeof(result)-1;
}
static int ipc(int op,const void* in,unsigned len,void* out,unsigned cap){
    return __banana->web_ipc(op,in,len,out,cap);
}
/* Assertion diagnostics use the bounded result IPC, never a console/file bypass. */
void ak_assertion_handler(const char* message){
    report(message,0);
    struct {ww_io_t header;char data[8192];} packet;
    packet.header.kind=WW_RESULT;packet.header.offset=0;
    memcpy(packet.data,result,used+1);
    ipc(WW_OUTPUT,&packet,sizeof(packet.header)+used+1,NULL,0);
}
static int input(char* out,unsigned length){
    for(unsigned offset=0;offset<length;){
        unsigned n=length-offset;if(n>WW_CHUNK)n=WW_CHUNK;
        ww_io_t io={0,offset};
        if(ipc(WW_INPUT,&io,sizeof(io),out+offset,n)!=(int)n)return -1;
        offset+=n;
    }
    out[length]=0;return 0;
}
static int denied_resource(int kind,const char* url){
    ww_resource_t* request=calloc(1,sizeof(*request));
    if(!request)return 0;
    request->kind=kind;snprintf(request->url,sizeof(request->url),"%s",url);
    snprintf(request->method,sizeof(request->method),"GET");
    int sent=ipc(WW_RESOURCE,request,sizeof(*request),NULL,0);free(request);
    if(sent<0)return 0;
    ww_response_t response;unsigned start=__banana->ticks_ms();
    for(;;){
        int n=ipc(WW_RESPONSE,NULL,0,&response,sizeof(response));
        if(n==(int)sizeof(response)){
            response.error[sizeof(response.error)-1]=0;
            return response.result<0&&strstr(response.error,"permission")!=NULL;
        }
        if(n<0||__banana->ticks_ms()-start>3000)return 0;
        __banana->sleep_ms(1);
    }
}
extern void (*__init_array_start[])(void), (*__init_array_end[])(void);
int main(int argc,char** argv){
    (void)argc;(void)argv;
    if(!BANANA_HAS(__banana,web_ipc))return 125;
    /* The C SDK startup does not run ELF C++ initializers. Keep this private
     * to the optional engine; its GC type allocators require these constructors. */
    for(void (**f)(void)=__init_array_start;f<__init_array_end;++f)if(*f)(*f)();
    for(;;){
        int n=ipc(WW_WAIT,NULL,0,&command,sizeof(command));
        if(n<0)return 121;
        if(!n){__banana->sleep_ms(10);continue;}
        if(n!=(int)sizeof(command)||command.version!=WW_VERSION)return 121;
        ww_state_t state;memset(&state,0,sizeof(state));state.version=WW_VERSION;
        snprintf(state.title,sizeof(state.title),"LibJS prototype");
        result[0]=0;used=0;
        switch(command.op){
        case WW_INIT:break;
        case WW_LOAD:{
            int failed=banana_libjs_selftest(report);
            report(failed?"LibJS suite failed":"LibJS suite passed",!failed);
            state.result=failed?-1:0;break;
        }
        case WW_EVAL:{
            if(command.length>WW_DOCUMENT){state.result=-1;break;}
            char* code=malloc(command.length+1);
            if(!code||input(code,command.length)<0){free(code);return 120;}
            state.result=banana_libjs_eval(code,result,sizeof(result));free(code);
            used=(unsigned)strlen(result);break;
        }
        case WW_TEST:
            if(command.a[0]==4){
                state.result=(__banana->open("/etc/hostname",1)==-1&&
                    __banana->kcmd("echo escaped")==-1&&__banana->thread_create(NULL,NULL)==-1&&
                    denied_resource(WW_FILE,"/etc/hostname")&&
                    denied_resource(WW_FETCH,"https://example.com/"))?0:-1;
            }else state.result=-1;
            break;
        default:state.result=-1;break;
        }
        if(used){
            struct {ww_io_t header;char data[8192];} packet;
            packet.header.kind=WW_RESULT;packet.header.offset=0;
            memcpy(packet.data,result,used+1);
            if(ipc(WW_OUTPUT,&packet,sizeof(packet.header)+used+1,NULL,0)<0)return 121;
        }
        if(ipc(WW_FINISH,&state,sizeof(state),NULL,0)<0)return 121;
    }
}
