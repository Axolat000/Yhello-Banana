#ifndef WEBP_STDLIB_H
#define WEBP_STDLIB_H
#include <stddef.h>
#include <stdint.h>
void* kmalloc(size_t);
void* kzalloc(size_t);
void kfree(void*);
static inline void* malloc(size_t n) { return n <= UINT32_MAX ? kmalloc(n) : NULL; }
static inline void* calloc(size_t n, size_t size) { return size && n > UINT32_MAX / size ? NULL : kzalloc(n*size); }
static inline void free(void* p) { kfree(p); }
static inline int abs(int n) { return n < 0 ? -n : n; }
/* Upstream's common palette utilities also contain encoder helpers. Their
 * small palette sort needs no allocator or host runtime (decoding never calls it). */
static inline void qsort(void* base, size_t n, size_t size, int (*compare)(const void*,const void*)) {
    unsigned char* bytes=base;
    for(size_t i=1;i<n;i++)for(size_t j=i;j && compare(bytes+(j-1)*size,bytes+j*size)>0;j--)
        for(size_t k=0;k<size;k++) { unsigned char t=bytes[(j-1)*size+k];bytes[(j-1)*size+k]=bytes[j*size+k];bytes[j*size+k]=t; }
}
#endif
