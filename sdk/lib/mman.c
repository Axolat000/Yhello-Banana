/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: mmap, munmap, mprotect (<sys/mman.h>) over the system's
 * vm_map / vm_unmap / vm_protect (API version 16). A file mapping is
 * anonymous memory the file is read into. */
#include <banana.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

extern const banana_api_t* __banana;

static int has_vm(void) {
    return __banana->version >= 16 && BANANA_HAS(__banana, vm_protect);
}

#define PAGE 4096ul

void* mmap(void* addr, size_t len, int prot, int flags, int fd, off_t off) {
    int anon = flags & MAP_ANONYMOUS;
    if (!len || (off & (PAGE - 1)) || (!anon && fd < 0) || !(flags & (MAP_SHARED | MAP_PRIVATE))) {
        errno = !anon && fd < 0 ? EBADF : EINVAL;
        return MAP_FAILED;
    }
    if (!has_vm()) { errno = ENOSYS; return MAP_FAILED; }
    if ((flags & MAP_FIXED) && ((uintptr_t)addr & (PAGE - 1))) { errno = EINVAL; return MAP_FAILED; }
    int bprot = prot & (PROT_READ | PROT_WRITE | PROT_EXEC);
    /* a file is read in first: writable until then */
    int first = anon ? bprot : (bprot | BANANA_PROT_READ | BANANA_PROT_WRITE);
    void* p = __banana->vm_map(addr, len, first, (flags & MAP_FIXED) ? BANANA_MAP_FIXED : 0);
    if (!p) { errno = ENOMEM; return MAP_FAILED; }
    if ((flags & MAP_FIXED_NOREPLACE) && p != addr) {
        __banana->vm_unmap(p, len);
        errno = EEXIST;
        return MAP_FAILED;
    }
    if (!anon) {
        long was = __banana->seek(fd, 0, BANANA_SEEK_CUR);
        if (was < 0 || __banana->seek(fd, off, BANANA_SEEK_SET) < 0) {
            __banana->vm_unmap(p, len);
            errno = EBADF;
            return MAP_FAILED;
        }
        size_t got = 0;
        while (got < len) {                       /* (past the end of the file: zeros) */
            long n = __banana->read(fd, (char*)p + got, len - got > (1u << 20) ? (1u << 20) : len - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        __banana->seek(fd, was, BANANA_SEEK_SET);
        if (first != bprot) __banana->vm_protect(p, len, bprot);
    }
    return p;
}

int munmap(void* addr, size_t len) {
    if (!has_vm() || ((uintptr_t)addr & (PAGE - 1)) || !len) { errno = EINVAL; return -1; }
    if (__banana->vm_unmap(addr, len) != 0) { errno = EINVAL; return -1; }
    return 0;
}

int mprotect(void* addr, size_t len, int prot) {
    if (!has_vm() || ((uintptr_t)addr & (PAGE - 1))) { errno = EINVAL; return -1; }
    if (!len) return 0;
    if (__banana->vm_protect(addr, len, prot & (PROT_READ | PROT_WRITE | PROT_EXEC)) != 0) { errno = ENOMEM; return -1; }
    return 0;
}

/* advice only: the pages stay as they are (MADV_DONTNEED does not clear them) */
int madvise(void* addr, size_t len, int advice) { (void)addr; (void)len; (void)advice; return 0; }
int posix_madvise(void* addr, size_t len, int advice) { (void)addr; (void)len; (void)advice; return 0; }
int msync(void* addr, size_t len, int flags) { (void)addr; (void)len; (void)flags; return 0; }
int mlock(const void* addr, size_t len) { (void)addr; (void)len; return 0; }
int munlock(const void* addr, size_t len) { (void)addr; (void)len; return 0; }
