/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _WCHAR_H
#define _WCHAR_H
#ifdef __cplusplus
extern "C" {
#endif
/* wide characters: wchar_t is 32-bit (UTF-32), as on Linux. Wide-character
 * stdio reads and writes UTF-8 through the byte streams. */
#include <stddef.h>
#include <stdarg.h>
typedef unsigned int wint_t;
/* Match the GNU C++ stream ABI used by optional native-code ports.
 * The UTF-8 decoder uses the first word; the second is reserved. */
typedef struct { unsigned int __state, __reserved; } __mbstate_t;
typedef __mbstate_t mbstate_t;
#define WEOF ((wint_t)-1)
#ifndef WCHAR_MIN
#define WCHAR_MIN (-2147483647 - 1)
#define WCHAR_MAX 2147483647
#endif
typedef struct _bfile FILE;
struct tm;

size_t   wcslen(const wchar_t* s);
wchar_t* wcscpy(wchar_t* d, const wchar_t* s);
wchar_t* wcsncpy(wchar_t* d, const wchar_t* s, size_t n);
int      wcscmp(const wchar_t* a, const wchar_t* b);
int      wcsncmp(const wchar_t* a, const wchar_t* b, size_t n);
int      wcscoll(const wchar_t* a, const wchar_t* b);
size_t   wcsxfrm(wchar_t* d, const wchar_t* s, size_t n);
wchar_t* wcschr(const wchar_t* s, wchar_t c);
wchar_t* wcsrchr(const wchar_t* s, wchar_t c);
wchar_t* wcscat(wchar_t* d, const wchar_t* s);
wchar_t* wcsncat(wchar_t* d, const wchar_t* s, size_t n);
size_t   wcsspn(const wchar_t* s, const wchar_t* accept);
size_t   wcscspn(const wchar_t* s, const wchar_t* reject);
wchar_t* wcspbrk(const wchar_t* s, const wchar_t* accept);
wchar_t* wcsstr(const wchar_t* h, const wchar_t* n);
wchar_t* wcstok(wchar_t* s, const wchar_t* delim, wchar_t** save);
wchar_t* wmemcpy(wchar_t* d, const wchar_t* s, size_t n);
wchar_t* wmemmove(wchar_t* d, const wchar_t* s, size_t n);
wchar_t* wmemset(wchar_t* d, wchar_t c, size_t n);
int      wmemcmp(const wchar_t* a, const wchar_t* b, size_t n);
wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n);
long               wcstol(const wchar_t* s, wchar_t** end, int base);
unsigned long      wcstoul(const wchar_t* s, wchar_t** end, int base);
long long          wcstoll(const wchar_t* s, wchar_t** end, int base);
unsigned long long wcstoull(const wchar_t* s, wchar_t** end, int base);
double             wcstod(const wchar_t* s, wchar_t** end);
float              wcstof(const wchar_t* s, wchar_t** end);
long double        wcstold(const wchar_t* s, wchar_t** end);
size_t   mbrtowc(wchar_t* pwc, const char* s, size_t n, mbstate_t* ps);
size_t   mbrlen(const char* s, size_t n, mbstate_t* ps);
size_t   wcrtomb(char* s, wchar_t wc, mbstate_t* ps);
int      mbsinit(const mbstate_t* ps);
size_t   mbsrtowcs(wchar_t* d, const char** s, size_t n, mbstate_t* ps);
size_t   wcsrtombs(char* d, const wchar_t** s, size_t n, mbstate_t* ps);
size_t   mbstowcs(wchar_t* d, const char* s, size_t n);
size_t   wcstombs(char* d, const wchar_t* s, size_t n);
int      wctomb(char* s, wchar_t wc);
int      mbtowc(wchar_t* pwc, const char* s, size_t n);
wint_t   btowc(int c);
int      wctob(wint_t c);
size_t   wcsftime(wchar_t* s, size_t max, const wchar_t* fmt, const struct tm* tm);
/* wide stdio */
wint_t   fgetwc(FILE* f);
wint_t   getwc(FILE* f);
wint_t   getwchar(void);
wchar_t* fgetws(wchar_t* s, int n, FILE* f);
wint_t   fputwc(wchar_t c, FILE* f);
wint_t   putwc(wchar_t c, FILE* f);
wint_t   putwchar(wchar_t c);
int      fputws(const wchar_t* s, FILE* f);
wint_t   ungetwc(wint_t c, FILE* f);
int      fwide(FILE* f, int mode);
int      swprintf(wchar_t* s, size_t n, const wchar_t* fmt, ...);
int      vswprintf(wchar_t* s, size_t n, const wchar_t* fmt, va_list ap);
int      fwprintf(FILE* f, const wchar_t* fmt, ...);
int      vfwprintf(FILE* f, const wchar_t* fmt, va_list ap);
int      wprintf(const wchar_t* fmt, ...);
int      vwprintf(const wchar_t* fmt, va_list ap);
int      swscanf(const wchar_t* s, const wchar_t* fmt, ...);
int      vswscanf(const wchar_t* s, const wchar_t* fmt, va_list ap);
int      fwscanf(FILE* f, const wchar_t* fmt, ...);
int      vfwscanf(FILE* f, const wchar_t* fmt, va_list ap);
int      wscanf(const wchar_t* fmt, ...);
int      vwscanf(const wchar_t* fmt, va_list ap);
#ifdef __cplusplus
}
#endif
#endif
