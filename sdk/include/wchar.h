#ifndef _WCHAR_H
#define _WCHAR_H
#ifdef __cplusplus
extern "C" {
#endif
/* wide characters: wchar_t is 32-bit (UTF-32), as on Linux */
#include <stddef.h>
#include <stdarg.h>
typedef unsigned int wint_t;
typedef struct { unsigned int __state; } mbstate_t;
#define WEOF ((wint_t)-1)
#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif

size_t   wcslen(const wchar_t* s);
wchar_t* wcscpy(wchar_t* d, const wchar_t* s);
wchar_t* wcsncpy(wchar_t* d, const wchar_t* s, size_t n);
int      wcscmp(const wchar_t* a, const wchar_t* b);
int      wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* wcschr(const wchar_t* s, wchar_t c);
wchar_t* wcsrchr(const wchar_t* s, wchar_t c);
wchar_t* wcscat(wchar_t* d, const wchar_t* s);
wchar_t* wmemcpy(wchar_t* d, const wchar_t* s, size_t n);
wchar_t* wmemset(wchar_t* d, wchar_t c, size_t n);
size_t   mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps);
size_t   wcrtomb(char* s, wchar_t wc, mbstate_t* ps);
int      mbsinit(const mbstate_t* ps);
size_t   mbstowcs(wchar_t* d, const char* s, size_t n);
size_t   wcstombs(char* d, const wchar_t* s, size_t n);
int      wctomb(char* s, wchar_t wc);
int      mbtowc(wchar_t* pwc, const char* s, size_t n);
int      swprintf(wchar_t* s, size_t n, const wchar_t* fmt, ...);
wint_t   btowc(int c);
int      wctob(wint_t c);
struct tm;
size_t   wcsftime(wchar_t* s, size_t max, const wchar_t* fmt, const struct tm* tm);
#ifdef __cplusplus
}
#endif
#endif
