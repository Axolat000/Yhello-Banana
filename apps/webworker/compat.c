/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <banana.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "../../kernel/types.h"
void* kmalloc(size_t n) { void* p = malloc(n); if (!p && n) __banana->exit(120); return p; }
void* kzalloc(size_t n) { void* p = kmalloc(n); if (p) memset(p,0,n); return p; }
void* krealloc(void* p, size_t n) { void* q = realloc(p,n); if (!q && n) __banana->exit(120); return q; }
void kfree(void* p) { free(p); }
uint32_t kheap_largest_free(void) { return 96u << 20; }
uint32_t timer_ms(void) { return __banana->ticks_ms(); }
uint32_t random_u32(void) { return __banana->random(); }
size_t kstrlcpy(char* d,const char* s,size_t n) { size_t l=strlen(s); if(n){size_t k=l<n-1?l:n-1;memcpy(d,s,k);d[k]=0;}return l; }
size_t kstrlcat(char* d,const char* s,size_t n) { size_t l=strlen(d);return l+(l<n?kstrlcpy(d+l,s,n-l):strlen(s)); }
int kvsnprintf(char* d,size_t n,const char* fmt,va_list ap) { return vsnprintf(d,n,fmt,ap); }
int ksnprintf(char* d,size_t n,const char* fmt,...) { va_list ap;va_start(ap,fmt);int r=vsnprintf(d,n,fmt,ap);va_end(ap);return r; }
void memset32(uint32_t* p,uint32_t v,size_t n) { while(n--)*p++=v; }
int k_isdigit(int c) { return c>='0'&&c<='9'; }
int k_isspace(int c) { return c==' '||(c>='\t'&&c<='\r'); }
int k_tolower(int c) { return c>='A'&&c<='Z'?c+32:c; }
int k_parse_u32(const char* s,uint32_t* out) { int n=0;uint32_t v=0;while(k_isdigit(s[n])){v=v*10+s[n++]-'0';}*out=v;return n; }
