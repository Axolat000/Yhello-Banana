// SPDX-License-Identifier: GPL-3.0-or-later
// Include the implementation to retain a real cross-realm function reference;
// the exported adapter deliberately has no operation for sharing raw JS values.
#define BANANA_LIBJS_SESSION_TEST
#include "dom.cpp"
extern "C" bjs_value bjs_host_foreign(void*,unsigned,int){return {};}
extern "C" int bjs_host(void*,int op,bjs_value,char const* name,bjs_value* args,unsigned count,bjs_value* out,char*,unsigned){
    *out={};
    if(op==3&&name&&!strcmp(name,"window")){out->kind=5;out->token=1;}
    if(op==5)out->kind=1;
    if(op==0&&name){
        if(!strcmp(name,"innerWidth")||!strcmp(name,"innerHeight")){out->kind=3;out->number=640;}
        if(!strcmp(name,"top")||!strcmp(name,"parent")){out->kind=5;out->token=1;}
    }
    if(op==4&&count){
        static char copy[8192];snprintf(copy,sizeof(copy),"%s",args[0].text);
        *out=args[0];out->text=copy;
    }
    return 0;
}
static unsigned checks;
static void check(bool ok,char const* name){++checks;if(!ok){fprintf(stderr,"FAIL %s\n",name);__builtin_trap();}}
static bool run(void* ctx,char const* code){char error[512]={};int rc=bjs_run(ctx,code,strlen(code),"session test",error,sizeof(error));if(rc)fprintf(stderr,"%s\n",error);return !rc;}
int main(){
    auto* first=static_cast<DOM*>(bjs_create(nullptr));
    auto* second=static_cast<DOM*>(bjs_create(nullptr));
    check(first&&second,"two realms");
    check(run(first,"globalThis.marker=41;const lexical=7;function increment(){return marker+1}"),"persistent declarations");
    check(run(first,"if(lexical!==7||increment()!==42)throw Error('lost realm')"),"persistent lexical and function");
    check(run(second,"if(typeof marker!=='undefined')throw Error('realm leak')"),"realm separation");
    check(run(first,"if(window!==globalThis||parent!==window||innerWidth!==640)throw Error('Window identity')"),"window alias");
    check(run(first,"for(const id of [NaN,Infinity,-1,16384,0.5]){let blocked=false;try{__bananaGet(id,'x')}catch(e){blocked=e instanceof TypeError}if(!blocked)throw Error('forged handle')}"),"invalid object handles");
    check(run(first,"globalThis.job=0;Promise.resolve(9).then(x=>job=x)"),"promise scheduled");
    bjs_checkpoint(first);
    check(run(first,"if(job!==9)throw Error('lost job')"),"promise delivered");
    check(run(first,"globalThis.joinArgs=(...args)=>args.join(':')"),"callback argument fixture");
    bjs_value function={},out={},arguments[5]={};
    check(!bjs_global(first,"joinArgs",&function)&&function.kind==9,"callback conversion");
    const char* words[]={"one","two","three","four"};
    for(unsigned i=0;i<4;i++){arguments[i+1].kind=4;arguments[i+1].text=words[i];}
    check(!bjs_foreign(first,function.token,2,nullptr,arguments,5,&out)&&
          out.kind==4&&!strcmp(out.text,"one:two:three:four"),
          "callback arguments survive collection between conversions");
    first->enter();
    auto fn=first->realm->global_object().get(key("__bananaGet"));
    check(!fn.is_error()&&fn.value().is_function(),"retained native function");
    auto function_value=fn.release_value();
    GC::Root<Object> retained(function_value.as_object());
    first->leave();
    second->enter();
    second->realm->global_object().define_direct_property(key("retained"),retained.cell(),Attribute::Configurable);
    second->leave();
    bjs_destroy(first);
    check(run(second,"let blocked=false;try{retained(0,'x')}catch(e){blocked=e.name==='TypeError'&&String(e).includes('detached DOM realm')}if(!blocked)throw Error('detached realm')"),"detached callback rejected");
    retained={};
    bjs_destroy(second);
    check(!shared_vm&&bindings.is_empty(),"last realm releases VM and guards");
    auto* replacement=bjs_create(nullptr);
    check(replacement&&run(replacement,"if(typeof marker!=='undefined')throw Error('stale state')"),"new VM after teardown");
    bjs_destroy(replacement);
    printf("LibJS DOM sessions: %u checks passed\n",checks);
}
