/* Compiler-private compatibility; not a second libc. Unsupported process
 * services return errors. The actual allocator, threads, files and JIT
 * mappings come from Banana's SDK. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <wchar.h>
#include <wctype.h>
#include <locale.h>
#include <langinfo.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <spawn.h>
#include <time.h>

/* GNU's static C++ facets require its C-locale layout. Keep this private
 * to the compiler DSO: SDK locale_t remains opaque and unchanged. */
struct __locale_struct {
    void* categories[13];
    const unsigned short* b;
    const int* lower;
    const int* upper;
    const char* names[13];
};
static struct __locale_struct classic;
static unsigned short masks[384];
static int lower[384], upper[384];
static pthread_once_t once=PTHREAD_ONCE_INIT;
static void locale_init(void) {
    for(int i=-128;i<256;i++) {
        int c=i<0?i+256:i; unsigned short m=0;
        if(isupper(c))m|=0x100;if(islower(c))m|=0x200;
        if(isalpha(c))m|=0x400;if(isdigit(c))m|=0x800;
        if(isxdigit(c))m|=0x1000;if(isspace(c))m|=0x2000;
        if(isprint(c))m|=0x4000;if(isgraph(c))m|=0x8000;
        if(c==' '||c=='\t')m|=1;if(iscntrl(c))m|=2;
        if(ispunct(c))m|=4;if(isalnum(c))m|=8;
        masks[i+128]=m;lower[i+128]=tolower(c);upper[i+128]=toupper(c);
    }
    lower[127]=upper[127]=-1;masks[127]=0;
    classic.b=masks+128;classic.lower=lower+128;classic.upper=upper+128;
    for(int i=0;i<13;i++)classic.names[i]="C";
}
static locale_t c_locale(void){pthread_once(&once,locale_init);return &classic;}
locale_t __newlocale(int mask,const char* name,locale_t base) {
    (void)mask;(void)base;
    if(!name || (strcmp(name,"C")&&strcmp(name,"POSIX")&&strcmp(name,"C.UTF-8")&&*name)) {errno=ENOENT;return NULL;}
    return c_locale();
}
locale_t __duplocale(locale_t l){(void)l;return c_locale();}
void __freelocale(locale_t l){(void)l;}
static __thread locale_t current;
locale_t __uselocale(locale_t l){locale_t old=current?current:c_locale();if(l)current=l==LC_GLOBAL_LOCALE?c_locale():l;return old;}
char* __nl_langinfo_l(nl_item i,locale_t l){(void)l;switch(i){case CODESET:return "UTF-8";case 65536:return ".";case 65539:return (char*)L".";case 65540:return (char*)L"";default:return "";}}
size_t __ctype_get_mb_cur_max(void){return 4;}
int __strcoll_l(const char*a,const char*b,locale_t l){(void)l;return strcmp(a,b);}
size_t __strxfrm_l(char*d,const char*s,size_t n,locale_t l){(void)l;return strxfrm(d,s,n);}
int __wcscoll_l(const wchar_t*a,const wchar_t*b,locale_t l){(void)l;return wcscmp(a,b);}
size_t __wcsxfrm_l(wchar_t*d,const wchar_t*s,size_t n,locale_t l){(void)l;return wcsxfrm(d,s,n);}
double __strtod_l(const char*s,char**e,locale_t l){(void)l;return strtod(s,e);}
float __strtof_l(const char*s,char**e,locale_t l){(void)l;return strtof(s,e);}
long double strtold_l(const char*s,char**e,locale_t l){(void)l;return strtold(s,e);}
size_t __strftime_l(char*d,size_t n,const char*f,const struct tm*t,locale_t l){(void)l;return strftime(d,n,f,t);}
size_t __wcsftime_l(wchar_t*d,size_t n,const wchar_t*f,const struct tm*t,locale_t l){(void)l;return wcsftime(d,n,f,t);}
static const char* classes[]={"alnum","alpha","blank","cntrl","digit","graph","lower","print","punct","space","upper","xdigit"};
unsigned long __wctype_l(const char*n,locale_t l){(void)l;for(int i=0;i<12;i++)if(!strcmp(n,classes[i]))return i+1;return 0;}
int __iswctype_l(wint_t c,unsigned long t,locale_t l){(void)l;if(c>127)return 0;switch(t){case 1:return isalnum(c);case 2:return isalpha(c);case 3:return c==' '||c=='\t';case 4:return iscntrl(c);case 5:return isdigit(c);case 6:return isgraph(c);case 7:return islower(c);case 8:return isprint(c);case 9:return ispunct(c);case 10:return isspace(c);case 11:return isupper(c);case 12:return isxdigit(c);default:return 0;}}
wint_t __towlower_l(wint_t c,locale_t l){(void)l;return c<128?(wint_t)tolower(c):c;}
wint_t __towupper_l(wint_t c,locale_t l){(void)l;return c<128?(wint_t)toupper(c):c;}
/* The compiler does not fork, use message catalogs or legacy encodings. */
char* gettext(const char*s){return (char*)s;}
char* dgettext(const char*d,const char*s){(void)d;return (char*)s;}
char* bindtextdomain(const char*d,const char*p){(void)d;(void)p;return "/";}
char* bind_textdomain_codeset(const char*d,const char*c){(void)d;return (char*)(c?c:"UTF-8");}
void* iconv_open(const char*to,const char*from){(void)to;(void)from;errno=ENOSYS;return (void*)-1;}
int iconv_close(void*p){(void)p;errno=EBADF;return -1;}
size_t iconv(void*p,char**s,size_t*n,char**d,size_t*m){(void)p;(void)s;(void)n;(void)d;(void)m;errno=EBADF;return (size_t)-1;}
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t*a,int x,int y){(void)a;(void)x;(void)y;return ENOSYS;}
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t*a,int x,const char*p,int f,mode_t m){(void)a;(void)x;(void)p;(void)f;(void)m;return ENOSYS;}
pid_t wait4(pid_t p,int*s,int o,struct rusage*r){pid_t v=waitpid(p,s,o);if(v>0&&r)memset(r,0,sizeof(*r));return v;}
/* No unwinding through dynamically generated code is supported. */
int _dl_find_object(void*p,void*r){(void)p;(void)r;return -1;}
char __libc_single_threaded=0;
uint32_t arc4random(void){uint32_t n;if(getrandom(&n,sizeof(n),0)!=sizeof(n))abort();return n;}
unsigned long __isoc23_strtoul(const char*s,char**e,int b){return strtoul(s,e,b);}
static void bounds(int ok){if(!ok){fputs("LLVM C++ buffer bound failure\n",stderr);abort();}}
ssize_t __read_chk(int f,void*d,size_t n,size_t cap){bounds(n<=cap);return read(f,d,n);}
int __fprintf_chk(FILE*f,int flags,const char*fmt,...){(void)flags;va_list a;va_start(a,fmt);int n=vfprintf(f,fmt,a);va_end(a);return n;}
wchar_t* __wmemcpy_chk(wchar_t*d,const wchar_t*s,size_t n,size_t cap){bounds(n<=cap);return wmemcpy(d,s,n);}
wchar_t* __wmemset_chk(wchar_t*d,wchar_t c,size_t n,size_t cap){bounds(n<=cap);return wmemset(d,c,n);}
size_t __mbsrtowcs_chk(wchar_t*d,const char**s,size_t n,mbstate_t*st,size_t cap){bounds(!d||n<=cap);return mbsrtowcs(d,s,n,st);}
size_t mbsnrtowcs(wchar_t*d,const char**src,size_t bytes,size_t n,mbstate_t*state){
    const char*s=*src;size_t used=0,count=0;mbstate_t local={0};if(!state)state=&local;
    while(used<bytes&&(!d||count<n)) {wchar_t c;size_t k=mbrtowc(&c,s,bytes-used,state);if(k==(size_t)-1){if(d)*src=s;return k;}if(k==(size_t)-2){used=bytes;s=*src+bytes;break;}if(!k){if(d){d[count]=0;*src=NULL;}return count;}if(d)d[count]=c;count++;used+=k;s+=k;}
    if(d)*src=s;return count;
}
size_t wcsnrtombs(char*d,const wchar_t**src,size_t chars,size_t n,mbstate_t*state){
    const wchar_t*s=*src;size_t count=0;mbstate_t local={0};if(!state)state=&local;
    for(size_t i=0;i<chars;i++){char b[8];mbstate_t saved=*state;size_t k=wcrtomb(b,*s,state);if(k==(size_t)-1){if(d)*src=s;return k;}if(d&&k>n-count){*state=saved;break;}if(!*s){if(d){memcpy(d+count,b,k);*src=NULL;}return count;}if(d)memcpy(d+count,b,k);count+=k;s++;}
    if(d)*src=s;return count;
}
