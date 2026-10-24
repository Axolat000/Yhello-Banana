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
    check(run(first,"const bytes=Array.from({length:256},(_,i)=>String.fromCharCode(i)).join('');if(atob(btoa(bytes))!==bytes||atob('AP+A').charCodeAt(1)!==255)throw Error('binary base64')"),"all byte values survive base64 including NUL");
    check(run(first,"if(btoa(String.fromCharCode(233))!=='6Q=='||atob('YQ')!=='a'||atob('YWI')!=='ab'||atob(' Y Q = = ')!=='a')throw Error('base64 grammar')"),"Latin-1 and forgiving base64 grammar");
    check(run(first,"for(const s of ['a','a=','====','YQ===','YQ==!']){let ok=false;try{atob(s)}catch(e){ok=e.name==='InvalidCharacterError'}if(!ok)throw Error('invalid base64 accepted')}"),"invalid padding and alphabet rejected");
    check(run(first,"let ok=false;try{btoa(String.fromCharCode(256))}catch(e){ok=e.name==='InvalidCharacterError'}if(!ok)throw Error('non Latin-1 accepted')"),"non Latin-1 encoding rejected");
    check(run(first,"for(const f of [atob,btoa]){let ok=false;try{f()}catch(e){ok=e instanceof TypeError}if(!ok)throw Error('missing argument accepted')}"),"base64 functions require an argument");
    check(run(first,"for(const f of [atob,btoa]){let ok=false;try{f(Symbol('x'))}catch(e){ok=e instanceof TypeError}if(!ok)throw Error('Symbol coercion accepted')}"),"base64 DOM string conversion rejects Symbols");
    check(run(first,"const many=bytes.repeat(40);if(atob(btoa(many))!==many)throw Error('chunk boundary')"),"base64 chunk boundaries preserve binary data");
    check(run(first,"const enc=new TextEncoder,dec=new TextDecoder;const txt='A'+String.fromCharCode(233)+String.fromCodePoint(0x1f34c)+String.fromCharCode(0);if(dec.decode(enc.encode(txt))!==txt||enc.encode(txt).join(',')!=='65,195,169,240,159,141,140,0')throw Error('UTF-8 roundtrip')"),"UTF-8 text codecs preserve Unicode and NUL");
    check(run(first,"{const dst=new Uint8Array(5),r=enc.encodeInto('A'+String.fromCodePoint(0x1f34c)+'B',dst);if(r.read!==3||r.written!==5||dec.decode(dst)!=='A'+String.fromCodePoint(0x1f34c))throw Error('encodeInto')}"),"encodeInto counts UTF-16 units and complete UTF-8 sequences");
    check(run(first,"{const r=enc.encodeInto(String.fromCodePoint(0x1f34c),new Uint8Array(3));if(r.read||r.written||dec.decode(enc.encode(String.fromCharCode(0xd800)))!==String.fromCharCode(0xfffd))throw Error('surrogate')}"),"short destinations and lone surrogates");
    check(run(first,"{const a=new Uint8Array([255,65,0,255]);if(dec.decode(new DataView(a.buffer,1,2))!=='A'+String.fromCharCode(0))throw Error('view offset')}"),"TextDecoder uses BufferSource offsets");
    check(run(first,"{const d=new TextDecoder;const a=d.decode(new Uint8Array([240,159]),{stream:true}),b=d.decode(new Uint8Array([141,140]));if(a!==''||b!==String.fromCodePoint(0x1f34c))throw Error('stream')}"),"UTF-8 streaming preserves split code points");
    check(run(first,"{const b=new Uint8Array([239,187,191,65]);if(dec.decode(b)!=='A'||new TextDecoder('utf8',{ignoreBOM:true}).decode(b)!==String.fromCharCode(0xfeff)+'A')throw Error('BOM')}"),"UTF-8 BOM handling");
    check(run(first,"{const d=new TextDecoder;if(d.decode(new Uint8Array([239]),{stream:true})!==''||d.decode(new Uint8Array([187,191,65]))!=='A')throw Error('stream BOM')}"),"BOM split across streaming chunks");
    check(run(first,"{const cases=[[[192,128],2],[[237,160,128],3],[[244,144,128,128],4],[[226,130],1]];for(const [b,n] of cases)if(dec.decode(new Uint8Array(b))!==String.fromCharCode(0xfffd).repeat(n))throw Error('invalid UTF-8')}"),"malformed UTF-8 and truncation follow replacement rules");
    check(run(first,"{const d=new TextDecoder('utf-8',{fatal:true});let ok=false;try{d.decode(new Uint8Array([192]))}catch(e){ok=e instanceof TypeError}if(!ok||d.decode(new Uint8Array([65]))!=='A')throw Error('fatal decoder')}"),"fatal decoding throws and recovers");
    check(run(first,"{let count=0;for(const f of [()=>dec.decode([65]),()=>enc.encodeInto('a',new Uint16Array(1)),()=>new TextDecoder('windows-1252')]){try{f()}catch(e){count++}}if(count!==3)throw Error('codec validation')}"),"codec buffer types and unsupported labels are rejected");
    check(run(first,"{let n=0;for(const f of [()=>TextEncoder.prototype.encode.call({},'a'),()=>TextEncoder.prototype.encodeInto.call({},'a',new Uint8Array(1)),()=>Object.getOwnPropertyDescriptor(TextEncoder.prototype,'encoding').get.call({}),()=>TextDecoder.prototype.decode.call({})]){try{f()}catch(e){if(e instanceof TypeError)n++}}if(n!==4)throw Error('codec receiver')}"),"text codec methods validate their receiver");
    check(run(first,"{const a=new Uint8Array(1);Object.defineProperty(a,'length',{value:100});const r=enc.encodeInto('ab',a);if(r.read!==1||r.written!==1)throw Error('spoofed destination length')}"),"encodeInto honors the actual buffer capacity");
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
    char diagnostic[256];const char* thrown="throw new Error('diagnostic marker')";
    check(bjs_run(first,thrown,strlen(thrown),"diagnostic test",diagnostic,sizeof(diagnostic))<0&&strstr(bjs_error(first),"diagnostic marker"),"script exception stack retained for host diagnostics");
    check(bjs_run(first,"throw 42",8,"primitive diagnostic",diagnostic,sizeof(diagnostic))<0&&!strcmp(bjs_error(first),"42"),"primitive exceptions replace stale diagnostic stacks");
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
