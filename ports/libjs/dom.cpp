// SPDX-License-Identifier: GPL-3.0-or-later
#include "dom.h"
#include "web_compat.h"
#include <AK/Vector.h>
#include <LibGC/Root.h>
#include <LibGC/RootVector.h>
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/AbstractOperations.h>
#include <LibJS/Runtime/Error.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/NativeFunction.h>
#include <LibJS/Runtime/VM.h>
#include <LibJS/Script.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
using namespace JS;
static RefPtr<VM> shared_vm;
static unsigned sessions;
struct DOM;
struct Binding { DOM* session=nullptr; };
static Vector<OwnPtr<Binding>> bindings;
static Utf16FlyString key(char const* s){return Utf16FlyString::from_utf8(StringView(s,strlen(s)));}
struct DOM {
    RefPtr<VM> vm;
    NonnullOwnPtr<ExecutionContext> context;
    GC::Root<Realm> realm;
    void* host;
    Binding* binding=nullptr;
    char last_error[256]={};
    Vector<bjs_value> handles;
    Vector<GC::Root<Object>> foreign;
    Vector<GC::Root<Object>> wrappers;
    unsigned depth=0;
    DOM(void* h):vm(shared_vm),context(create_simple_execution_context<GlobalObject>(*vm)),
        realm(context->realm),host(h){vm->pop_execution_context();++sessions;}
    ~DOM(){if(!--sessions)shared_vm=nullptr;}
    void enter(){if(!depth++)vm->push_execution_context(*context);}
    void leave(){if(!--depth)vm->pop_execution_context();}
    ThrowCompletionOr<Value> failure(){return vm->throw_completion<TypeError>(ErrorType::NotAnObject,"unsupported or exhausted DOM bridge");}
    ThrowCompletionOr<Value> from_host(bjs_value w){
        switch(w.kind){
        case 0:return js_undefined();
        case 1:return js_null();
        case 2:return Value(w.number!=0);
        case 3:return Value(w.number);
        case 4:return Value(PrimitiveString::create(*vm,String::from_utf8_with_replacement_character(StringView(w.text?w.text:"",strlen(w.text?w.text:"")))));
        case 8:case 9:if(w.token<foreign.size())return Value(foreign[w.token].cell());return failure();
        case 5:case 6:break;
        default:return failure();
        }
        unsigned id=0;
        for(;id<handles.size();id++)if(handles[id].kind==w.kind&&handles[id].token==w.token)break;
        if(id==handles.size()){
            if(id>=16384)return failure();
            handles.append(w);wrappers.append({});
        }
        if(wrappers[id])return Value(wrappers[id].cell());
        auto wrap=TRY(realm->global_object().get(key("__bananaWrap")));
        auto value=TRY(JS::call(*vm,wrap,js_undefined(),Value(id),Value(w.kind==6),Value(w.array!=0)));
        if(!value.is_object())return failure();
        wrappers[id]=GC::Root<Object>(value.as_object());
        return value;
    }
    ThrowCompletionOr<bjs_value> to_host(Value v,Vector<ByteString>& strings){
        bjs_value out={};
        if(v.is_undefined())return out;
        if(v.is_null()){out.kind=1;return out;}
        if(v.is_boolean()){out.kind=2;out.number=v.as_bool();return out;}
        if(v.is_number()){out.kind=3;out.number=v.as_double();return out;}
        if(v.is_string()){
            auto str=TRY(v.to_string(*vm));strings.append(str.to_byte_string());out.kind=4;out.text=strings.last().characters();return out;
        }
        if(!v.is_object())return TRY(failure()),out;
        auto& object=v.as_object();
        for(unsigned i=0;i<wrappers.size();i++)if(wrappers[i].cell()==&object)return handles[i];
        unsigned id=0;
        for(;id<foreign.size();id++)if(foreign[id].cell()==&object)break;
        if(id==foreign.size()){
            if(id>=16384)return TRY(failure()),out;
            foreign.append(GC::Root<Object>(object));
        }
        out.kind=v.is_function()?9:8;out.token=id;return out;
    }
    ThrowCompletionOr<Value> host_op(int op){
        if(op==6)return Value(PrimitiveString::create(*vm,
            String::from_utf8_with_replacement_character(StringView(last_error,strlen(last_error)))));
        Vector<ByteString> strings;
        bjs_value object={},out={};
        char const* name="";
        Vector<bjs_value> args;
        if(op==3){
            auto s=TRY(vm->argument(0).to_string(*vm));strings.append(s.to_byte_string());name=strings.last().characters();
        }else{
            auto id=vm->argument(0);
            if(!id.is_number()||!isfinite(id.as_double())||id.as_double()<0||id.as_double()>=handles.size()||
               id.as_double()!=(unsigned)id.as_double())return failure();
            object=handles[(unsigned)id.as_double()];
            if(op==2||op==7){
                args.append(TRY(to_host(vm->argument(1),strings)));
                auto av=vm->argument(2);
                auto length=TRY(av.get(*vm,vm->names.length));
                if(!length.is_number()||!isfinite(length.as_double())||length.as_double()<0||length.as_double()>128||length.as_double()!=(unsigned)length.as_double())return failure();
                for(unsigned i=0;i<(unsigned)length.as_double();i++){
                    auto v=TRY(av.get(*vm,PropertyKey(i)));args.append(TRY(to_host(v,strings)));
                }
            }else if(op!=5){
                auto s=TRY(vm->argument(1).to_string(*vm));strings.append(s.to_byte_string());name=strings.last().characters();
                if(op==1)args.append(TRY(to_host(vm->argument(2),strings)));
            }
        }
        char error[256]={};
        if(bjs_host(host,op,object,name,args.data(),args.size(),&out,error,sizeof(error))<0)
            return vm->throw_completion<TypeError>(ErrorType::NotAnObject,StringView(error,strlen(error)));
        return from_host(out);
    }
    int run(char const* source,unsigned length,char const* name,char* error,unsigned cap){
        enter();
        int rc=-1;
        auto script=Script::parse(StringView(source,length),*realm,name?StringView(name,strlen(name)):"page"sv);
        if(script.is_error()){snprintf(last_error,sizeof(last_error),"LibJS parse error");if(cap)snprintf(error,cap,"LibJS parse error");}
        else{
            auto result=vm->bytecode_interpreter().run(*script.release_value());
            if(!result.is_error())rc=0;
            else if(cap){
                auto exception=result.error().value();
                auto text=exception.to_string(*vm);
                snprintf(last_error,sizeof(last_error),"%s",text.is_error()?"LibJS exception":text.value().to_byte_string().characters());
                if(exception.is_object()){
                    auto stack=exception.as_object().get(key("stack"));
                    if(!stack.is_error()&&stack.value().is_string()){
                        auto detail=stack.value().to_string(*vm);
                        if(!detail.is_error())snprintf(last_error,sizeof(last_error),"%s",detail.value().to_byte_string().characters());
                    }
                }
                if(text.is_error())snprintf(error,cap,"LibJS exception");
                else snprintf(error,cap,"LibJS (%s): %s",name?name:"script",text.value().to_byte_string().characters());
            }
        }
        leave();return rc;
    }
};
static constexpr char bootstrap[]=R"JS(
(()=>{
 const cache=new Map, reverse=new WeakMap;
 globalThis.__bananaWrap=(id,callable,array)=>{
   if(cache.has(id))return cache.get(id);
   const target=callable?function(){}:array?[]:{};
   const proxy=new Proxy(target,{
     get(t,k,r){
       const desc=Reflect.getOwnPropertyDescriptor(t,k);
       if(desc&&!desc.configurable&&!desc.writable&&'value' in desc)return desc.value;
       if(typeof k==='symbol')return Reflect.get(t,k,r);
       const v=__bananaGet(id,k);
       return v===undefined?Reflect.get(t,k,r):v;
     },
     set(t,k,v){if(typeof k==='symbol')return Reflect.set(t,k,v);__bananaSet(id,String(k),v);return true},
     has(t,k){return typeof k==='string'&&__bananaGet(id,k)!==undefined||Reflect.has(t,k)},
     apply(t,self,args){return __bananaCall(id,self,args)},
     construct(t,args,newTarget){const v=__bananaConstruct(id,newTarget,args);if(v===null||typeof v!=='object'&&typeof v!=='function')throw new TypeError('DOM constructor');return v}
   });
   cache.set(id,proxy);reverse.set(proxy,id);
   if(!callable&&!array){const proto=__bananaProto(id);if(proto!==undefined)Reflect.setPrototypeOf(target,proto);}
   return proxy;
 };
 globalThis.window=globalThis.self=globalThis.frames=globalThis;
 const hostWindow=__bananaGlobal('window');
 const windowId=reverse.get(hostWindow);
 // Return the actual global for native Window values, preserving event identity.
 for(const [id,value] of cache)if(value===hostWindow)cache.set(id,globalThis);
 for(const name of [
 'document','location','navigator','screen','localStorage','sessionStorage',
 'Event','CustomEvent','MouseEvent','KeyboardEvent','InputEvent','FocusEvent',
 'HTMLElement','Element','Node','Document','Text','Comment','DocumentFragment',
 'Image','XMLHttpRequest','matchMedia','getSelection','MutationObserver',
 'IntersectionObserver','ResizeObserver','PerformanceObserver',
 'requestIdleCallback','cancelIdleCallback','scrollBy','history','customElements',
 'banana','__readFile','alert','confirm','prompt','setTimeout','clearTimeout','setInterval',
 'clearInterval','requestAnimationFrame','cancelAnimationFrame','addEventListener',
 'removeEventListener','dispatchEvent','getComputedStyle','scrollTo','open','close',
 'postMessage','atob','btoa','performance','console','devicePixelRatio'
 ]) {
   const value=__bananaGlobal(name);
   if(value!==undefined)globalThis[name]=value;
 }
 // fetch uses the existing IPC-backed host implementation, assimilated into
 // genuine LibJS promises. No direct filesystem or socket API is installed.
 for(const name of ['innerWidth','innerHeight','outerWidth','outerHeight',
   'scrollX','scrollY','pageXOffset','pageYOffset','location','onload','top','parent']){
   Object.defineProperty(globalThis,name,{
     configurable:true,enumerable:true,
     get(){return __bananaGet(windowId,name)},
     set(value){__bananaSet(windowId,name,value)}
   });
 }
 const hostFetch=__bananaGlobal('fetch');
 if(hostFetch)globalThis.fetch=(...args)=>Promise.resolve(hostFetch(...args));
})()
)JS";
extern "C" void* bjs_create(void* host){
    if(!shared_vm)shared_vm=VM::create();
    if(bindings.size()>=4096)return nullptr;
    auto* d=new DOM(host);auto guard=make<Binding>();guard->session=d;
    d->binding=guard.ptr();Binding* binding=guard.ptr();bindings.append(move(guard));
    d->enter();
    auto& global=d->realm->global_object();
    char const* names[]={"__bananaGet","__bananaSet","__bananaCall","__bananaGlobal","__bananaProto","__bananaDiagnostics","__bananaConstruct"};
    int operations[]={0,1,2,3,5,6,7};
    for(int index=0;index<7;index++){
        int op=operations[index];
        auto fn=NativeFunction::create(*d->realm,key(names[index]),[binding,op](VM& vm)->ThrowCompletionOr<Value>{
            if(!binding->session)return vm.throw_completion<TypeError>(ErrorType::NotAnObject,"detached DOM realm");
            return binding->session->host_op(op);
        });
        global.define_direct_property(key(names[index]),fn,Attribute::Configurable);
    }
    d->leave();
    char error[256]={};
    if(d->run(bootstrap,sizeof(bootstrap)-1,"DOM bootstrap",error,sizeof(error))<0){bjs_destroy(d);return nullptr;}
    if(d->run(web_compat,sizeof(web_compat)-1,"Web codecs",error,sizeof(error))<0){bjs_destroy(d);return nullptr;}
    bjs_value window={},unused={};char diagnostic[8];
    if(!bjs_host(host,3,unused,"window",nullptr,0,&window,diagnostic,sizeof(diagnostic)))
        for(unsigned i=0;i<d->handles.size();i++)
            if(d->handles[i].kind==window.kind&&d->handles[i].token==window.token)
                d->wrappers[i]=GC::Root<Object>(global);
    return d;
}
extern "C" void bjs_destroy(void* ctx){
    auto* d=static_cast<DOM*>(ctx);
    if(d->binding)d->binding->session=nullptr;
    delete d;
    // Guards survive detached realms that another realm still references.
    // Release them only after the final VM and all its functions are gone.
    if(!sessions)bindings.clear();
}
extern "C" char const* bjs_error(void* ctx){return static_cast<DOM*>(ctx)->last_error;}
extern "C" int bjs_run(void* ctx,char const* src,unsigned len,char const* name,char* error,unsigned cap){
    return static_cast<DOM*>(ctx)->run(src,len,name,error,cap);
}
extern "C" int bjs_global(void* ctx,char const* name,bjs_value* out){
    auto& d=*static_cast<DOM*>(ctx);d.enter();int rc=-1;Vector<ByteString> strings;
    auto v=d.realm->global_object().get(key(name));
    if(!v.is_error()){auto w=d.to_host(v.value(),strings);if(!w.is_error()){*out=w.value();rc=0;}}
    // Strings must outlive this function; globals used by native event delivery
    // are callbacks/objects. Copy string values through the page's arena host.
    if(!rc&&out->kind==4){
        bjs_value copied={};char error[8];
        if(bjs_host(d.host,4,{},nullptr,out,1,&copied,error,sizeof(error))<0)rc=-1;
        else *out=copied;
    }
    d.leave();return rc;
}
extern "C" void bjs_checkpoint(void* ctx){auto& d=*static_cast<DOM*>(ctx);if(d.depth)return;d.enter();d.vm->run_queued_promise_jobs();d.leave();}
extern "C" int bjs_foreign(void* ctx,unsigned id,int op,char const* name,bjs_value* args,unsigned count,bjs_value* out){
    auto& d=*static_cast<DOM*>(ctx);if(id>=d.foreign.size())return -1;
    d.enter();int rc=-1;Vector<ByteString> strings;
    auto work=[&]()->ThrowCompletionOr<bjs_value>{
        Value value=d.foreign[id].cell();
        if(op==0)value=TRY(value.get(*d.vm,key(name)));
        else if(op==1&&count){TRY(value.as_object().set(key(name),TRY(d.from_host(args[0])),Object::ShouldThrowExceptions::Yes));value=js_undefined();}
        else if(op==2&&count){
            GC::RootVector<Value> av {d.vm->heap()};
            for(unsigned i=1;i<count;i++){
                av.append(TRY(d.from_host(args[i])));
#ifdef BANANA_LIBJS_SESSION_TEST
                d.vm->heap().collect_garbage();
#endif
            }
            value=TRY(JS::call(*d.vm,value,TRY(d.from_host(args[0])),av.span()));
        }
        else return TRY(d.failure()),bjs_value{};
        auto w=TRY(d.to_host(value,strings));
        // Copy transient UTF-8 text into the legacy page arena via its value API.
        if(w.kind==4){bjs_value copied={};char error[8];bjs_value a[]={w};
            // op 4 is a string copy; no JS execution.
            if(bjs_host(d.host,4,{},nullptr,a,1,&copied,error,sizeof(error))<0)return TRY(d.failure()),w;
            return copied;
        }
        return w;
    };
    auto w=work();if(!w.is_error()){*out=w.value();rc=0;}
    else{
        auto error=w.error().value().to_string(*d.vm);
        if(error.is_error())snprintf(d.last_error,sizeof(d.last_error),"LibJS callback threw");
        else snprintf(d.last_error,sizeof(d.last_error),"%s",error.value().to_byte_string().characters());
    }
    d.leave();return rc;
}
