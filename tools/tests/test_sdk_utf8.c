/* Linked against the actual SDK conversion module, with the host only
 * providing errno/assert/stdio. Cover every Unicode scalar and split. */
#include <wchar.h>
#include <assert.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
int main(void){
    for(unsigned c=0;c<=0x10ffff;c++){
        if(c>=0xd800&&c<=0xdfff)continue;
        char b[4];mbstate_t s={0};size_t n=wcrtomb(b,(wchar_t)c,&s);assert(n>=1&&n<=4);
        for(size_t cut=0;cut<n;cut++){
            memset(&s,0,sizeof(s));wchar_t out=-1;
            if(cut){assert(mbrtowc(&out,b,cut,&s)==(size_t)-2);assert(!mbsinit(&s));}
            assert(mbrtowc(&out,b+cut,n-cut,&s)==(c?n-cut:0));assert(out==(wchar_t)c);assert(mbsinit(&s));
        }
    }
    const unsigned char bad[][5]={{0xc0,0x80},{0xed,0xa0,0x80},{0xf4,0x90,0x80,0x80},{0xe0,0x80,0x80},{0xf0,0x80,0x80,0x80},{0xf5},{0x80},{0xe2,'A'}};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){mbstate_t s={0};errno=0;assert(mbrtowc(NULL,(const char*)bad[i],4,&s)==(size_t)-1);assert(errno==EILSEQ);assert(mbsinit(&s));}
    mbstate_t s={0};assert(mbrtowc(NULL,"\xe2",1,&s)==(size_t)-2);errno=0;assert(mbrtowc(NULL,NULL,0,&s)==(size_t)-1);assert(errno==EILSEQ);
    assert(wcrtomb(NULL,0x110000,&s)==1);assert(mbsinit(&s));
    puts("SDK UTF-8: every Unicode scalar and split, invalid sequences and reset passed");
}
