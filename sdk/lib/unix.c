/* Banana OS SDK: the rest of the Unix and Linux C library that ported
 * programs (OpenJDK...) expect - the environment, folders, the user,
 * uname, limits, locale, wide characters, file odds and ends. Where Banana
 * OS has no such thing (other processes, shared memory) the call
 * fails the way Linux would with the feature missing. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fenv.h>
#include <grp.h>
#include <ifaddrs.h>
#include <langinfo.h>
#include <libgen.h>
#include <locale.h>
#include <malloc.h>
#include <pwd.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <wchar.h>
#include <time.h>
#include <utmpx.h>
#include <netinet/in.h>
#include <linux/futex.h>
#include <mntent.h>
#include <net/if.h>
#include <spawn.h>
#include <termios.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/statvfs.h>
#include <sys/xattr.h>
#include <sys/random.h>
#include <syslog.h>
#include <sys/file.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

/* ── the environment ──────────────────────────────────────────────── */

static char* g_env_init[] = {
    "HOME=/home/banana", "USER=banana", "LOGNAME=banana", "PATH=/bin:/usr/bin",
    "LANG=C.UTF-8", "SHELL=/bin/sh", "TMPDIR=/tmp", "TERM=banana", NULL,
};
char** environ = g_env_init;
static int g_env_owned;                       /* environ is ours to grow (malloc'd) */

static int env_find(const char* name, size_t n) {
    for (int i = 0; environ[i]; i++)
        if (!strncmp(environ[i], name, n) && environ[i][n] == '=') return i;
    return -1;
}

char* getenv(const char* name) {
    int i = env_find(name, strlen(name));
    return i < 0 ? NULL : environ[i] + strlen(name) + 1;
}

static int env_own(void) {
    if (g_env_owned) return 0;
    int n = 0;
    while (environ[n]) n++;
    char** e = malloc((size_t)(n + 1) * sizeof(char*));
    if (!e) return -1;
    for (int i = 0; i < n; i++) e[i] = strdup(environ[i]);
    e[n] = NULL;
    environ = e;
    g_env_owned = 1;
    return 0;
}

static int env_put(char* entry, size_t namelen) {
    if (env_own()) { errno = ENOMEM; return -1; }
    int i = env_find(entry, namelen);
    if (i >= 0) { environ[i] = entry; return 0; }
    int n = 0;
    while (environ[n]) n++;
    char** e = realloc(environ, (size_t)(n + 2) * sizeof(char*));
    if (!e) { errno = ENOMEM; return -1; }
    e[n] = entry;
    e[n + 1] = NULL;
    environ = e;
    return 0;
}

int setenv(const char* name, const char* value, int overwrite) {
    size_t n = name ? strlen(name) : 0;
    if (!n || strchr(name, '=')) { errno = EINVAL; return -1; }
    if (!overwrite && getenv(name)) return 0;
    char* e = malloc(n + strlen(value) + 2);
    if (!e) { errno = ENOMEM; return -1; }
    sprintf(e, "%s=%s", name, value);
    return env_put(e, n);
}

int putenv(char* s) {
    char* eq = strchr(s, '=');
    if (!eq) { errno = EINVAL; return -1; }
    return env_put(s, (size_t)(eq - s));
}

int unsetenv(const char* name) {
    if (env_own()) return -1;
    size_t n = strlen(name);
    int i;
    while ((i = env_find(name, n)) >= 0)
        for (; environ[i]; i++) environ[i] = environ[i + 1];
    return 0;
}

int clearenv(void) {
    static char* empty[1];
    environ = empty;
    g_env_owned = 0;
    return 0;
}

/* ── folders ──────────────────────────────────────────────────────── */

struct __dir {
    char          path[512];
    int           index;
    struct dirent ent;
};

DIR* opendir(const char* path) {
    banana_stat_t st;
    if (!path || __banana->stat(path, &st) != 0) { errno = ENOENT; return NULL; }
    if (!st.is_dir) { errno = ENOTDIR; return NULL; }
    DIR* d = calloc(1, sizeof(DIR));
    if (!d) { errno = ENOMEM; return NULL; }
    snprintf(d->path, sizeof(d->path), "%s", path);
    return d;
}

DIR* fdopendir(int fd) { (void)fd; errno = ENOTSUP; return NULL; }

/* "." and ".." first, as everywhere, then what the folder holds */
struct dirent* readdir(DIR* d) {
    memset(&d->ent, 0, sizeof(d->ent));
    if (d->index < 2) {
        strcpy(d->ent.d_name, d->index ? ".." : ".");
        d->ent.d_type = DT_DIR;
    } else {
        banana_dirent_t e;
        if (__banana->readdir(d->path, d->index - 2, &e) != 0) return NULL;
        snprintf(d->ent.d_name, sizeof(d->ent.d_name), "%s", e.name);
        d->ent.d_type = e.is_dir ? DT_DIR : DT_REG;
    }
    d->index++;
    d->ent.d_ino = (ino_t)d->index;
    d->ent.d_off = d->index;
    d->ent.d_reclen = sizeof(struct dirent);
    return &d->ent;
}

int readdir_r(DIR* d, struct dirent* e, struct dirent** out) {
    struct dirent* r = readdir(d);
    if (r) { *e = *r; *out = e; } else *out = NULL;
    return 0;
}

int closedir(DIR* d) { free(d); return 0; }
void rewinddir(DIR* d) { d->index = 0; }
int dirfd(DIR* d) { (void)d; errno = ENOTSUP; return -1; }

/* ── files ────────────────────────────────────────────────────────── */

int lstat(const char* path, struct stat* st) { return stat(path, st); }      /* (no links here) */

int fstatat(int dirfd, const char* path, struct stat* st, int flags) {
    (void)dirfd; (void)flags;
    return stat(path, st);
}

int access(const char* path, int mode) {
    (void)mode;
    banana_stat_t st;
    if (!path || __banana->stat(path, &st) != 0) { errno = ENOENT; return -1; }
    return 0;
}

int chmod(const char* path, mode_t mode) { (void)mode; return access(path, 0); }
int fchmod(int fd, mode_t mode) { (void)fd; (void)mode; return 0; }
int chown(const char* path, uid_t u, gid_t g) { (void)u; (void)g; return access(path, 0); }
int fchown(int fd, uid_t u, gid_t g) { (void)fd; (void)u; (void)g; return 0; }
int lchown(const char* path, uid_t u, gid_t g) { return chown(path, u, g); }
mode_t umask(mode_t m) { static mode_t cur = 022; mode_t o = cur; cur = m & 0777; return o; }
int mkfifo(const char* path, mode_t mode) { (void)path; (void)mode; errno = ENOTSUP; return -1; }
ssize_t readlink(const char* path, char* buf, size_t len) { (void)buf; (void)len; errno = access(path, 0) ? ENOENT : EINVAL; return -1; }
int symlink(const char* a, const char* b) { (void)a; (void)b; errno = EPERM; return -1; }
int link(const char* a, const char* b) { (void)a; (void)b; errno = EPERM; return -1; }
int fsync(int fd) {
    if (!__banana || !BANANA_HAS(__banana, fd_sync) || !__banana->fd_sync) {
        errno = ENOSYS;
        return -1;
    }
    int rc = __banana->fd_sync(fd);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

int fdatasync(int fd) {
    return fsync(fd);
}

void sync(void) {
    /* POSIX sync() is fire-and-forget advisory flush across filesystems */
}
int flock(int fd, int op) { (void)fd; (void)op; return 0; }

int ftruncate(int fd, off_t len) {
    off_t cur = lseek(fd, 0, SEEK_CUR), end = lseek(fd, 0, SEEK_END);
    if (cur < 0 || end < 0) { errno = EBADF; return -1; }
    int rc = 0;
    if (len > end) {                                  /* longer: zeros at the end */
        static const char zero[512];
        for (off_t n = len - end; n > 0 && rc == 0; n -= (off_t)sizeof(zero))
            rc = write(fd, zero, n > (off_t)sizeof(zero) ? sizeof(zero) : (size_t)n) < 0 ? -1 : 0;
    } else if (len < end) {
        errno = ENOTSUP;                              /* (files do not shrink through a descriptor) */
        rc = -1;
    }
    lseek(fd, cur, SEEK_SET);
    return rc;
}

ssize_t pread(int fd, void* buf, size_t n, off_t off) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = read(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

ssize_t pwrite(int fd, const void* buf, size_t n, off_t off) {
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur < 0 || lseek(fd, off, SEEK_SET) < 0) return -1;
    ssize_t r = write(fd, buf, n);
    lseek(fd, cur, SEEK_SET);
    return r;
}

/* (dup, dup2, pipe: ufd.c) */

int mkstemp(char* tmpl) {
    size_t n = strlen(tmpl);
    if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX")) { errno = EINVAL; return -1; }
    for (int tries = 0; tries < 100; tries++) {
        unsigned r = __banana->random();
        for (int i = 0; i < 6; i++) tmpl[n - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[(r >> (i * 5)) % 36];
        if (access(tmpl, 0) == 0) continue;
        int fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
    }
    errno = EEXIST;
    return -1;
}

ssize_t getdelim(char** line, size_t* cap, int delim, FILE* f) {
    if (!line || !cap || !f) { errno = EINVAL; return -1; }
    size_t n = 0;
    int c;
    while ((c = fgetc(f)) != EOF) {
        if (n + 2 > *cap) {
            size_t nc = *cap ? *cap * 2 : 128;
            char* b = realloc(*line, nc);
            if (!b) { errno = ENOMEM; return -1; }
            *line = b;
            *cap = nc;
        }
        (*line)[n++] = (char)c;
        if (c == delim) break;
    }
    if (!n) return -1;
    (*line)[n] = 0;
    return (ssize_t)n;
}
ssize_t getline(char** line, size_t* cap, FILE* f) { return getdelim(line, cap, '\n', f); }

/* ── processes (there are only apps: no fork, exec or children) ───── */

pid_t fork(void) { errno = ENOSYS; return -1; }
pid_t vfork(void) { errno = ENOSYS; return -1; }
int execve(const char* p, char* const a[], char* const e[]) { (void)p; (void)a; (void)e; errno = ENOSYS; return -1; }
int execv(const char* p, char* const a[]) { (void)p; (void)a; errno = ENOSYS; return -1; }
int execvp(const char* p, char* const a[]) { (void)p; (void)a; errno = ENOSYS; return -1; }
pid_t wait(int* s) { (void)s; errno = ECHILD; return -1; }
pid_t waitpid(pid_t pid, int* s, int o) { (void)pid; (void)s; (void)o; errno = ECHILD; return -1; }
pid_t setsid(void) { return getpid(); }
pid_t getpgrp(void) { return getpid(); }
int setpgid(pid_t a, pid_t b) { (void)a; (void)b; return 0; }
unsigned alarm(unsigned s) { (void)s; return 0; }
char* getlogin(void) { return "banana"; }
char* ttyname(int fd) { return fd <= 2 ? "/dev/tty" : NULL; }
long pathconf(const char* p, int n) { (void)p; (void)n; return 255; }
long fpathconf(int fd, int n) { (void)fd; (void)n; return 255; }
int getdtablesize(void) { return 64 + 3; }

/* ── the user ─────────────────────────────────────────────────────── */

static struct passwd g_pw = { "banana", "x", 1000, 1000, "Banana OS", "/home/banana", "/bin/sh" };
static char* g_members[] = { "banana", NULL };
static struct group g_gr = { "banana", "x", 1000, g_members };

struct passwd* getpwuid(uid_t uid) { return uid == 1000 || uid == 0 ? &g_pw : NULL; }
struct passwd* getpwnam(const char* name) { return name && !strcmp(name, "banana") ? &g_pw : NULL; }
int getpwuid_r(uid_t uid, struct passwd* pw, char* buf, size_t len, struct passwd** out) {
    (void)buf; (void)len;
    struct passwd* p = getpwuid(uid);
    if (p) *pw = *p;
    *out = p ? pw : NULL;
    return 0;
}
int getpwnam_r(const char* name, struct passwd* pw, char* buf, size_t len, struct passwd** out) {
    (void)buf; (void)len;
    struct passwd* p = getpwnam(name);
    if (p) *pw = *p;
    *out = p ? pw : NULL;
    return 0;
}
struct group* getgrgid(gid_t gid) { return gid == 1000 ? &g_gr : NULL; }
struct group* getgrnam(const char* name) { return name && !strcmp(name, "banana") ? &g_gr : NULL; }
int getgrgid_r(gid_t gid, struct group* g, char* buf, size_t len, struct group** out) {
    (void)buf; (void)len;
    struct group* r = getgrgid(gid);
    if (r) *g = *r;
    *out = r ? g : NULL;
    return 0;
}
int getgrnam_r(const char* name, struct group* g, char* buf, size_t len, struct group** out) {
    (void)buf; (void)len;
    struct group* r = getgrnam(name);
    if (r) *g = *r;
    *out = r ? g : NULL;
    return 0;
}

/* ── the system ───────────────────────────────────────────────────── */

int uname(struct utsname* u) {
    memset(u, 0, sizeof(*u));
    strcpy(u->sysname, "Banana OS");
    strcpy(u->nodename, "banana-os");
    snprintf(u->release, sizeof(u->release), "%s", __banana->os_version ? __banana->os_version : "0.6");
    strcpy(u->version, "Banana OS");
    snprintf(u->machine, sizeof(u->machine), "%s", __banana->arch);
    return 0;
}

int getrlimit(int res, struct rlimit* r) {
    r->rlim_cur = r->rlim_max = RLIM_INFINITY;
    if (res == RLIMIT_NOFILE) r->rlim_cur = r->rlim_max = 256;   /* (kernel/app.c's APP_FD_MAX + 3) */
    if (res == RLIMIT_STACK) r->rlim_cur = r->rlim_max = 1ul << 20;    /* (the main thread's) */
    if (res == RLIMIT_CORE) r->rlim_cur = r->rlim_max = 0;
    return 0;
}
int setrlimit(int res, const struct rlimit* r) { (void)res; (void)r; return 0; }

int getrusage(int who, struct rusage* r) {
    (void)who;
    memset(r, 0, sizeof(*r));
    struct timespec t;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t);
    r->ru_utime.tv_sec = t.tv_sec;
    r->ru_utime.tv_usec = t.tv_nsec / 1000;
    return 0;
}
int getpriority(int which, id_t who) { (void)which; (void)who; errno = 0; return 0; }
int setpriority(int which, id_t who, int prio) { (void)which; (void)who; (void)prio; return 0; }

clock_t times(struct tms* t) {
    clock_t now = (clock_t)__banana->ticks_ms();
    if (t) { t->tms_utime = clock(); t->tms_stime = t->tms_cutime = t->tms_cstime = 0; }
    return now;
}

int get_nprocs(void) { return (int)sysconf(_SC_NPROCESSORS_ONLN); }
int get_nprocs_conf(void) { return get_nprocs(); }

int sysinfo(struct sysinfo* i) {
    memset(i, 0, sizeof(*i));
    i->uptime = (long)(__banana->ticks_ms() / 1000u);
    i->totalram = (unsigned long)sysconf(_SC_PHYS_PAGES);
    i->freeram = (unsigned long)sysconf(_SC_AVPHYS_PAGES);
    i->mem_unit = 4096;
    i->procs = 1;
    return 0;
}

int statfs(const char* path, struct statfs* s) {
    if (access(path, 0)) return -1;
    memset(s, 0, sizeof(*s));
    s->f_type = 0xBA7A;
    s->f_bsize = s->f_frsize = 4096;
    s->f_blocks = s->f_bfree = s->f_bavail = 1ul << 20;
    s->f_files = s->f_ffree = 1ul << 20;
    s->f_namelen = 255;
    return 0;
}
int fstatfs(int fd, struct statfs* s) { (void)fd; return statfs("/", s); }

int prctl(int option, ...) { (void)option; return 0; }

/* the Linux system calls ported code makes itself */
long syscall(long nr, ...) {
    va_list ap;
    va_start(ap, nr);
    long a1 = va_arg(ap, long), a2 = va_arg(ap, long), a3 = va_arg(ap, long), a4 = va_arg(ap, long);
    va_end(ap);
    switch (nr) {
    case SYS_gettid: return gettid();
    case SYS_getpid: return getpid();
    case SYS_futex: {
        volatile int* addr = (volatile int*)a1;
        int op = (int)a2 & ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME);
        const struct timespec* ts = (const struct timespec*)a4;
        if (op == FUTEX_WAIT || op == FUTEX_WAIT_BITSET) {
            if (*addr != (int)a3) { errno = EAGAIN; return -1; }
            int ms = -1;
            if (ts) {                                   /* WAIT: a time span; WAIT_BITSET: a deadline */
                long long ns = (long long)ts->tv_sec * 1000000000LL + ts->tv_nsec;
                if (op == FUTEX_WAIT_BITSET) {
                    struct timespec now;
                    clock_gettime((int)a2 & FUTEX_CLOCK_REALTIME ? CLOCK_REALTIME : CLOCK_MONOTONIC, &now);
                    ns -= (long long)now.tv_sec * 1000000000LL + now.tv_nsec;
                }
                if (ns <= 0) { errno = ETIMEDOUT; return -1; }
                ms = ns / 1000000 > 0x7fffffff ? 0x7fffffff : (int)((ns + 999999) / 1000000);
            }
            if (__banana->wait(addr, (int)a3, ms) == 1) { errno = ETIMEDOUT; return -1; }
            return 0;
        }
        if (op == FUTEX_WAKE || op == FUTEX_WAKE_BITSET) return __banana->wake(addr, (int)a3);
        break;
    }
    case SYS_getrandom: return getrandom((void*)a1, (size_t)a2, (unsigned)a3);
    case SYS_membarrier: return 0;
    }
    errno = ENOSYS;
    return -1;
}

/* System V shared memory: not here */
int shmget(key_t k, size_t s, int f) { (void)k; (void)s; (void)f; errno = ENOSYS; return -1; }
void* shmat(int id, const void* a, int f) { (void)id; (void)a; (void)f; errno = ENOSYS; return (void*)-1; }
int shmdt(const void* a) { (void)a; errno = EINVAL; return -1; }
int shmctl(int id, int c, struct shmid_ds* b) { (void)id; (void)c; (void)b; errno = EINVAL; return -1; }

/* the network interfaces: the loopback (the card's address: getaddrinfo) */
int getifaddrs(struct ifaddrs** out) {
    struct { struct ifaddrs i; struct sockaddr_in a, m; char name[4]; }* lo = calloc(1, sizeof(*lo));
    if (!lo) { errno = ENOMEM; return -1; }
    strcpy(lo->name, "lo");
    lo->a.sin_family = lo->m.sin_family = AF_INET;
    lo->a.sin_addr.s_addr = 0x0100007f;               /* 127.0.0.1, network order */
    lo->m.sin_addr.s_addr = 0x000000ff;               /* 255.0.0.0 */
    lo->i.ifa_name = lo->name;
    lo->i.ifa_flags = 0x1 | 0x8 | 0x40;               /* IFF_UP | IFF_LOOPBACK | IFF_RUNNING */
    lo->i.ifa_addr = (struct sockaddr*)&lo->a;
    lo->i.ifa_netmask = (struct sockaddr*)&lo->m;
    *out = &lo->i;
    return 0;
}
void freeifaddrs(struct ifaddrs* i) { free(i); }

/* ── memory ───────────────────────────────────────────────────────── */

/* (malloc_usable_size: malloc.c) */
struct mallinfo mallinfo(void) { struct mallinfo m; memset(&m, 0, sizeof(m)); return m; }
int malloc_trim(size_t pad) { (void)pad; return 0; }

/* ── locale: "C", text in UTF-8 ───────────────────────────────────── */

char* setlocale(int cat, const char* name) {
    (void)cat;
    if (!name || !*name || !strcmp(name, "C") || !strcmp(name, "POSIX") || strstr(name, "UTF-8") || strstr(name, "utf8"))
        return "C.UTF-8";
    return NULL;
}

struct lconv* localeconv(void) {
    static struct lconv l = { ".", "", "", "", "", "", "", "", "", "-", 127, 127, 127, 127, 127, 127, 127, 127 };
    return &l;
}

char* nl_langinfo(nl_item item) { return item == CODESET ? "UTF-8" : ""; }

/* ── floating point environment (x87 control word and SSE MXCSR) ─── */

int fegetround(void) {
    unsigned int m;
    __asm__ volatile("stmxcsr %0" : "=m"(m));
    return (int)((m >> 3) & 0xc00);
}

int fesetround(int mode) {
    if (mode & ~0xc00) return -1;
    unsigned short cw;
    unsigned int m;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw = (unsigned short)((cw & ~0xc00) | mode);
    __asm__ volatile("fldcw %0" :: "m"(cw));
    __asm__ volatile("stmxcsr %0" : "=m"(m));
    m = (m & ~0x6000u) | ((unsigned)mode << 3);
    __asm__ volatile("ldmxcsr %0" :: "m"(m));
    return 0;
}

int fegetenv(fenv_t* e) {
    __asm__ volatile("fnstcw %0" : "=m"(e->cw));
    __asm__ volatile("fnstsw %0" : "=m"(e->sw));
    __asm__ volatile("stmxcsr %0" : "=m"(e->mxcsr));
    return 0;
}

int fesetenv(const fenv_t* e) {
    __asm__ volatile("fldcw %0" :: "m"(e->cw));
    __asm__ volatile("ldmxcsr %0" :: "m"(e->mxcsr));
    return 0;
}

int feclearexcept(int e) {
    unsigned int m;
    __asm__ volatile("fnclex");
    __asm__ volatile("stmxcsr %0" : "=m"(m));
    m &= ~(unsigned)(e & FE_ALL_EXCEPT);
    __asm__ volatile("ldmxcsr %0" :: "m"(m));
    return 0;
}

int fetestexcept(int e) {
    unsigned short sw;
    unsigned int m;
    __asm__ volatile("fnstsw %0" : "=m"(sw));
    __asm__ volatile("stmxcsr %0" : "=m"(m));
    return (int)((sw | m) & (unsigned)e & FE_ALL_EXCEPT);
}

/* ── paths ────────────────────────────────────────────────────────── */

char* basename(char* p) {
    static char dot[] = ".";
    if (!p || !*p) return dot;
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = 0;
    char* s = strrchr(p, '/');
    return s && s[1] ? s + 1 : p;
}

char* dirname(char* p) {
    static char dot[] = ".", root[] = "/";
    if (!p || !*p) return dot;
    size_t n = strlen(p);
    while (n > 1 && p[n - 1] == '/') p[--n] = 0;
    char* s = strrchr(p, '/');
    if (!s) return dot;
    if (s == p) return root;
    *s = 0;
    return p;
}

/* ── wide characters (UTF-32 wchar_t, UTF-8 multibyte) ────────────── */

size_t wcslen(const wchar_t* s) { size_t n = 0; while (s[n]) n++; return n; }
wchar_t* wcscpy(wchar_t* d, const wchar_t* s) { wchar_t* r = d; while ((*d++ = *s++)) ; return r; }
wchar_t* wcsncpy(wchar_t* d, const wchar_t* s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
int wcscmp(const wchar_t* a, const wchar_t* b) { while (*a && *a == *b) { a++; b++; } return *a < *b ? -1 : *a > *b; }
int wcsncmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (; n; n--, a++, b++) { if (*a != *b) return *a < *b ? -1 : 1; if (!*a) return 0; }
    return 0;
}
wchar_t* wcschr(const wchar_t* s, wchar_t c) { for (;; s++) { if (*s == c) return (wchar_t*)s; if (!*s) return NULL; } }
wchar_t* wcsrchr(const wchar_t* s, wchar_t c) { const wchar_t* r = NULL; for (;; s++) { if (*s == c) r = s; if (!*s) return (wchar_t*)r; } }
wchar_t* wcscat(wchar_t* d, const wchar_t* s) { wcscpy(d + wcslen(d), s); return d; }
wchar_t* wmemcpy(wchar_t* d, const wchar_t* s, size_t n) { memcpy(d, s, n * sizeof(wchar_t)); return d; }
wchar_t* wmemset(wchar_t* d, wchar_t c, size_t n) { for (size_t i = 0; i < n; i++) d[i] = c; return d; }

int mbtowc(wchar_t* pwc, const char* s, size_t n) { size_t r = mbrtowc(pwc, s, n, NULL); return r > 4 ? -1 : (int)r; }
int wctomb(char* s, wchar_t wc) { return s ? (int)wcrtomb(s, wc, NULL) : 0; }
wint_t btowc(int c) { return c >= 0 && c < 0x80 ? (wint_t)c : WEOF; }
int wctob(wint_t c) { return c < 0x80 ? (int)c : EOF; }

size_t mbstowcs(wchar_t* d, const char* s, size_t n) {
    mbstate_t state = {0};
    size_t i = 0;
    while (!d || i < n) {
        wchar_t w;
        size_t r = mbrtowc(&w, s, 4, &state);
        if (r == (size_t)-1 || r == (size_t)-2) return (size_t)-1;
        if (d) d[i] = w;
        if (!r) break;
        s += r;
        i++;
    }
    return i;
}

size_t wcstombs(char* d, const wchar_t* s, size_t n) {
    size_t out = 0;
    char b[4];
    for (; *s; s++) {
        size_t r = wcrtomb(b, *s, NULL);
        if (r == (size_t)-1) return r;
        if (d) { if (out + r > n) break; memcpy(d + out, b, r); }
        out += r;
    }
    if (d && out < n) d[out] = 0;
    return out;
}

/* ── strings ──────────────────────────────────────────────────────── */

int strerror_r(int e, char* buf, size_t len) {
    snprintf(buf, len, "%s", strerror(e));
    return 0;
}
int __xpg_strerror_r(int e, char* buf, size_t len) { return strerror_r(e, buf, len); }

char* strsignal(int sig) {
    static char b[32];
    static const char* const names[] = { "", "Hangup", "Interrupt", "Quit", "Illegal instruction", "Trace/breakpoint trap",
        "Aborted", "Bus error", "Floating point exception", "Killed", "User defined signal 1", "Segmentation fault",
        "User defined signal 2", "Broken pipe", "Alarm clock", "Terminated" };
    if (sig > 0 && sig < 16) return (char*)names[sig];
    snprintf(b, sizeof(b), "Signal %d", sig);
    return b;
}

char* strsep(char** s, const char* delim) {
    char* start = *s;
    if (!start) return NULL;
    char* e = start + strcspn(start, delim);
    if (*e) { *e = 0; *s = e + 1; } else *s = NULL;
    return start;
}

size_t confstr(int name, char* buf, size_t len) {
    const char* v = name == _CS_GNU_LIBC_VERSION ? "Banana OS SDK libc" :
                    name == _CS_GNU_LIBPTHREAD_VERSION ? "Banana OS SDK pthreads" :
                    name == _CS_PATH ? "/bin:/usr/bin" : NULL;
    if (!v) { errno = EINVAL; return 0; }
    if (buf && len) snprintf(buf, len, "%s", v);
    return strlen(v) + 1;
}

int fchdir(int fd) { (void)fd; errno = ENOTSUP; return -1; }

/* ── time odds and ends ───────────────────────────────────────────── */

long timezone;
int daylight;
static char g_utc[] = "UTC", g_tzn[2][12];
char* tzname[2] = { g_utc, g_utc };
time_t __tm_to_epoch(const struct tm* tm);
long   __utc_offset(time_t utc);

/* the zone of Settings > Date & time: timezone (seconds WEST of UTC, as
 * POSIX has it), daylight (1: it has a summer time), tzname (winter, summer) */
void tzset(void) {
    time_t now = time(NULL);
    struct tm y;
    gmtime_r(&now, &y);
    struct tm jan = { 0, 0, 12, 1, 0, y.tm_year, 0, 0, 0, 0, 0 }, jul = jan;
    jul.tm_mon = 6;
    long a = __utc_offset(__tm_to_epoch(&jan)), b = __utc_offset(__tm_to_epoch(&jul));
    long w = a < b ? a : b, s = a < b ? b : a;
    timezone = -w;
    daylight = s != w;
    for (int k = 0; k < 2; k++) {
        long o = k ? s : w, n = o < 0 ? -o : o;
        if (!o) { tzname[k] = g_utc; continue; }
        snprintf(g_tzn[k], sizeof(g_tzn[k]), "UTC%c%02ld:%02ld", o < 0 ? '-' : '+', n / 3600, n / 60 % 60);
        tzname[k] = g_tzn[k];
    }
}

char* asctime_r(const struct tm* tm, char* buf) {
    static const char wd[] = "SunMonTueWedThuFriSat", mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    snprintf(buf, 26, "%.3s %.3s%3d %02d:%02d:%02d %d\n", wd + 3 * (tm->tm_wday % 7), mon + 3 * (tm->tm_mon % 12),
             tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, 1900 + tm->tm_year);
    return buf;
}
char* asctime(const struct tm* tm) { static char b[32]; return asctime_r(tm, b); }
char* ctime_r(const time_t* t, char* buf) { struct tm tm; return asctime_r(localtime_r(t, &tm), buf); }
char* ctime(const time_t* t) { static char b[32]; return ctime_r(t, b); }
double difftime(time_t a, time_t b) { return (double)(a - b); }
time_t timegm(struct tm* tm) { return __tm_to_epoch(tm); }

size_t wcsftime(wchar_t* s, size_t max, const wchar_t* fmt, const struct tm* tm) {
    char f[256], out[512];
    size_t i = 0;
    for (; fmt[i] && i < sizeof(f) - 1; i++) f[i] = fmt[i] < 0x80 ? (char)fmt[i] : '?';
    f[i] = 0;
    size_t n = strftime(out, sizeof(out), f, tm);
    if (n >= max) return 0;
    for (i = 0; i <= n; i++) s[i] = (unsigned char)out[i];
    return n;
}

/* who is logged in: nobody listed */
void setutxent(void) {}
void endutxent(void) {}
struct utmpx* getutxent(void) { return NULL; }

int mincore(void* addr, size_t len, unsigned char* vec) {
    (void)addr;
    memset(vec, 1, (len + 4095) / 4096);
    return 0;
}

int posix_fallocate(int fd, off_t off, off_t len) {
    off_t end = lseek(fd, 0, SEEK_END);
    if (end < 0) return EBADF;
    return off + len > end && ftruncate(fd, off + len) ? errno : 0;
}
int posix_fadvise(int fd, off_t off, off_t len, int advice) { (void)fd; (void)off; (void)len; (void)advice; return 0; }

/* (stdio streams are not shared between threads here) */
void flockfile(FILE* f) { (void)f; }
void funlockfile(FILE* f) { (void)f; }
int ftrylockfile(FILE* f) { (void)f; return 0; }

int getloadavg(double loads[], int n) {
    for (int i = 0; i < n && i < 3; i++) loads[i] = 0;
    return n < 3 ? n : 3;
}

/* ── more files ───────────────────────────────────────────────────── */

int statvfs(const char* path, struct statvfs* out) {
    struct statfs s;
    if (statfs(path, &s)) return -1;
    memset(out, 0, sizeof(*out));
    out->f_bsize = (unsigned long)s.f_bsize;
    out->f_frsize = (unsigned long)s.f_frsize;
    out->f_blocks = (fsblkcnt_t)s.f_blocks;
    out->f_bfree = (fsblkcnt_t)s.f_bfree;
    out->f_bavail = (fsblkcnt_t)s.f_bavail;
    out->f_files = out->f_ffree = out->f_favail = (fsfilcnt_t)s.f_files;
    out->f_namemax = 255;
    return 0;
}
int fstatvfs(int fd, struct statvfs* out) { (void)fd; return statvfs("/", out); }

/* file times: Banana OS keeps none an app can set (accepted, unchanged) */
int utimes(const char* path, const struct timeval t[2]) { (void)t; return access(path, 0); }
int lutimes(const char* path, const struct timeval t[2]) { return utimes(path, t); }
int futimes(int fd, const struct timeval t[2]) { (void)fd; (void)t; return 0; }
int mknod(const char* path, mode_t mode, dev_t dev) { (void)path; (void)mode; (void)dev; errno = EPERM; return -1; }

/* copy between two descriptors through a buffer */
ssize_t sendfile(int out, int in, off_t* offset, size_t count) {
    char buf[4096];
    size_t done = 0;
    while (done < count) {
        size_t n = count - done < sizeof(buf) ? count - done : sizeof(buf);
        ssize_t r = offset ? pread(in, buf, n, *offset + (off_t)done) : read(in, buf, n);
        if (r <= 0) { if (r < 0 && !done) return -1; break; }
        ssize_t w = write(out, buf, (size_t)r);
        if (w < 0) { if (!done) return -1; break; }
        done += (size_t)w;
        if (w < r) break;
    }
    if (offset) *offset += (off_t)done;
    return (ssize_t)done;
}

/* extended attributes: none */
ssize_t getxattr(const char* p, const char* n, void* v, size_t s) { (void)p; (void)n; (void)v; (void)s; errno = ENOTSUP; return -1; }
ssize_t fgetxattr(int fd, const char* n, void* v, size_t s) { (void)fd; (void)n; (void)v; (void)s; errno = ENOTSUP; return -1; }
int setxattr(const char* p, const char* n, const void* v, size_t s, int f) { (void)p; (void)n; (void)v; (void)s; (void)f; errno = ENOTSUP; return -1; }
int fsetxattr(int fd, const char* n, const void* v, size_t s, int f) { (void)fd; (void)n; (void)v; (void)s; (void)f; errno = ENOTSUP; return -1; }
int removexattr(const char* p, const char* n) { (void)p; (void)n; errno = ENOTSUP; return -1; }
int fremovexattr(int fd, const char* n) { (void)fd; (void)n; errno = ENOTSUP; return -1; }
ssize_t listxattr(const char* p, char* l, size_t s) { (void)p; (void)l; (void)s; return 0; }
ssize_t flistxattr(int fd, char* l, size_t s) { (void)fd; (void)l; (void)s; return 0; }

/* the mounted file systems: the system disk at "/" */
typedef struct { int next; } mnt_iter_t;
FILE* setmntent(const char* file, const char* mode) {
    (void)file; (void)mode;
    mnt_iter_t* it = calloc(1, sizeof(*it));
    return (FILE*)it;
}
struct mntent* getmntent_r(FILE* f, struct mntent* m, char* buf, int len) {
    mnt_iter_t* it = (mnt_iter_t*)f;
    if (!it || it->next > 0 || len < 32) return NULL;
    it->next++;
    snprintf(buf, (size_t)len, "%s%c%s%c%s%c%s", "/dev/disk0", 0, "/", 0, "bananafs", 0, "rw");
    m->mnt_fsname = buf;
    m->mnt_dir = buf + 11;
    m->mnt_type = buf + 13;
    m->mnt_opts = buf + 22;
    m->mnt_freq = m->mnt_passno = 0;
    return m;
}
struct mntent* getmntent(FILE* f) { static struct mntent m; static char b[64]; return getmntent_r(f, &m, b, sizeof(b)); }
int endmntent(FILE* f) { free(f); return 1; }
char* hasmntopt(const struct mntent* m, const char* opt) { return strstr(m->mnt_opts, opt); }

/* ── terminals, processes, events ─────────────────────────────────── */

static tcflag_t g_lflag = ICANON | ECHO | ISIG;
int tcgetattr(int fd, struct termios* t) {
    if (fd > 2) { errno = ENOTTY; return -1; }
    memset(t, 0, sizeof(*t));
    t->c_lflag = g_lflag;
    t->c_iflag = ICRNL;
    t->c_oflag = OPOST;
    t->c_ispeed = t->c_ospeed = B38400;
    return 0;
}
int tcsetattr(int fd, int when, const struct termios* t) {
    (void)when;
    if (fd > 2) { errno = ENOTTY; return -1; }
    g_lflag = t->c_lflag;
    return 0;
}

int waitid(idtype_t type, id_t id, siginfo_t* info, int options) { (void)type; (void)id; (void)info; (void)options; errno = ECHILD; return -1; }
int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* a,
                char* const argv[], char* const envp[]) {
    (void)pid; (void)path; (void)fa; (void)a; (void)argv; (void)envp;
    return ENOSYS;
}
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* a,
                 char* const argv[], char* const envp[]) {
    return posix_spawn(pid, file, fa, a, argv, envp);
}
int posix_spawnattr_init(posix_spawnattr_t* a) { a->__flags = 0; return 0; }
int posix_spawnattr_destroy(posix_spawnattr_t* a) { (void)a; return 0; }
int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa) { fa->__n = 0; return 0; }
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa) { (void)fa; return 0; }

/* (epoll, eventfd: ufd.c) inotify: not here yet (Java's file watching fails cleanly) */
int inotify_init(void) { errno = ENOSYS; return -1; }
int inotify_init1(int flags) { (void)flags; errno = ENOSYS; return -1; }
int inotify_add_watch(int fd, const char* path, uint32_t mask) { (void)fd; (void)path; (void)mask; errno = EBADF; return -1; }
int inotify_rm_watch(int fd, int wd) { (void)fd; (void)wd; errno = EBADF; return -1; }

/* the network interfaces by name: lo (1), eth0 (2) */
unsigned int if_nametoindex(const char* name) { return !strcmp(name, "lo") ? 1 : !strcmp(name, "eth0") ? 2 : 0; }
char* if_indextoname(unsigned int index, char* name) {
    if (index < 1 || index > 2) { errno = ENXIO; return NULL; }
    strcpy(name, index == 1 ? "lo" : "eth0");
    return name;
}
struct if_nameindex* if_nameindex(void) {
    static char lo[] = "lo", eth[] = "eth0";
    struct if_nameindex* p = calloc(3, sizeof(*p));
    if (!p) { errno = ENOMEM; return NULL; }
    p[0].if_index = 1; p[0].if_name = lo;
    p[1].if_index = 2; p[1].if_name = eth;
    return p;
}
void if_freenameindex(struct if_nameindex* p) { free(p); }

/* ── for C++'s standard library and Mesa ──────────────────────────── */

char* program_invocation_name = "app";
char* program_invocation_short_name = "app";

/* locale_t: the one "C" locale */
static struct { int dummy; } g_c_locale;
locale_t newlocale(int mask, const char* name, locale_t base) {
    (void)mask; (void)base;
    if (name && *name && strcmp(name, "C") && strcmp(name, "POSIX") && !strstr(name, "UTF-8") && !strstr(name, "utf8")) {
        errno = ENOENT;
        return NULL;
    }
    return (locale_t)&g_c_locale;
}
locale_t duplocale(locale_t l) { (void)l; return (locale_t)&g_c_locale; }
void freelocale(locale_t l) { (void)l; }
locale_t uselocale(locale_t l) { (void)l; return (locale_t)&g_c_locale; }
double strtod_l(const char* s, char** end, locale_t l) { (void)l; return strtod(s, end); }
float strtof_l(const char* s, char** end, locale_t l) { (void)l; return strtof(s, end); }
int strcoll(const char* a, const char* b) { return strcmp(a, b); }
size_t strxfrm(char* d, const char* s, size_t n) { size_t k = strlen(s); if (k < n) memcpy(d, s, k + 1); return k; }
int mblen(const char* s, size_t n) { return s ? mbtowc(NULL, s, n) : 0; }

static void (*g_quick[16])(void);
static int g_nquick;
int at_quick_exit(void (*fn)(void)) { if (g_nquick == 16) return -1; g_quick[g_nquick++] = fn; return 0; }
void quick_exit(int code) { while (g_nquick) g_quick[--g_nquick](); _Exit(code); }

int timespec_get(struct timespec* ts, int base) { return base == TIME_UTC && clock_gettime(CLOCK_REALTIME, ts) == 0 ? base : 0; }

int memfd_create(const char* name, unsigned int flags) { (void)name; (void)flags; errno = ENOSYS; return -1; }

/* random bytes: the kernel's generator */
ssize_t getrandom(void* buf, size_t len, unsigned int flags) {
    (void)flags;
    unsigned char* b = buf;
    for (size_t i = 0; i < len; i += 4) {
        unsigned int r = __banana->random();
        size_t k = len - i < 4 ? len - i : 4;
        memcpy(b + i, &r, k);
    }
    return (ssize_t)len;
}
int getentropy(void* buf, size_t len) { if (len > 256) { errno = EIO; return -1; } getrandom(buf, len, 0); return 0; }

/* the system log: standard error */
static const char* g_log_ident;
void openlog(const char* ident, int option, int facility) { (void)option; (void)facility; g_log_ident = ident; }
void closelog(void) { g_log_ident = NULL; }
int setlogmask(int mask) { (void)mask; return 0xff; }
void vsyslog(int priority, const char* fmt, va_list ap) {
    (void)priority;
    if (g_log_ident) fprintf(stderr, "%s: ", g_log_ident);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void syslog(int priority, const char* fmt, ...) { va_list ap; va_start(ap, fmt); vsyslog(priority, fmt, ap); va_end(ap); }

/* ── more wide characters ─────────────────────────────────────────── */

int wcscoll(const wchar_t* a, const wchar_t* b) { return wcscmp(a, b); }
size_t wcsxfrm(wchar_t* d, const wchar_t* s, size_t n) { size_t k = wcslen(s); if (k < n) wmemcpy(d, s, k + 1); return k; }
wchar_t* wcsncat(wchar_t* d, const wchar_t* s, size_t n) {
    wchar_t* e = d + wcslen(d);
    while (n-- && *s) *e++ = *s++;
    *e = 0;
    return d;
}
size_t wcsspn(const wchar_t* s, const wchar_t* a) { size_t n = 0; while (s[n] && wcschr(a, s[n])) n++; return n; }
size_t wcscspn(const wchar_t* s, const wchar_t* r) { size_t n = 0; while (s[n] && !wcschr(r, s[n])) n++; return n; }
wchar_t* wcspbrk(const wchar_t* s, const wchar_t* a) { s += wcscspn(s, a); return *s ? (wchar_t*)s : NULL; }
wchar_t* wcsstr(const wchar_t* h, const wchar_t* n) {
    size_t k = wcslen(n);
    for (; *h; h++) if (!wcsncmp(h, n, k)) return (wchar_t*)h;
    return k ? NULL : (wchar_t*)h;
}
wchar_t* wcstok(wchar_t* s, const wchar_t* delim, wchar_t** save) {
    if (!s) s = *save;
    s += wcsspn(s, delim);
    if (!*s) { *save = s; return NULL; }
    wchar_t* e = s + wcscspn(s, delim);
    if (*e) *e++ = 0;
    *save = e;
    return s;
}
wchar_t* wmemmove(wchar_t* d, const wchar_t* s, size_t n) { memmove(d, s, n * sizeof(wchar_t)); return d; }
int wmemcmp(const wchar_t* a, const wchar_t* b, size_t n) {
    for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return 0;
}
wchar_t* wmemchr(const wchar_t* s, wchar_t c, size_t n) { for (size_t i = 0; i < n; i++) if (s[i] == c) return (wchar_t*)s + i; return NULL; }

/* numbers: through an ASCII copy */
static const wchar_t* wnum(const wchar_t* s, char* buf, size_t cap) {
    size_t i = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    for (const wchar_t* p = s; *p && *p < 0x80 && i < cap - 1; p++) buf[i++] = (char)*p;
    buf[i] = 0;
    return s;
}
#define WNUM(name, type, call)                                               \
    type name {                                                              \
        char b[128], *e;                                                     \
        const wchar_t* st = wnum(s, b, sizeof(b));                           \
        type v = call;                                                       \
        if (end) *end = (wchar_t*)(e == b ? s : st + (e - b));               \
        return v;                                                            \
    }
WNUM(wcstol(const wchar_t* s, wchar_t** end, int base), long, strtol(b, &e, base))
WNUM(wcstoul(const wchar_t* s, wchar_t** end, int base), unsigned long, strtoul(b, &e, base))
WNUM(wcstoll(const wchar_t* s, wchar_t** end, int base), long long, strtoll(b, &e, base))
WNUM(wcstoull(const wchar_t* s, wchar_t** end, int base), unsigned long long, strtoull(b, &e, base))
WNUM(wcstod(const wchar_t* s, wchar_t** end), double, strtod(b, &e))
WNUM(wcstof(const wchar_t* s, wchar_t** end), float, strtof(b, &e))
WNUM(wcstold(const wchar_t* s, wchar_t** end), long double, strtold(b, &e))

size_t mbrlen(const char* s, size_t n, mbstate_t* ps) { return mbrtowc(NULL, s, n, ps); }
size_t mbsrtowcs(wchar_t* d, const char** s, size_t n, mbstate_t* ps) {
    static mbstate_t implicit;
    if (!ps) ps = &implicit;
    mbstate_t query = {0};
    query.__state = ps->__state;
    if (!d) ps = &query;
    size_t k = 0;
    const char* p = *s;
    while (!d || k < n) {
        wchar_t c;
        size_t r = mbrtowc(&c, p, 4, ps);
        if (r == (size_t)-1 || r == (size_t)-2) { if (d) *s = p; errno = EILSEQ; return (size_t)-1; }
        if (!c) { if (d) { d[k] = 0; *s = NULL; } return k; }
        if (d) d[k] = c;
        k++;
        p += r;
    }
    if (d) *s = p;
    return k;
}
size_t wcsrtombs(char* d, const wchar_t** s, size_t n, mbstate_t* ps) {
    (void)ps;
    size_t k = 0;
    const wchar_t* p = *s;
    for (;; p++) {
        char b[4] = { 0 };
        size_t r = *p ? wcrtomb(b, *p, NULL) : 1;
        if (r == (size_t)-1) { errno = EILSEQ; return (size_t)-1; }
        if (d && k + r > n) break;
        if (d) memcpy(d + k, b, r);
        if (!*p) { if (d) *s = NULL; return k; }
        k += r;
    }
    *s = p;
    return k;
}

/* wide stdio: UTF-8 on the byte stream */
wint_t fgetwc(FILE* f) {
    int c = fgetc(f);
    if (c == EOF) return WEOF;
    if (c < 0x80) return (wint_t)c;
    char b[4] = { (char)c };
    int n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
    for (int i = 1; i < n; i++) { int d = fgetc(f); if (d == EOF) return WEOF; b[i] = (char)d; }
    wchar_t w;
    return mbrtowc(&w, b, (size_t)n, NULL) > 4 ? WEOF : (wint_t)w;
}
wint_t getwc(FILE* f) { return fgetwc(f); }
wint_t getwchar(void) { return fgetwc(stdin); }
wchar_t* fgetws(wchar_t* s, int n, FILE* f) {
    int i = 0;
    while (i < n - 1) {
        wint_t c = fgetwc(f);
        if (c == WEOF) break;
        s[i++] = (wchar_t)c;
        if (c == L'\n') break;
    }
    if (!i) return NULL;
    s[i] = 0;
    return s;
}
wint_t fputwc(wchar_t c, FILE* f) {
    char b[4];
    size_t n = wcrtomb(b, c, NULL);
    if (n == (size_t)-1 || fwrite(b, 1, n, f) != n) return WEOF;
    return (wint_t)c;
}
wint_t putwc(wchar_t c, FILE* f) { return fputwc(c, f); }
wint_t putwchar(wchar_t c) { return fputwc(c, stdout); }
int fputws(const wchar_t* s, FILE* f) { for (; *s; s++) if (fputwc(*s, f) == WEOF) return -1; return 0; }
wint_t ungetwc(wint_t c, FILE* f) { return c < 0x80 && ungetc((int)c, f) != EOF ? c : WEOF; }
int fwide(FILE* f, int mode) { (void)f; return mode; }

/* wide formatting: the format and the result through UTF-8 */
static char* w2u(const wchar_t* w) {
    size_t n = wcslen(w) * 4;
    char* s = malloc(n + 1);
    if (!s) return NULL;
    const wchar_t* q = w;
    if (wcsrtombs(s, &q, n + 1, NULL) == (size_t)-1) { free(s); return NULL; }
    return s;
}
int vswprintf(wchar_t* s, size_t n, const wchar_t* fmt, va_list ap) {
    char* f = w2u(fmt);
    if (!f) return -1;
    char out[1024];
    int r = vsnprintf(out, sizeof(out), f, ap);
    free(f);
    if (r < 0) return -1;
    size_t k = mbstowcs(s, out, n);
    if (k == (size_t)-1 || k >= n) { if (n) s[n - 1] = 0; return -1; }
    return (int)k;
}
int swprintf(wchar_t* s, size_t n, const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vswprintf(s, n, fmt, ap); va_end(ap); return r; }
int vfwprintf(FILE* f, const wchar_t* fmt, va_list ap) {
    char* u = w2u(fmt);
    if (!u) return -1;
    int r = vfprintf(f, u, ap);
    free(u);
    return r;
}
int fwprintf(FILE* f, const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfwprintf(f, fmt, ap); va_end(ap); return r; }
int vwprintf(const wchar_t* fmt, va_list ap) { return vfwprintf(stdout, fmt, ap); }
int wprintf(const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfwprintf(stdout, fmt, ap); va_end(ap); return r; }
int vswscanf(const wchar_t* s, const wchar_t* fmt, va_list ap) {
    char* u = w2u(s);
    char* f = w2u(fmt);
    int r = u && f ? vsscanf(u, f, ap) : -1;
    free(u);
    free(f);
    return r;
}
int swscanf(const wchar_t* s, const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vswscanf(s, fmt, ap); va_end(ap); return r; }
int vfwscanf(FILE* f, const wchar_t* fmt, va_list ap) {
    char* u = w2u(fmt);
    int r = u ? vfscanf(f, u, ap) : -1;
    free(u);
    return r;
}
int fwscanf(FILE* f, const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfwscanf(f, fmt, ap); va_end(ap); return r; }
int vwscanf(const wchar_t* fmt, va_list ap) { return vfwscanf(stdin, fmt, ap); }
int wscanf(const wchar_t* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfwscanf(stdin, fmt, ap); va_end(ap); return r; }

ldiv_t ldiv(long a, long b) { ldiv_t d = { a / b, a % b }; return d; }
lldiv_t lldiv(long long a, long long b) { lldiv_t d = { a / b, a % b }; return d; }
int system(const char* cmd) { return cmd ? -1 : 0; }               /* (no shell to run it) */
char* secure_getenv(const char* name) { return getenv(name); }

int vasprintf(char** out, const char* fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    *out = NULL;
    if (n < 0) return -1;
    char* s = malloc((size_t)n + 1);
    if (!s) return -1;
    vsnprintf(s, (size_t)n + 1, fmt, ap);
    *out = s;
    return n;
}
int asprintf(char** out, const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vasprintf(out, fmt, ap); va_end(ap); return r; }

FILE* popen(const char* cmd, const char* mode) { (void)cmd; (void)mode; errno = ENOSYS; return NULL; }
int pclose(FILE* f) { (void)f; errno = ECHILD; return -1; }
