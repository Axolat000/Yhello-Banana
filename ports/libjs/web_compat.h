// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
static constexpr char web_compat[]=R"JS(
(()=>{
 // Binary DOM strings must stay in LibJS: a C UTF-8 string cannot represent
 // the Latin-1 bytes (including NUL) returned by the browser base64 APIs.
 const alphabet='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
 const invalidBase64=()=>{
   if(typeof DOMException==='function')throw new DOMException('Invalid base64 data','InvalidCharacterError');
   const e=new Error('Invalid base64 data');e.name='InvalidCharacterError';throw e;
 };
 globalThis.atob=function(data){
   if(!arguments.length||typeof data==='symbol')throw new TypeError('atob requires a string argument');
   let text=String(data).replace(/[\t\n\f\r ]/g,'');
   if(text.length%4===0)text=text.replace(/={1,2}$/,'');
   if(text.length%4===1||/[^A-Za-z0-9+/]/.test(text))invalidBase64();
   let bits=0,acc=0,codes=[],parts=[];
   for(let i=0;i<text.length;i++){
     acc=(acc<<6)|alphabet.indexOf(text[i]);bits+=6;
     if(bits>=8){
       bits-=8;codes.push((acc>>bits)&255);
       if(codes.length===4096){parts.push(String.fromCharCode(...codes));codes=[];}
     }
   }
   if(codes.length)parts.push(String.fromCharCode(...codes));
   return parts.join('');
 };
 globalThis.btoa=function(data){
   if(!arguments.length||typeof data==='symbol')throw new TypeError('btoa requires a string argument');
   const text=String(data),parts=[];let chunk='';
   for(let i=0;i<text.length;i+=3){
     const a=text.charCodeAt(i),b=text.charCodeAt(i+1),c=text.charCodeAt(i+2);
     if(a>255||b>255||c>255)invalidBase64();
     const n=(a<<16)|((b||0)<<8)|(c||0);
     chunk+=alphabet[(n>>>18)&63]+alphabet[(n>>>12)&63]+
       (i+1<text.length?alphabet[(n>>>6)&63]:'=')+
       (i+2<text.length?alphabet[n&63]:'=');
     if(chunk.length>=4096){parts.push(chunk);chunk='';}
   }
   if(chunk)parts.push(chunk);return parts.join('');
 };

 const domString=x=>{if(typeof x==='symbol')throw new TypeError('DOM string required');return String(x)};
 const U8=Uint8Array, AB=ArrayBuffer, DV=DataView;
 const typedProto=Object.getPrototypeOf(U8.prototype);
 const getter=(p,k)=>Object.getOwnPropertyDescriptor(p,k).get;
 const typedBuffer=getter(typedProto,'buffer'),typedOffset=getter(typedProto,'byteOffset'),
   typedLength=getter(typedProto,'byteLength'),typedTag=getter(typedProto,Symbol.toStringTag);
 const viewBuffer=getter(DV.prototype,'buffer'),viewOffset=getter(DV.prototype,'byteOffset'),
   viewLength=getter(DV.prototype,'byteLength'),bufferLength=getter(AB.prototype,'byteLength');
 const sharedLength=typeof SharedArrayBuffer==='function'?getter(SharedArrayBuffer.prototype,'byteLength'):null;
 const arraySet=U8.prototype.set;
 function bytesOf(input){
   if(input===undefined)return new U8(0);
   if(AB.isView(input)){
     let buffer,offset,length;
     try{buffer=typedBuffer.call(input);offset=typedOffset.call(input);length=typedLength.call(input)}
     catch(e){buffer=viewBuffer.call(input);offset=viewOffset.call(input);length=viewLength.call(input)}
     return new U8(buffer,offset,length);
   }
   try{bufferLength.call(input)}
   catch(e){if(!sharedLength)throw new TypeError('BufferSource required');sharedLength.call(input)}
   return new U8(input);
 }
 function utf8Write(text,dst){
   const capacity=typedLength.call(dst);
   let read=0,written=0;
   while(read<text.length){
     let cp=text.charCodeAt(read),units=1;
     if(cp>=0xd800&&cp<=0xdbff){
       const low=text.charCodeAt(read+1);
       if(low>=0xdc00&&low<=0xdfff){cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;units=2}
       else cp=0xfffd;
     }else if(cp>=0xdc00&&cp<=0xdfff)cp=0xfffd;
     const count=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
     if(capacity-written<count)break;
     if(count===1)dst[written++]=cp;
     else{
       if(count===2)dst[written++]=0xc0|(cp>>6);
       else if(count===3){dst[written++]=0xe0|(cp>>12);dst[written++]=0x80|((cp>>6)&63)}
       else{dst[written++]=0xf0|(cp>>18);dst[written++]=0x80|((cp>>12)&63);dst[written++]=0x80|((cp>>6)&63)}
       dst[written++]=0x80|(cp&63);
     }
     read+=units;
   }
   return {read,written};
 }
 globalThis.TextEncoder=class TextEncoder {
   #brand;
   get encoding(){this.#brand;return 'utf-8'}
   encode(input=undefined){
     this.#brand;
     const text=input===undefined?'':domString(input),out=new U8(text.length*3);
     const {written}=utf8Write(text,out);return out.slice(0,written);
   }
   encodeInto(input,dst){
     this.#brand;
     const text=domString(input);
     if(typedTag.call(dst)!=='Uint8Array')throw new TypeError('Uint8Array destination required');
     arraySet.call(dst,new U8(0)); // also rejects detached/out-of-bounds buffers
     return utf8Write(text,dst);
   }
 };
 // UTF-8 state machine from the Encoding Standard. Unsupported legacy labels
 // fail explicitly; no native-interpreter typed arrays or C strings are used.
 globalThis.TextDecoder=class TextDecoder {
   #fatal;#ignore;#streaming=false;#need=0;#seen=0;#cp=0;#lower=0x80;#upper=0xbf;#bom=false;
   constructor(label='utf-8',options={}){
     const name=domString(label).replace(/^[\t\n\f\r ]+|[\t\n\f\r ]+$/g,'').toLowerCase();
     if(!['unicode-1-1-utf-8','unicode11utf8','unicode20utf8','utf-8','utf8','x-unicode20utf8'].includes(name))
       throw new RangeError('Only UTF-8 decoding is available');
     options=options||{};this.#fatal=!!options.fatal;this.#ignore=!!options.ignoreBOM;
   }
   get encoding(){return 'utf-8'}
   get fatal(){return this.#fatal}
   get ignoreBOM(){return this.#ignore}
   decode(input=undefined,options={}){
     const bytes=bytesOf(input);
     if(!this.#streaming){this.#need=this.#seen=this.#cp=0;this.#lower=0x80;this.#upper=0xbf;this.#bom=false}
     this.#streaming=!!(options&&options.stream);
     const parts=[];let codes=[];
     const emit=cp=>{
       if(!this.#bom){this.#bom=true;if(!this.#ignore&&cp===0xfeff)return}
       codes.push(cp);if(codes.length===4096){parts.push(String.fromCodePoint(...codes));codes=[]}
     };
     const error=()=>{
       if(this.#fatal){this.#streaming=false;throw new TypeError('Invalid UTF-8 data')}
       emit(0xfffd);
     };
     let i=0;
     while(i<bytes.length){
       const byte=bytes[i];
       if(this.#need===0){
         i++;
         if(byte<=0x7f){emit(byte);continue}
         if(byte>=0xc2&&byte<=0xdf){this.#need=1;this.#cp=byte&31}
         else if(byte>=0xe0&&byte<=0xef){
           this.#need=2;this.#cp=byte&15;
           if(byte===0xe0)this.#lower=0xa0;if(byte===0xed)this.#upper=0x9f;
         }else if(byte>=0xf0&&byte<=0xf4){
           this.#need=3;this.#cp=byte&7;
           if(byte===0xf0)this.#lower=0x90;if(byte===0xf4)this.#upper=0x8f;
         }else error();
         continue;
       }
       if(byte<this.#lower||byte>this.#upper){
         this.#need=this.#seen=this.#cp=0;this.#lower=0x80;this.#upper=0xbf;error();continue;
       }
       i++;this.#lower=0x80;this.#upper=0xbf;
       this.#cp=(this.#cp<<6)|(byte&63);this.#seen++;
       if(this.#seen===this.#need){const cp=this.#cp;this.#need=this.#seen=this.#cp=0;emit(cp)}
     }
     if(!this.#streaming&&this.#need){this.#need=this.#seen=this.#cp=0;error()}
     if(codes.length)parts.push(String.fromCodePoint(...codes));return parts.join('');
   }
 };

})()
)JS";
