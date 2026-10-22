/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SIGNAL_H
#define _SIGNAL_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Signals (Banana OS API version 19), with Linux's numbers.
 *
 * 64-bit: a CPU fault in the app's code - a bad memory access, a division
 * by zero, an invalid instruction - goes to its SIGSEGV / SIGFPE / SIGILL
 * / SIGBUS / SIGTRAP handler with a siginfo_t (si_addr) and a ucontext_t
 * whose registers the handler may change (returning resumes from them);
 * sigaltstack gives handlers a stack of their own. kill / raise /
 * pthread_kill reach a thread when it next leaves a system call or the
 * timer interrupts it, and wake it from a wait. Ctrl+C is SIGINT once
 * there is a handler (or SIG_IGN) for it. The default action of a signal
 * is Linux's: end the app, or nothing (SIGCHLD, SIGCONT, SIGURG, SIGWINCH).
 *
 * 32-bit: handlers run for raise() only; faults end the app as before.
 * Not there: sigwait, real-time signal queues, SIGPIPE on sockets (send
 * fails with EPIPE), alarm / timers.
 */
#include <stddef.h>
#include <sys/types.h>
#include <time.h>

typedef int sig_atomic_t;
#ifndef __sigset_t_defined
#define __sigset_t_defined
typedef struct { unsigned long __val[1024 / (8 * sizeof(unsigned long))]; } sigset_t;
#endif

#define SIGHUP    1
#define SIGINT    2
#define SIGQUIT   3
#define SIGILL    4
#define SIGTRAP   5
#define SIGABRT   6
#define SIGIOT    SIGABRT
#define SIGBUS    7
#define SIGFPE    8
#define SIGKILL   9
#define SIGUSR1   10
#define SIGSEGV   11
#define SIGUSR2   12
#define SIGPIPE   13
#define SIGALRM   14
#define SIGTERM   15
#define SIGSTKFLT 16
#define SIGCHLD   17
#define SIGCONT   18
#define SIGSTOP   19
#define SIGTSTP   20
#define SIGTTIN   21
#define SIGTTOU   22
#define SIGURG    23
#define SIGXCPU   24
#define SIGXFSZ   25
#define SIGVTALRM 26
#define SIGPROF   27
#define SIGWINCH  28
#define SIGIO     29
#define SIGPOLL   SIGIO
#define SIGPWR    30
#define SIGSYS    31
#define SIGRTMIN  34
#define SIGRTMAX  64
#define NSIG      65
#define _NSIG     NSIG

typedef void (*sighandler_t)(int);
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

union sigval { int sival_int; void* sival_ptr; };
/* how a timer tells (timer_create): a signal, or nothing (no SIGEV_THREAD) */
#define SIGEV_SIGNAL 0
#define SIGEV_NONE   1
#define SIGEV_THREAD 2
struct sigevent {
    union sigval sigev_value;
    int  sigev_signo;
    int  sigev_notify;
    void (*sigev_notify_function)(union sigval);
    void* sigev_notify_attributes;
    char __pad[32];
};

/* (the same layout as the system's banana_siginfo_t) */
typedef struct {
    int          si_signo, si_errno, si_code, __pad0;
    void*        si_addr;
    int          si_pid, si_uid;
    union sigval si_value;
    long         __pad1[11];
} siginfo_t;

/* si_code */
/* (fields some signals carry, in the spare words) */
#define si_status __pad0
#define si_band   __pad1[0]
#define si_fd     __pad1[1]
#define SI_USER     0
#define SI_KERNEL   0x80
#define SI_TKILL    (-6)
#define SEGV_MAPERR 1
#define SEGV_ACCERR 2
#define BUS_ADRALN  1
#define BUS_ADRERR  2
#define BUS_OBJERR  3
#define SEGV_BNDERR 3
#define SEGV_PKUERR 4
#define FPE_INTDIV  1
#define FPE_INTOVF  2
#define FPE_FLTDIV  3
#define FPE_FLTOVF  4
#define FPE_FLTUND  5
#define FPE_FLTRES  6
#define FPE_FLTINV  7
#define FPE_FLTSUB  8
#define ILL_ILLOPC  1
#define ILL_ILLOPN  2
#define ILL_ILLADR  3
#define ILL_ILLTRP  4
#define ILL_PRVOPC  5
#define ILL_PRVREG  6
#define ILL_COPROC  7
#define ILL_BADSTK  8
#define TRAP_BRKPT  1
#define TRAP_TRACE  2
#define CLD_EXITED    1
#define CLD_KILLED    2
#define CLD_DUMPED    3
#define CLD_TRAPPED   4
#define CLD_STOPPED   5
#define CLD_CONTINUED 6
#define POLL_IN  1
#define POLL_OUT 2
#define POLL_MSG 3
#define POLL_ERR 4
#define POLL_PRI 5
#define POLL_HUP 6
#define SI_QUEUE   (-1)
#define SI_TIMER   (-2)
#define SI_MESGQ   (-3)
#define SI_ASYNCIO (-4)
#define SI_SIGIO   (-5)

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t*, void*);
    } __sa;
    sigset_t sa_mask;
    int      sa_flags;
    void   (*sa_restorer)(void);
};
#define sa_handler   __sa.sa_handler
#define sa_sigaction __sa.sa_sigaction

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTORER  0x04000000
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000
#define SA_NOMASK    SA_NODEFER
#define SA_ONESHOT   SA_RESETHAND

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

typedef struct { void* ss_sp; int ss_flags; size_t ss_size; } stack_t;
#define SS_ONSTACK  1
#define SS_DISABLE  2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ    8192

sighandler_t signal(int sig, sighandler_t handler);
int  sigaction(int sig, const struct sigaction* act, struct sigaction* old);
int  sigemptyset(sigset_t* s);
int  sigfillset(sigset_t* s);
int  sigaddset(sigset_t* s, int sig);
int  sigdelset(sigset_t* s, int sig);
int  sigismember(const sigset_t* s, int sig);
int  sigprocmask(int how, const sigset_t* set, sigset_t* old);
int  sigpending(sigset_t* s);
int  sigsuspend(const sigset_t* mask);
int  sigaltstack(const stack_t* ss, stack_t* old);
int  kill(pid_t pid, int sig);
int  killpg(pid_t pgrp, int sig);
int  raise(int sig);
void psignal(int sig, const char* s);
int  siginterrupt(int sig, int flag);

#include <pthread.h>
int  pthread_sigmask(int how, const sigset_t* set, sigset_t* old);
int  pthread_kill(pthread_t t, int sig);
#ifdef __cplusplus
}
#endif
#endif
