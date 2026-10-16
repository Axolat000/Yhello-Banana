/* Banana OS SDK: POSIX bits over the system calls - file descriptors
 * (open, lseek, fstat, stat), aligned memory, fdopen - for programs
 * ported from elsewhere. */
#include <banana.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern const banana_api_t* __banana;

int open(const char* path, int flags, ...) {
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
    if (fd < 0) { errno = ENOENT; return -1; }
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

int mkdir(const char* path, mode_t mode) { (void)mode; return __banana->mkdir(path) == 0 ? 0 : -1; }

/* ── aligned memory ───────────────────────────────────────────────── */
/* Keep a list of aligned allocations instead of probing below every pointer
 * passed to free/realloc: an ordinary allocation can start at a user mapping
 * boundary, with kernel-only memory immediately before it. Headers live at
 * the start of their own raw allocation and are read only through this list.
 * No allocation, syscall or copy takes place while the list lock is held. */
typedef struct aligned_block {
    struct aligned_block* next;
    void* ptr;
    size_t size;
} aligned_block_t;

static aligned_block_t* g_aligned;
static volatile int g_aligned_lock;

static void aligned_lock(void) {
    while (__sync_lock_test_and_set(&g_aligned_lock, 1))
        while (g_aligned_lock) __asm__ volatile("pause");
}

static void aligned_unlock(void) { __sync_lock_release(&g_aligned_lock); }

int posix_memalign(void** out, size_t align, size_t size) {
    if (!out || align < sizeof(void*) || (align & (align - 1))) return EINVAL;
    if (align <= 16) {
        void* p = malloc(size);
        if (!p) return ENOMEM;
        *out = p;
        return 0;
    }
    size_t extra = sizeof(aligned_block_t) + align - 1;
    if (extra < align || size > (size_t)-1 - extra) return ENOMEM;
    aligned_block_t* h = malloc(size + extra);
    if (!h) return ENOMEM;
    uintptr_t p = ((uintptr_t)h + sizeof(*h) + align - 1) & ~(uintptr_t)(align - 1);
    h->ptr = (void*)p;
    h->size = size;
    aligned_lock();
    h->next = g_aligned;
    g_aligned = h;
    aligned_unlock();
    *out = (void*)p;
    return 0;
}
void* aligned_alloc(size_t align, size_t size) { void* p; return posix_memalign(&p, align, size) ? NULL : p; }
void* memalign(size_t align, size_t size) { return aligned_alloc(align, size); }

/* (libc.c: free and realloc know these blocks) */
int __aligned_block(void* p, void** raw, size_t* size, int remove) {
    int found = 0;
    aligned_lock();
    for (aligned_block_t** pp = &g_aligned; *pp; pp = &(*pp)->next) {
        aligned_block_t* h = *pp;
        if (h->ptr != p) continue;
        if (raw) *raw = h;
        if (size) *size = h->size;
        if (remove) *pp = h->next;
        found = 1;
        break;
    }
    aligned_unlock();
    return found;
}

intmax_t strtoimax(const char* s, char** end, int base) { return strtoll(s, end, base); }
uintmax_t strtoumax(const char* s, char** end, int base) { return strtoull(s, end, base); }

/* seconds from the clock chip, the sub-second part from the uptime counter */
int gettimeofday(struct timeval* tv, void* tz) {
    (void)tz;
    if (tv) { tv->tv_sec = (long)time(0); tv->tv_usec = (long)(__banana->ticks_ms() % 1000u) * 1000; }
    return 0;
}

char* realpath(const char* path, char* resolved) {
    char buf[1024];
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
