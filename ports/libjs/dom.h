/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BANANA_LIBJS_DOM_H
#define BANANA_LIBJS_DOM_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Process-local C boundary, never an IPC structure. Tokens are not exposed
 * as addresses to JavaScript; the C++ side validates its own object indices. */
typedef struct { int kind,array; double number; const char* text; uint64_t token; } bjs_value;
void* bjs_create(void* host);
void bjs_destroy(void*);
const char* bjs_error(void*);
int bjs_run(void*,const char*,unsigned,const char*,char*,unsigned);
int bjs_global(void*,const char*,bjs_value*);
int bjs_foreign(void*,unsigned,int,const char*,bjs_value*,unsigned,bjs_value*);
void bjs_checkpoint(void*);
int bjs_host(void*,int,bjs_value,const char*,bjs_value*,unsigned,bjs_value*,char*,unsigned);
bjs_value bjs_host_foreign(void*,unsigned,int);
#ifdef __cplusplus
}
#endif
#endif
