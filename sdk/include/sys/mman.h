#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 * mmap and friends (Banana OS 0.6, API version 16). On the 64-bit system a
 * mapping reserves addresses and its pages get memory - zeroed - when
 * first touched, so a big range (a Java heap) costs only what is used;
 * PROT_NONE / PROT_READ are enforced (a bad access stops the app, or goes
 * to its SIGSEGV handler). A file mapping is a private copy of the file:
 * MAP_SHARED writes are not written back to it. The 32-bit system has no
 * paging: memory is handed out right away and protection is not enforced.
 * The values are Linux's.
 */
#include <sys/types.h>

#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_NORESERVE 0x4000      /* (every mapping is like that here) */
#define MAP_FIXED_NOREPLACE 0x100000
#define MAP_FAILED    ((void*)-1)

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4
#define MADV_FREE       8
#define POSIX_MADV_NORMAL    0
#define POSIX_MADV_DONTNEED  4

#define MS_ASYNC      1
#define MS_INVALIDATE 2
#define MS_SYNC       4

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off);
int   munmap(void* addr, size_t len);
int   mprotect(void* addr, size_t len, int prot);
int   madvise(void* addr, size_t len, int advice);
#define MFD_CLOEXEC       1
#define MFD_ALLOW_SEALING 2
int   memfd_create(const char* name, unsigned int flags);   /* (none here: ENOSYS) */
int   posix_madvise(void* addr, size_t len, int advice);
int   msync(void* addr, size_t len, int flags);
int   mlock(const void* addr, size_t len);
int   munlock(const void* addr, size_t len);
int   mincore(void* addr, size_t len, unsigned char* vec);   /* (every page counts as there) */
#define mmap64 mmap

#ifdef __cplusplus
}
#endif
#endif
