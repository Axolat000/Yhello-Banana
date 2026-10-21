/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: Unix processes - fork, the exec family, wait, process
 * groups and sessions, kill, posix_spawn, system and popen. The system's
 * calls are API version 26 (64-bit; the 32-bit system has no fork: ENOSYS). */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

#define HAS_PROC BANANA_HAS(__banana, proc_kill)

static int ret(int r) { if (r < 0) { errno = -r; return -1; } return r; }

pid_t fork(void) {
    if (!HAS_PROC) { errno = ENOSYS; return -1; }
    fflush(NULL);                       /* (a child must not print what the parent buffered too) */
    return ret(__banana->proc_fork());
}

/* the child shares nothing it could break here: a plain fork */
pid_t vfork(void) { return fork(); }

int execve(const char* path, char* const argv[], char* const envp[]) {
    if (!HAS_PROC) { errno = ENOSYS; return -1; }
    if (!path || !*path) { errno = ENOENT; return -1; }
    fflush(NULL);
    return ret(__banana->proc_exec(path, argv, envp));
}

int execv(const char* path, char* const argv[]) { return execve(path, argv, environ); }

int fexecve(int fd, char* const argv[], char* const envp[]) {
    (void)fd; (void)argv; (void)envp;
    errno = ENOSYS;
    return -1;
}

/* file searched in $PATH (unless it has a /), as execvp does */
int execvpe(const char* file, char* const argv[], char* const envp[]) {
    if (!file || !*file) { errno = ENOENT; return -1; }
    if (strchr(file, '/')) return execve(file, argv, envp);
    const char* path = getenv("PATH");
    if (!path) path = "/bin:/usr/bin";
    char buf[PATH_MAX];
    int seen_eacces = 0;
    for (const char* p = path; ; ) {
        const char* end = strchr(p, ':');
        size_t n = end ? (size_t)(end - p) : strlen(p);
        if (n + 1 + strlen(file) + 1 <= sizeof(buf)) {
            if (n) { memcpy(buf, p, n); buf[n] = '/'; strcpy(buf + n + 1, file); }
            else strcpy(buf, file);                             /* (an empty entry: here) */
            execve(buf, argv, envp);
            if (errno == EACCES) seen_eacces = 1;
            else if (errno != ENOENT && errno != ENOTDIR) return -1;
        }
        if (!end) break;
        p = end + 1;
    }
    errno = seen_eacces ? EACCES : ENOENT;
    return -1;
}

int execvp(const char* file, char* const argv[]) { return execvpe(file, argv, environ); }

/* execl, execlp, execle: the arguments into an array */
#define ARGV_FROM(first, ap, argv, extra)                                       \
    do {                                                                        \
        va_list cp;                                                             \
        va_copy(cp, ap);                                                        \
        int n = 1;                                                              \
        while (va_arg(cp, char*)) n++;                                          \
        va_end(cp);                                                             \
        argv = __builtin_alloca((size_t)(n + 1) * sizeof(char*));               \
        argv[0] = (char*)(first);                                               \
        for (int i = 1; i <= n; i++) argv[i] = va_arg(ap, char*);               \
        char*** ex_ = (extra);                                                  \
        if (ex_) *ex_ = va_arg(ap, char**);                                     \
    } while (0)

int execl(const char* path, const char* arg, ...) {
    va_list ap;
    va_start(ap, arg);
    char** argv;
    ARGV_FROM(arg, ap, argv, (char***)NULL);
    va_end(ap);
    return execve(path, argv, environ);
}

int execlp(const char* file, const char* arg, ...) {
    va_list ap;
    va_start(ap, arg);
    char** argv;
    ARGV_FROM(arg, ap, argv, (char***)NULL);
    va_end(ap);
    return execvp(file, argv);
}

int execle(const char* path, const char* arg, ...) {
    va_list ap;
    va_start(ap, arg);
    char** argv;
    char** envp = NULL;
    ARGV_FROM(arg, ap, argv, &envp);
    va_end(ap);
    return execve(path, argv, envp);
}

pid_t wait4(pid_t pid, int* status, int options, struct rusage* ru) {
    if (!HAS_PROC) { errno = ECHILD; return -1; }
    if (options & ~(WNOHANG | WUNTRACED | WCONTINUED)) { errno = EINVAL; return -1; }
    return ret(__banana->proc_wait(pid, status, options, ru));
}

pid_t waitpid(pid_t pid, int* status, int options) { return wait4(pid, status, options, NULL); }
pid_t wait(int* status) { return wait4(-1, status, 0, NULL); }
pid_t wait3(int* status, int options, struct rusage* ru) { return wait4(-1, status, options, ru); }

int waitid(idtype_t type, id_t id, siginfo_t* info, int options) {
    pid_t pid = type == P_ALL ? -1 : type == P_PGID ? -(pid_t)id : (pid_t)id;
    int st = 0;
    pid_t r = wait4(pid, &st, options & (WNOHANG | WUNTRACED | WCONTINUED), NULL);
    if (r < 0) return -1;
    if (info) {
        memset(info, 0, sizeof(*info));
        if (r > 0) {
            info->si_signo = SIGCHLD;
            info->si_pid = r;
            info->si_code = WIFSIGNALED(st) ? 2 /* CLD_KILLED */ : 1 /* CLD_EXITED */;
            info->si_status = WIFSIGNALED(st) ? WTERMSIG(st) : WEXITSTATUS(st);
        }
    }
    return 0;
}

static int ids(pid_t pid, int which) {
    int out[4];
    if (!HAS_PROC) return which == 1 ? 1 : getpid();
    int r = __banana->proc_ids(pid, out);
    if (r < 0) { errno = -r; return -1; }
    return out[which];
}

pid_t getppid(void) { return ids(0, 1); }
pid_t getpgid(pid_t pid) { return ids(pid, 2); }
pid_t getpgrp(void) { return ids(0, 2); }
pid_t getsid(pid_t pid) { return ids(pid, 3); }

int setpgid(pid_t pid, pid_t pgid) {
    if (!HAS_PROC) return 0;
    int r = __banana->proc_setpgid(pid, pgid);
    if (r == -1) { errno = EPERM; return -1; }
    return ret(r);
}
int setpgrp(void) { return setpgid(0, 0); }

pid_t setsid(void) {
    if (!HAS_PROC) return getpid();
    int r = __banana->proc_setsid();
    if (r == -1) { errno = EPERM; return -1; }
    return ret(r);
}

/* signal.c: a signal to the calling process (raise's way) */
int __kill_self(int sig);

int kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= NSIG) { errno = EINVAL; return -1; }
    if (!HAS_PROC || pid == getpid()) return __kill_self(sig);
    return ret(__banana->proc_kill(pid, sig));
}

int killpg(pid_t pgrp, int sig) {
    if (pgrp < 0) { errno = EINVAL; return -1; }
    return kill(pgrp ? -pgrp : 0, sig);
}

/* ── posix_spawn: fork, the file actions and attributes, exec ───────── */

enum { A_OPEN = 1, A_CLOSE, A_DUP2, A_CHDIR, A_FCHDIR };
struct __spawn_act { int what, fd, newfd, oflag; mode_t mode; char* path; };

static int add_act(posix_spawn_file_actions_t* fa, struct __spawn_act a) {
    if (fa->__n == fa->__cap) {
        int cap = fa->__cap ? fa->__cap * 2 : 8;
        struct __spawn_act* n = realloc(fa->__acts, (size_t)cap * sizeof(*n));
        if (!n) { free(a.path); return ENOMEM; }
        fa->__acts = n;
        fa->__cap = cap;
    }
    fa->__acts[fa->__n++] = a;
    return 0;
}

int posix_spawn_file_actions_init(posix_spawn_file_actions_t* fa) { memset(fa, 0, sizeof(*fa)); return 0; }
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t* fa) {
    for (int i = 0; i < fa->__n; i++) free(fa->__acts[i].path);
    free(fa->__acts);
    memset(fa, 0, sizeof(*fa));
    return 0;
}
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t* fa, int fd, const char* path, int oflag, mode_t mode) {
    if (fd < 0) return EBADF;
    char* p = strdup(path);
    if (!p) return ENOMEM;
    return add_act(fa, (struct __spawn_act){ A_OPEN, fd, 0, oflag, mode, p });
}
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t* fa, int fd) {
    if (fd < 0) return EBADF;
    return add_act(fa, (struct __spawn_act){ A_CLOSE, fd, 0, 0, 0, NULL });
}
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t* fa, int fd, int newfd) {
    if (fd < 0 || newfd < 0) return EBADF;
    return add_act(fa, (struct __spawn_act){ A_DUP2, fd, newfd, 0, 0, NULL });
}
int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t* fa, const char* path) {
    char* p = strdup(path);
    if (!p) return ENOMEM;
    return add_act(fa, (struct __spawn_act){ A_CHDIR, 0, 0, 0, 0, p });
}
int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t* fa, int fd) {
    return add_act(fa, (struct __spawn_act){ A_FCHDIR, fd, 0, 0, 0, NULL });
}

int posix_spawnattr_init(posix_spawnattr_t* a) { memset(a, 0, sizeof(*a)); return 0; }
int posix_spawnattr_destroy(posix_spawnattr_t* a) { (void)a; return 0; }
int posix_spawnattr_setflags(posix_spawnattr_t* a, short flags) { a->__flags = flags; return 0; }
int posix_spawnattr_getflags(const posix_spawnattr_t* a, short* flags) { *flags = (short)a->__flags; return 0; }
int posix_spawnattr_setpgroup(posix_spawnattr_t* a, pid_t pg) { a->__pgrp = pg; return 0; }
int posix_spawnattr_getpgroup(const posix_spawnattr_t* a, pid_t* pg) { *pg = a->__pgrp; return 0; }
int posix_spawnattr_setsigmask(posix_spawnattr_t* a, const sigset_t* s) { a->__mask = *s; return 0; }
int posix_spawnattr_getsigmask(const posix_spawnattr_t* a, sigset_t* s) { *s = a->__mask; return 0; }
int posix_spawnattr_setsigdefault(posix_spawnattr_t* a, const sigset_t* s) { a->__def = *s; return 0; }
int posix_spawnattr_getsigdefault(const posix_spawnattr_t* a, sigset_t* s) { *s = a->__def; return 0; }

static int spawn(pid_t* pidp, const char* file, int search, const posix_spawn_file_actions_t* fa,
                 const posix_spawnattr_t* at, char* const argv[], char* const envp[]) {
    /* the child tells the parent why exec failed through a close-on-exec pipe */
    int pp[2];
    if (pipe2(pp, O_CLOEXEC) != 0) return errno;
    pid_t pid = fork();
    if (pid < 0) { int e = errno; close(pp[0]); close(pp[1]); return e; }
    if (pid == 0) {
        close(pp[0]);
        int err = 0;
        if (at && (at->__flags & POSIX_SPAWN_SETSID) && setsid() < 0) err = errno;
        if (!err && at && (at->__flags & POSIX_SPAWN_SETPGROUP) && setpgid(0, at->__pgrp) < 0) err = errno;
        if (!err && at && (at->__flags & POSIX_SPAWN_SETSIGDEF))
            for (int s = 1; s < NSIG; s++)
                if (sigismember(&at->__def, s) == 1) signal(s, SIG_DFL);
        if (!err && at && (at->__flags & POSIX_SPAWN_SETSIGMASK)) sigprocmask(SIG_SETMASK, &at->__mask, NULL);
        for (int i = 0; !err && fa && i < fa->__n; i++) {
            const struct __spawn_act* a = &fa->__acts[i];
            switch (a->what) {
            case A_OPEN: {
                int fd = open(a->path, a->oflag, a->mode);
                if (fd < 0) { err = errno; break; }
                if (fd != a->fd) { if (dup2(fd, a->fd) < 0) err = errno; close(fd); }
                break;
            }
            case A_CLOSE: close(a->fd); break;
            case A_DUP2:
                if (a->fd == a->newfd) fcntl(a->fd, F_SETFD, 0);       /* (kept across exec) */
                else if (dup2(a->fd, a->newfd) < 0) err = errno;
                break;
            case A_CHDIR: if (chdir(a->path) < 0) err = errno; break;
            case A_FCHDIR: err = ENOSYS; break;                    /* (no descriptors for folders yet) */
            }
        }
        if (!err) {
            if (search) execvpe(file, argv, envp ? envp : environ);
            else execve(file, argv, envp ? envp : environ);
            err = errno;
        }
        write(pp[1], &err, sizeof(err));
        _exit(127);
    }
    close(pp[1]);
    int err = 0;
    ssize_t n;
    do n = read(pp[0], &err, sizeof(err)); while (n < 0 && errno == EINTR);
    close(pp[0]);
    if (n == (ssize_t)sizeof(err) && err) {            /* exec failed: the child is gone */
        waitpid(pid, NULL, 0);
        return err;
    }
    if (pidp) *pidp = pid;
    return 0;
}

int posix_spawn(pid_t* pid, const char* path, const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* a,
                char* const argv[], char* const envp[]) {
    return spawn(pid, path, 0, fa, a, argv, envp);
}
int posix_spawnp(pid_t* pid, const char* file, const posix_spawn_file_actions_t* fa, const posix_spawnattr_t* a,
                 char* const argv[], char* const envp[]) {
    return spawn(pid, file, 1, fa, a, argv, envp);
}

/* ── system, popen ────────────────────────────────────────────────── */

int system(const char* cmd) {
    if (!HAS_PROC) return cmd ? -1 : 0;
    if (!cmd) return access("/bin/sh", X_OK) == 0;
    char* argv[] = { "sh", "-c", (char*)cmd, NULL };
    struct sigaction ign = { 0 }, oint, oquit;
    ign.sa_handler = SIG_IGN;
    sigaction(SIGINT, &ign, &oint);
    sigaction(SIGQUIT, &ign, &oquit);
    pid_t pid;
    int st = -1;
    if (posix_spawn(&pid, "/bin/sh", NULL, NULL, argv, environ) == 0)
        while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
    else
        st = 127 << 8;
    sigaction(SIGINT, &oint, NULL);
    sigaction(SIGQUIT, &oquit, NULL);
    return st;
}

/* popen's pipes, by FILE, to know which child pclose waits for */
static struct { FILE* f; pid_t pid; } g_popen[32];

FILE* popen(const char* cmd, const char* mode) {
    if (!HAS_PROC) { errno = ENOSYS; return NULL; }
    if (!cmd || !mode || (mode[0] != 'r' && mode[0] != 'w')) { errno = EINVAL; return NULL; }
    int rd = mode[0] == 'r';
    int pp[2];
    if (pipe2(pp, O_CLOEXEC) != 0) return NULL;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, rd ? pp[1] : pp[0], rd ? 1 : 0);
    char* argv[] = { "sh", "-c", (char*)cmd, NULL };
    pid_t pid;
    int e = posix_spawn(&pid, "/bin/sh", &fa, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&fa);
    close(rd ? pp[1] : pp[0]);
    if (e) { close(rd ? pp[0] : pp[1]); errno = e; return NULL; }
    FILE* f = fdopen(rd ? pp[0] : pp[1], rd ? "r" : "w");
    if (!f) { close(rd ? pp[0] : pp[1]); waitpid(pid, NULL, 0); return NULL; }
    for (int i = 0; i < 32; i++) if (!g_popen[i].f) { g_popen[i].f = f; g_popen[i].pid = pid; break; }
    return f;
}

int pclose(FILE* f) {
    pid_t pid = -1;
    for (int i = 0; i < 32; i++) if (g_popen[i].f == f) { pid = g_popen[i].pid; g_popen[i].f = NULL; break; }
    if (pid < 0) { errno = ECHILD; return -1; }
    fclose(f);
    int st;
    while (waitpid(pid, &st, 0) < 0) if (errno != EINTR) return -1;
    return st;
}
