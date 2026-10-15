#ifndef APPMEM_H
#define APPMEM_H

#include "types.h"

/*
 * An app's memory, apart from the kernel's (kernel/app.c).
 *
 * Everything an app may write - its code and data, its stacks, what it
 * malloc()s, its windows' pixels - comes from its own chunks (4 MiB each,
 * on 4 MiB boundaries). While its code runs the processor uses page tables
 * of the app's own (appmem_cr3) where those chunks are writable and the
 * rest of memory, the kernel's, is read-only: a wild write stops the app
 * (a page fault, kernel/app.c's app_fault) instead of damaging the system.
 * The system calls switch to the kernel's tables and back (the gates).
 *
 * The 64-bit kernel only, for now: on the 32-bit one (no paging) the
 * chunks are the same but appmem_cr3() is 0 - nothing is switched.
 *
 * The block headers inside the chunks are the app's to scribble on, so the
 * allocator checks each one before it believes it: a broken heap fails
 * the app's allocation (NULL), it never makes the kernel write elsewhere.
 */

#define APPMEM_CHUNK (4u << 20)

typedef struct appmem appmem_t;

appmem_t* appmem_new(void);
void      appmem_destroy(appmem_t* m);            /* every chunk and table at once */
void*     appmem_alloc(appmem_t* m, size_t n);    /* 16-byte aligned, not zeroed; NULL */
void*     appmem_alloc_aligned(appmem_t* m, size_t n, uint32_t align);   /* align <= 4096 */
void      appmem_free(appmem_t* m, void* p);      /* also what appmem_alloc_aligned gave */
size_t    appmem_size(appmem_t* m, void* p);      /* payload bytes of a block, 0 if p is none */
int       appmem_owns(appmem_t* m, const void* p, size_t n);   /* [p, p + n) is all the app's */
uint32_t  appmem_used(appmem_t* m);               /* bytes in its blocks */
uintptr_t appmem_cr3(appmem_t* m);                /* its page tables, 0 if none */
/* the kernel's page tables changed over the app's chunks (guard pages):
 * its own copy follows */
void      appmem_sync(appmem_t* m);
/* the page tables at cr3 (an app's) let addr be written: a page fault there
 * came from an old TLB entry (its memory grew while another core ran it) -
 * the return from the fault reloads the tables, and the write is tried again */
int       appmem_tables_allow_write(uintptr_t cr3, uintptr_t addr);

/* the kernel's page tables (0 until appmem_init): the interrupt entry
 * stubs and the system call gates switch to these */
extern uintptr_t g_kernel_cr3;
/* at boot, after paging is final: [fault_base, + fault_size) - the fault
 * stacks every processor core may push an exception frame on while app
 * code runs - stays writable in every app's tables */
void appmem_init(uintptr_t fault_base, uint32_t fault_size);
/* each processor core, starting: read-only pages bind ring-0 code too */
void appmem_cpu_init(void);

#endif
