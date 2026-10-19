#ifndef BANANA_LLVM_COMPAT_H
#define BANANA_LLVM_COMPAT_H
#include <locale.h>
#include <stdio.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <wchar.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <spawn.h>
#include "../../../sdk/include/fenv.h"
#define _SC_ARG_MAX 0 /* Linux value; SDK sysconf returns EINVAL when unsupported. */
#define _POSIX_ARG_MAX 4096
typedef locale_t __locale_t;
/* GNU libstdc++'s C-locale character mask, little-endian representation. */
enum { _ISupper=0x100, _ISlower=0x200, _ISalpha=0x400, _ISdigit=0x800,
       _ISxdigit=0x1000, _ISspace=0x2000, _ISprint=0x4000, _ISgraph=0x8000,
       _ISblank=1, _IScntrl=2, _ISpunct=4, _ISalnum=8 };
typedef long fpos_t;
extern "C" {
int isblank(int);
int fegetexceptflag(fexcept_t*, int); int fesetexceptflag(const fexcept_t*, int);
int feraiseexcept(int); int feholdexcept(fenv_t*); int feupdateenv(const fenv_t*);
long syscall(long, ...);
int shm_open(const char*, int, mode_t);
int shm_unlink(const char*);
pid_t getsid(pid_t);
void* sbrk(ptrdiff_t);
int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t*, int, const char*, int, mode_t);
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t*, int, int);
pid_t wait4(pid_t, int*, int, struct rusage*);
intmax_t wcstoimax(const wchar_t*, wchar_t**, int);
uintmax_t wcstoumax(const wchar_t*, wchar_t**, int);
int pthread_rwlock_clockrdlock(pthread_rwlock_t*, clockid_t, const struct timespec*);
int pthread_rwlock_clockwrlock(pthread_rwlock_t*, clockid_t, const struct timespec*);
int pthread_rwlock_timedrdlock(pthread_rwlock_t*, const struct timespec*);
int pthread_rwlock_timedwrlock(pthread_rwlock_t*, const struct timespec*);
int fgetpos(FILE*, fpos_t*);
int fsetpos(FILE*, const fpos_t*);
FILE* freopen(const char*, const char*, FILE*);
void setbuf(FILE*, char*);
FILE* tmpfile(void);
char* tmpnam(char*);
int vscanf(const char*, va_list);
}
#endif
