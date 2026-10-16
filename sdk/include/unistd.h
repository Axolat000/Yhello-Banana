#ifndef _UNISTD_H
#define _UNISTD_H
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
#endif
