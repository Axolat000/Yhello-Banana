/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _UNISTD_H
#define _UNISTD_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stddef.h>
#include <sys/types.h>
off_t   lseek(int fd, off_t off, int whence);
int     isatty(int fd);
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
unsigned int sleep(unsigned int seconds);
int     usleep(unsigned long usec);
ssize_t write(int fd, const void* buf, size_t n);
ssize_t read(int fd, void* buf, size_t n);
int     close(int fd);
char*   getcwd(char* buf, size_t size);
int     chdir(const char* path);
int     fchdir(int fd);
size_t  confstr(int name, char* buf, size_t len);
#define _CS_PATH 0
#define _CS_GNU_LIBC_VERSION 2
#define _CS_GNU_LIBPTHREAD_VERSION 3
int     unlink(const char* path);
int     rmdir(const char* path);
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

/* the process: an app (one user, banana: uid 1000) */
pid_t   getpid(void);                   /* unique among the apps since boot */
pid_t   getppid(void);
pid_t   gettid(void);                   /* the calling thread's */
uid_t   getuid(void);
uid_t   geteuid(void);
gid_t   getgid(void);
gid_t   getegid(void);
int     getpagesize(void);
int     gethostname(char* name, size_t len);
#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4
extern char** environ;
int     access(const char* path, int mode);
ssize_t readlink(const char* path, char* buf, size_t len);
int     symlink(const char* target, const char* path);
int     link(const char* from, const char* to);
int     chown(const char* path, uid_t u, gid_t g);
int     fchown(int fd, uid_t u, gid_t g);
int     lchown(const char* path, uid_t u, gid_t g);
int     fsync(int fd);
int     fdatasync(int fd);
void    sync(void);
int     ftruncate(int fd, off_t len);
ssize_t pread(int fd, void* buf, size_t n, off_t off);
ssize_t pwrite(int fd, const void* buf, size_t n, off_t off);
/* (off_t is 64-bit already: the *64 names are the same calls) */
#define lseek64 lseek
#define pread64 pread
#define pwrite64 pwrite
#define ftruncate64 ftruncate
int     dup(int fd);
int     dup2(int fd, int to);
int     dup3(int fd, int to, int flags);
int     pipe(int fds[2]);
int     pipe2(int fds[2], int flags);
pid_t   fork(void);
pid_t   vfork(void);
int     execve(const char* path, char* const argv[], char* const envp[]);
int     execv(const char* path, char* const argv[]);
int     execvp(const char* file, char* const argv[]);
pid_t   setsid(void);
pid_t   getpgrp(void);
int     setpgid(pid_t pid, pid_t pgid);
unsigned alarm(unsigned seconds);
char*   getlogin(void);
char*   ttyname(int fd);
long    pathconf(const char* path, int name);
long    fpathconf(int fd, int name);
int     getdtablesize(void);
#define _PC_NAME_MAX 3
#define _PC_PATH_MAX 4
void    _exit(int code) __attribute__((noreturn));      /* at once: no stdio flush */

/* sysconf() names (Linux's values) */
#define _SC_CLK_TCK            2
#define _SC_OPEN_MAX           4
#define _SC_PAGESIZE           30
#define _SC_PAGE_SIZE          _SC_PAGESIZE
#define _SC_IOV_MAX            60
#define _SC_GETGR_R_SIZE_MAX   69
#define _SC_GETPW_R_SIZE_MAX   70
#define _SC_THREAD_KEYS_MAX    74
#define _SC_THREAD_STACK_MIN   75
#define _SC_THREAD_THREADS_MAX 76
#define _SC_NPROCESSORS_CONF   83
#define _SC_NPROCESSORS_ONLN   84
#define _SC_PHYS_PAGES         85
#define _SC_AVPHYS_PAGES       86
#define _SC_LINE_MAX           43
#define _SC_HOST_NAME_MAX      180
long    sysconf(int name);
#ifdef __cplusplus
}
#endif
#endif
