/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: POSIX bits over the system calls - file descriptors
 * (open, lseek, fstat, stat), aligned memory, fdopen - for programs
 * ported from elsewhere. */
#include <banana.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern const banana_api_t* __banana;

int __ufd_drm_open(void);

int open(const char* path, int flags, ...) {
    /* the GPU (an Intel one's 3D: ufd.c) */
    if (path && (!strcmp(path, "/dev/dri/card0") || !strcmp(path, "/dev/dri/renderD128"))) return __ufd_drm_open();
    int f = 0;
    int acc = flags & O_ACCMODE;
    if (acc == O_RDONLY || acc == O_RDWR) f |= BANANA_O_READ;
    if (acc == O_WRONLY || acc == O_RDWR) f |= BANANA_O_WRITE;
    if (flags & O_CREAT)  f |= BANANA_O_CREATE;
    if (flags & O_TRUNC)  f |= BANANA_O_TRUNC;
    if (flags & O_APPEND) f |= BANANA_O_APPEND;
    if ((flags & O_EXCL) && (flags & O_CREAT)) {
        banana_stat_t st;
        if (__banana->stat(path, &st) == 0) { errno = EEXIST; return -1; }
    }
    int fd = __banana->open(path, f);
    if (fd < 0) {
        /* why: it is not there - or it is a folder, or every descriptor is
         * taken (EMFILE: Java then knows, instead of "no such file") */
        banana_stat_t st;
        if (__banana->stat(path, &st) != 0) errno = ENOENT;
        else errno = st.is_dir ? EISDIR : EMFILE;
        return -1;
    }
    return fd;
}

/* (socket.c: a socket's O_NONBLOCK) */
int __sock_getfl(int fd);
int __sock_setfl(int fd, int fl);
/* (ufd.c: the C library's own descriptors) */
int __ufd_is(int fd);
int __ufd_fcntl(int fd, int cmd, long arg);
int __ufd_fstat(int fd, struct stat* st);

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    if (__ufd_is(fd)) return __ufd_fcntl(fd, cmd, arg);
    if (cmd == F_GETFL) { int f = fd > 2 ? __sock_getfl(fd) : -1; return O_RDWR | (f > 0 ? f : 0); }
    if (cmd == F_SETFL) { if (fd > 2) __sock_setfl(fd, (int)arg); return 0; }
    return 0;
}

off_t lseek(int fd, off_t off, int whence) {
    if (__ufd_is(fd)) { errno = ESPIPE; return -1; }
    long r = __banana->seek(fd, off, whence);
    if (r < 0) { errno = fd <= 2 ? ESPIPE : EINVAL; return -1; }
    return r;
}

int isatty(int fd) { return fd >= 0 && fd <= 2; }

int fstat(int fd, struct stat* st) {
    if (__ufd_is(fd)) return __ufd_fstat(fd, st);
    memset(st, 0, sizeof(*st));
    st->st_blksize = 4096;
    if (fd <= 2) { st->st_mode = 0020000; return 0; }     /* a character device */
    long cur = __banana->seek(fd, 0, SEEK_CUR);
    if (cur < 0) { errno = EBADF; return -1; }
    long end = __banana->seek(fd, 0, SEEK_END);
    __banana->seek(fd, cur, SEEK_SET);
    st->st_mode = S_IFREG | 0644;
    st->st_size = end;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 1000;
    st->st_ino = (ino_t)fd;
    st->st_blocks = (end + 511) / 512;
    return 0;
}

int stat(const char* path, struct stat* st) {
    banana_stat_t b;
    if (__banana->stat(path, &b) != 0) { errno = ENOENT; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = b.is_dir ? (S_IFDIR | 0755) : (S_IFREG | 0644);
    st->st_size = b.size;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 1000;
    st->st_dev = 1;
    for (const char* c = path; *c; c++) st->st_ino = st->st_ino * 31 + (unsigned char)*c;   /* (a stable number per path) */
    st->st_blocks = (b.size + 511) / 512;
    st->st_blksize = 4096;
    return 0;
}

/* errno as on Linux, EEXIST above all: Java's Files.createDirectories takes
 * a folder that is there by it (it saw a stale ENOENT and gave up) */
int mkdir(const char* path, mode_t mode) {
    (void)mode;
    if (!path || !*path) { errno = ENOENT; return -1; }
    banana_stat_t b;
    if (__banana->stat(path, &b) == 0) { errno = EEXIST; return -1; }
    if (__banana->mkdir(path) == 0) return 0;
    /* why not: the folder it would go in */
    char parent[512];
    size_t n = strlen(path);
    if (n >= sizeof(parent)) { errno = ENAMETOOLONG; return -1; }
    memcpy(parent, path, n + 1);
    while (n > 1 && parent[n - 1] == '/') parent[--n] = 0;
    char* slash = strrchr(parent, '/');
    if (slash == parent) slash[1] = 0;
    else if (slash) *slash = 0;
    else strcpy(parent, ".");
    if (__banana->stat(parent, &b) != 0) errno = ENOENT;
    else if (!b.is_dir) errno = ENOTDIR;
    else errno = EACCES;                        /* (a read-only drive, a full one) */
    return -1;
}

/* ── aligned memory ───────────────────────────────────────────────── */
/* An alignment over 16: a bigger block, the pointer moved up into it. free
 * and realloc must give back the block itself, so the aligned pointers are
 * kept in a table (pointer -> block, size) - nothing is probed below a
 * pointer passed to free: an ordinary allocation can start at a user
 * mapping boundary, with kernel-only memory immediately before it.
 * Every free asks the table, so asking is cheap: a pointer not aligned to
 * 32 cannot be in it (no lock), the rest is one hash lookup. The lock spins
 * a little, then yields: its holder may have been preempted, and pure
 * spinning kept every core busy waiting for it (the JVM and Mesa free from
 * many threads at once - the whole system froze). No allocation, syscall
 * or copy takes place while the lock is held. */
typedef struct { void* ptr; void* raw; size_t size; } aligned_slot_t;
#define SLOT_GONE ((void*)1)                     /* (a removed entry: lookups go on past it) */

static aligned_slot_t* g_atab;
static size_t g_acap, g_aused, g_alive;          /* slots (a power of 2), taken (live + gone), live */
static volatile int g_alock;

static void aligned_lock(void) {
    int spins = 0;
    while (__sync_lock_test_and_set(&g_alock, 1)) {
        while (g_alock) {
            if (++spins > 100) { __banana->yield(); spins = 0; }   /* the holder needs the CPU */
            __asm__ volatile("pause");
        }
    }
}

static void aligned_unlock(void) { __sync_lock_release(&g_alock); }

static size_t aligned_hash(const void* p, size_t cap) {
    uintptr_t x = (uintptr_t)p >> 5;
    x ^= x >> 15;
    x *= (uintptr_t)0x9E3779B97F4A7C15ull;
    return (size_t)(x ^ (x >> 16)) & (cap - 1);
}

/* the live entries into tab (cap slots, zeroed) */
static void aligned_rehash(aligned_slot_t* tab, size_t cap) {
    for (size_t i = 0; i < g_acap; i++) {
        void* p = g_atab[i].ptr;
        if (!p || p == SLOT_GONE) continue;
        size_t k = aligned_hash(p, cap);
        while (tab[k].ptr) k = (k + 1) & (cap - 1);
        tab[k] = g_atab[i];
    }
}

static int aligned_add(void* p, void* raw, size_t size) {
    aligned_lock();
    while ((g_aused + 1) * 4 > g_acap * 3) {             /* over 3/4 full: a bigger table */
        size_t cap = g_acap, want = 64;
        while (want < (g_alive + 1) * 4) want *= 2;
        aligned_unlock();
        aligned_slot_t* tab = calloc(want, sizeof(*tab));
        if (!tab) return -1;
        aligned_lock();
        aligned_slot_t* old = NULL;
        if (g_acap == cap) {                             /* (nobody grew it meanwhile) */
            aligned_rehash(tab, want);
            old = g_atab;
            g_atab = tab;
            g_acap = want;
            g_aused = g_alive;
        } else {
            old = tab;
        }
        aligned_unlock();
        free(old);
        aligned_lock();
    }
    size_t k = aligned_hash(p, g_acap);
    while (g_atab[k].ptr && g_atab[k].ptr != SLOT_GONE) k = (k + 1) & (g_acap - 1);
    if (!g_atab[k].ptr) g_aused++;
    g_atab[k].ptr = p;
    g_atab[k].raw = raw;
    g_atab[k].size = size;
    g_alive++;
    aligned_unlock();
    return 0;
}

int __malloc_heap(void);                           /* malloc.c */
void* __malloc_aligned(size_t align, size_t n);

int posix_memalign(void** out, size_t align, size_t size) {
    if (!out || align < sizeof(void*) || (align & (align - 1))) return EINVAL;
    if (__malloc_heap()) {                              /* (the app's heap aligns blocks itself) */
        void* p = __malloc_aligned(align, size);
        if (!p) return ENOMEM;
        *out = p;
        return 0;
    }
    /* the kernel's allocator: a bigger block, and where the aligned part is noted */
    if (align <= 16) {
        void* p = malloc(size);
        if (!p) return ENOMEM;
        *out = p;
        return 0;
    }
    if (size > (size_t)-1 - align) return ENOMEM;
    void* raw = malloc(size + align - 1);
    if (!raw) return ENOMEM;
    void* p = (void*)(((uintptr_t)raw + align - 1) & ~(uintptr_t)(align - 1));
    if (aligned_add(p, raw, size)) { free(raw); return ENOMEM; }
    *out = p;
    return 0;
}
void* aligned_alloc(size_t align, size_t size) { void* p; return posix_memalign(&p, align, size) ? NULL : p; }
void* memalign(size_t align, size_t size) { return aligned_alloc(align, size); }

/* (libc.c: free and realloc know these blocks) */
int __aligned_block(void* p, void** raw, size_t* size, int remove) {
    /* (only pointers aligned to 32 or more are ever added; g_alive is read
     * unlocked: p's own entry, if any, was added before p was handed out) */
    if (((uintptr_t)p & 31) || !g_alive) return 0;
    int found = 0;
    aligned_lock();
    if (g_acap) {
        for (size_t k = aligned_hash(p, g_acap); g_atab[k].ptr; k = (k + 1) & (g_acap - 1)) {
            if (g_atab[k].ptr != p) continue;
            if (raw) *raw = g_atab[k].raw;
            if (size) *size = g_atab[k].size;
            if (remove) { g_atab[k].ptr = SLOT_GONE; g_alive--; }
            found = 1;
            break;
        }
    }
    aligned_unlock();
    return found;
}

intmax_t strtoimax(const char* s, char** end, int base) { return strtoll(s, end, base); }
uintmax_t strtoumax(const char* s, char** end, int base) { return strtoull(s, end, base); }

/* interval timers (SIGALRM and the like): none here - getitimer says none
 * is armed, setitimer only disarms (OpenJDK's awt asks, for a debug option) */
int getitimer(int which, struct itimerval* cur) {
    (void)which;
    if (cur) memset(cur, 0, sizeof(*cur));
    return 0;
}

int setitimer(int which, const struct itimerval* val, struct itimerval* old) {
    (void)which;
    if (old) memset(old, 0, sizeof(*old));
    if (val && (val->it_value.tv_sec || val->it_value.tv_usec)) { errno = ENOSYS; return -1; }
    return 0;
}

/* the wall clock (libc.c: __realtime_ms - it never runs backwards) */
long long __realtime_ms(void);                  /* (libc.c: never runs backwards) */
int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (tv) { long long now = __realtime_ms(); tv->tv_sec = (long)(now / 1000); tv->tv_usec = (long)(now % 1000) * 1000; }
    return 0;
}

char* realpath(const char* path, char* resolved) {
    char buf[PATH_MAX];                         /* (what a caller's buffer holds) */
    if (!path || !*path) { errno = ENOENT; return NULL; }
    if (path[0] == '/') buf[0] = 0;
    else if (!getcwd(buf, sizeof(buf))) return NULL;
    size_t n = strlen(buf);
    /* each part: "." stays, ".." goes up one */
    const char* p = path;
    while (*p) {
        while (*p == '/') p++;
        const char* e = p;
        while (*e && *e != '/') e++;
        size_t l = (size_t)(e - p);
        if (l == 0) break;
        if (l == 1 && p[0] == '.') { p = e; continue; }
        if (l == 2 && p[0] == '.' && p[1] == '.') {
            while (n > 0 && buf[n - 1] != '/') n--;
            if (n > 0) n--;
            buf[n] = 0;
            p = e;
            continue;
        }
        if (n + 1 + l + 1 > sizeof(buf)) { errno = ENAMETOOLONG; return NULL; }
        buf[n++] = '/';
        memcpy(buf + n, p, l);
        n += l;
        buf[n] = 0;
        p = e;
    }
    if (!n) { buf[0] = '/'; buf[1] = 0; }
    if (!resolved) return strdup(buf);
    strcpy(resolved, buf);
    return resolved;
}
