// SPDX-License-Identifier: GPL-3.0-or-later
#include "probe.h"
#include <LibJS/Bytecode/Interpreter.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/VM.h>
#include <LibJS/Script.h>
#include <stdio.h>
#include <string.h>

struct Probe { char const* name; char const* source; };
static Probe const probes[] = {
    {"arithmetic", "1 + 2 === 3"},
    {"Set and Map", "(()=>{const s=new Set([1,2,2]);const m=new Map([['x',7]]);return s.size===2&&s.has(2)&&!s.has(9)&&m.get('x')===7&&m.has('x')})()"},
    {"WeakMap identity", "(()=>{const a={},b={};const m=new WeakMap([[a,4]]);return m.has(a)&&!m.has(b)&&m.get(a)===4})()"},
    {"closures and defaults", "((x=3,...rest)=>()=>x+rest.length)(undefined,8,9)()===5"},
    {"classes and super", "(()=>{class A{f(){return 3}}class B extends A{f(){return super.f()+4}}return new B().f()===7})()"},
    {"Proxy and Reflect", "(()=>{const p=new Proxy({x:2},{get(t,k,r){return Reflect.get(t,k,r)*3}});return p.x===6})()"},
    {"BigInt", "12345678901234567890n*10n===123456789012345678900n"},
    {"Unicode regular expressions", "/^\\p{L}+$/u.test('caf\u00e9')&&!/^\\p{L}+$/u.test('42')"},
    {"JSON decoding", "JSON.parse('{\"x\":[1,true,\"ok\"],\"z\":null}').x[2]==='ok'"},
    {"optional chaining", "({x:{y:8}})?.x?.y===8&&(null?.x??9)===9"},
    {"property descriptors", "(()=>{const o={};Object.defineProperty(o,'x',{get(){return 12}});return o.x===12&&Object.getOwnPropertyDescriptor(o,'x').get instanceof Function})()"},
    {"Intl formatting", "new Intl.NumberFormat('en-US').format(1234)==='1,234'"},
    {"Promise scheduling", "globalThis.__probePromise=0;Promise.resolve(8).then(v=>{globalThis.__probePromise=v+1});true"},
    {"Promise delivery", "globalThis.__probePromise===9"},
    {"async and await", "globalThis.__probeAsync=0;(async()=>{globalThis.__probeAsync=await Promise.resolve(5)})();true"},
    {"async delivery", "globalThis.__probeAsync===5"},
    {"allocation and collection", "(()=>{let n=0;for(let i=0;i<10000;i++){const a=Array.from({length:10},(_,j)=>j);n+=a[9]}return n===90000})()"},
};

extern "C" unsigned banana_libjs_test_count() { return sizeof(probes)/sizeof(probes[0]); }

extern "C" int banana_libjs_selftest(void (*report)(char const*,int))
{
    auto vm = JS::VM::create();
    auto context = JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    unsigned failed=0;
    for(auto const& probe:probes){
        auto script=JS::Script::parse(StringView(probe.source,strlen(probe.source)),*context->realm,"banana-libjs-probe"sv);
        if(script.is_error()){
            report(probe.name,0);failed++;continue;
        }
        auto result=vm->bytecode_interpreter().run(*script.release_value());
        bool ok=!result.is_error()&&result.value().is_boolean()&&result.value().as_bool();
        report(probe.name,ok);
        if(!ok)failed++;
        vm->run_queued_promise_jobs();
    }
    vm->pop_execution_context();
    return (int)failed;
}

extern "C" int banana_libjs_eval(char const* code,char* output,unsigned capacity)
{
    if(!capacity)return -1;
    snprintf(output,capacity,"JavaScript evaluation failed");
    auto vm=JS::VM::create();
    auto context=JS::create_simple_execution_context<JS::GlobalObject>(*vm);
    int status=-1;
    do{
        auto script=JS::Script::parse(StringView(code,strlen(code)),*context->realm,"banana-libjs-probe"sv);
        if(script.is_error()){snprintf(output,capacity,"JavaScript parse error");break;}
        auto result=vm->bytecode_interpreter().run(*script.release_value());
        if(result.is_error()){snprintf(output,capacity,"JavaScript threw an exception");break;}
        auto text=result.value().to_string(*vm);
        if(text.is_error())break;
        auto bytes=text.value().bytes();
        auto count=bytes.size()<capacity-1?bytes.size():capacity-1;
        memcpy(output,bytes.data(),count);output[count]=0;
        status=0;
    }while(false);
    vm->run_queued_promise_jobs();
    vm->pop_execution_context();
    return status;
}
#ifndef __BANANA_OS__
static void native_report(char const* name,int ok){printf("%s %s\n",ok?"ok":"FAIL",name);}
int main(int argc,char** argv)
{
    if(argc==3&&!strcmp(argv[1],"--eval")){
        char output[8192];int rc=banana_libjs_eval(argv[2],output,sizeof(output));
        puts(output);return rc?1:0;
    }
    int failed=banana_libjs_selftest(native_report);
    printf("LibJS prototype: %u passed, %d failed\n",banana_libjs_test_count()-failed,failed);
    return failed?1:0;
}
#endif
