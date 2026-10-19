/* SPDX-License-Identifier: BSD-2-Clause */
/* Restartable UTF-8 conversion. Keep state in the first word so older
 * four-byte C mbstate_t clients remain compatible. */
#include <wchar.h>
#include <errno.h>
#ifdef __x86_64__
static __thread mbstate_t read_state, write_state;
#else
static mbstate_t read_state, write_state;
#endif
size_t mbrtowc(wchar_t* out,const char* s,size_t n,mbstate_t* ps) {
    if(!ps)ps=&read_state;
    if(!s){s="";n=1;out=0;}
    if(!n)return (size_t)-2;
    unsigned state=ps->__state,cp=state&0x1fffff,remaining=(state>>21)&3,total=(state>>23)&3;
    size_t i=0;
    if(!remaining) {
        unsigned c=(unsigned char)s[i++];
        if(c<0x80){ps->__state=0;if(out)*out=c;return c?1:0;}
        if(c>=0xc2&&c<=0xdf){remaining=total=1;cp=c&31;}
        else if(c>=0xe0&&c<=0xef){remaining=total=2;cp=c&15;}
        else if(c>=0xf0&&c<=0xf4){remaining=total=3;cp=c&7;}
        else goto invalid;
    }
    while(remaining&&i<n){
        unsigned c=(unsigned char)s[i++];if((c&0xc0)!=0x80)goto invalid;
        cp=(cp<<6)|(c&63);remaining--;
    }
    if(remaining){ps->__state=cp|(remaining<<21)|(total<<23);return (size_t)-2;}
    if(!total||cp<(total==1?0x80:total==2?0x800:0x10000)||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))goto invalid;
    ps->__state=0;if(out)*out=(wchar_t)cp;return i;
invalid:
    ps->__state=0;errno=EILSEQ;return (size_t)-1;
}
size_t wcrtomb(char* s,wchar_t wc,mbstate_t* ps){
    if(!ps)ps=&write_state;
    ps->__state=0;
    if(!s)return 1; /* equivalent to encoding L'\0' into an internal buffer */
    unsigned v=(unsigned)wc;
    if(v>=0xd800&&v<=0xdfff){errno=EILSEQ;return (size_t)-1;}
    if(v<0x80){s[0]=v;return 1;}
    if(v<0x800){s[0]=0xc0|(v>>6);s[1]=0x80|(v&63);return 2;}
    if(v<0x10000){s[0]=0xe0|(v>>12);s[1]=0x80|((v>>6)&63);s[2]=0x80|(v&63);return 3;}
    if(v<=0x10ffff){s[0]=0xf0|(v>>18);s[1]=0x80|((v>>12)&63);s[2]=0x80|((v>>6)&63);s[3]=0x80|(v&63);return 4;}
    errno=EILSEQ;return (size_t)-1;
}
int mbsinit(const mbstate_t* ps){return !ps||!ps->__state;}
