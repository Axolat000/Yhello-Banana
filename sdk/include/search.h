/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SEARCH_H
#define _SEARCH_H
#ifdef __cplusplus
extern "C" {
#endif
/* binary search trees (tsearch), linear search, one hash table (hsearch) */
#include <stddef.h>
typedef enum { preorder, postorder, endorder, leaf } VISIT;
void* tsearch(const void* key, void** root, int (*cmp)(const void*, const void*));
void* tfind(const void* key, void* const* root, int (*cmp)(const void*, const void*));
void* tdelete(const void* key, void** root, int (*cmp)(const void*, const void*));
void  twalk(const void* root, void (*action)(const void* node, VISIT v, int depth));
void  tdestroy(void* root, void (*free_key)(void* key));
void* lfind(const void* key, const void* base, size_t* n, size_t size, int (*cmp)(const void*, const void*));
void* lsearch(const void* key, void* base, size_t* n, size_t size, int (*cmp)(const void*, const void*));
typedef struct entry { char* key; void* data; } ENTRY;
typedef enum { FIND, ENTER } ACTION;
int    hcreate(size_t n);
void   hdestroy(void);
ENTRY* hsearch(ENTRY item, ACTION action);
#ifdef __cplusplus
}
#endif
#endif
