/* SPDX-License-Identifier: BSD-2-Clause */
/* Exercise GNU C++ stream/facet ABI and the SDK's multibyte state using
 * the exact runtime linked into the JIT DSO. Called by selftest llvmpipe. */
#include <sstream>
#include <string>
#include <wchar.h>
extern "C" int banana_llvm_runtime_check(void) {
    static_assert(sizeof(mbstate_t)==8, "GNU stream state ABI");
    std::ostringstream text; text << 42 << ' ' << 1.25;
    if(text.str()!="42 1.25")return 1;
    std::wostringstream wide; wide << 42 << L' ' << 1.25;
    if(wide.str()!=L"42 1.25")return 2;
    mbstate_t state={};wchar_t c=0;
    if(mbrtowc(&c,"\xe2",1,&state)!=(size_t)-2)return 3;
    if(mbrtowc(&c,"\x82\xac",2,&state)!=2 || c!=0x20ac)return 4;
    char bytes[8];state={};
    if(wcrtomb(bytes,c,&state)!=3 || (unsigned char)bytes[0]!=0xe2 || (unsigned char)bytes[1]!=0x82 || (unsigned char)bytes[2]!=0xac)return 5;
    return 0;
}
