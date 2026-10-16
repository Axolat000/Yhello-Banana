#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/types.h>
#include <sys/time.h>
typedef unsigned long rlim_t;
struct rlimit { rlim_t rlim_cur, rlim_max; };
#define RLIM_INFINITY ((rlim_t)-1)
#define RLIMIT_CPU     0
#define RLIMIT_FSIZE   1
#define RLIMIT_DATA    2
#define RLIMIT_STACK   3
#define RLIMIT_CORE    4
#define RLIMIT_RSS     5
#define RLIMIT_NPROC   6
#define RLIMIT_NOFILE  7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS      9
struct rusage {
    struct timeval ru_utime, ru_stime;
    long ru_maxrss, ru_ixrss, ru_idrss, ru_isrss, ru_minflt, ru_majflt, ru_nswap, ru_inblock, ru_oublock,
         ru_msgsnd, ru_msgrcv, ru_nsignals, ru_nvcsw, ru_nivcsw;
};
#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1
#define PRIO_PROCESS 0
int getrlimit(int res, struct rlimit* r);
int setrlimit(int res, const struct rlimit* r);
int getrusage(int who, struct rusage* r);
int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);
#ifdef __cplusplus
}
#endif
#endif
