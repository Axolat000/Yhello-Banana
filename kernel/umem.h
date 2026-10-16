#ifndef UMEM_H
#define UMEM_H

#include "types.h"
#include "uspace.h"

/*
 * An app's memory: its code, stacks, malloc() and window pictures all come
 * from 2 MiB blocks that belong to it alone (opened to it in its address
 * space, kernel/uspace.c). The bookkeeping - which pages and which small
 * pieces are in use - is kept in kernel memory, out of the app's reach: an
 * app that writes past its buffers damages only itself, never the kernel's
 * heap. The blocks stay the app's until it exits (a system call another of
 * its threads is in may still be using memory it frees), and are zeroed
 * when it gets them.
 */

typedef struct umem umem_t;

umem_t* umem_create(uspace_t* us);              /* us: NULL on the 32-bit kernel */
void    umem_destroy(umem_t* m);                /* gives every block back */

void*   umem_alloc(umem_t* m, size_t size);     /* 16-byte aligned; NULL if out of memory */
void    umem_free(umem_t* m, void* p);          /* ignores anything that is not a block of m */
void*   umem_realloc(umem_t* m, void* p, size_t size);
size_t  umem_size(umem_t* m, const void* p);   /* the block's usable size, 0 if p is none */
/* whole 4 KiB pages; the lowest guard bytes (a multiple of 4 KiB) fault */
void*   umem_alloc_pages(umem_t* m, size_t size, size_t guard);
void    umem_free_pages(umem_t* m, void* p, size_t guard);
/* the size of the umem_alloc_pages() run starting at p (no guard), 0 if none */
size_t  umem_pages_size(umem_t* m, const void* p);

uint32_t umem_used(umem_t* m);                  /* bytes handed out */

/* 1 if [p, p + n) lies wholly in m's blocks */
int     umem_owns(umem_t* m, const void* p, size_t n);
/* 1 if s is a NUL-terminated string in m's blocks */
int     umem_owns_str(umem_t* m, const char* s);

#endif
