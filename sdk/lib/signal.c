/* Banana OS SDK: signals (<signal.h>) over the system's sig_* calls (API
 * version 19). The system runs entry() on the thread's stack for every
 * signal with a handler; it applies sa_mask, SA_NODEFER and SA_RESETHAND
 * and calls the handler; returning goes to the system's sigreturn, which
 * puts the registers (and the mask) back from the ucontext. */
#include <signal.h>
#include <setjmp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

static struct sigaction g_act[NSIG];
static int g_entry_set;

static int has_sig(void) { return __banana->version >= 19 && BANANA_HAS(__banana, sig_pending); }

static unsigned long long bit(int sig) { return 1ull << (sig - 1); }
static unsigned long long low(const sigset_t* s) { return (unsigned long long)s->__val[0]
#if __SIZEOF_LONG__ == 4
    | (unsigned long long)s->__val[1] << 32
#endif
    ; }
static void set_low(sigset_t* s, unsigned long long v) {
    memset(s, 0, sizeof(*s));
    s->__val[0] = (unsigned long)v;
#if __SIZEOF_LONG__ == 4
    s->__val[1] = (unsigned long)(v >> 32);
#endif
}

/* the default action of a signal nobody handles: 1 if it ends the app */
static int ends_app(int sig) { return sig != SIGCHLD && sig != SIGCONT && sig != SIGURG && sig != SIGWINCH; }

static void dispatch(int sig, siginfo_t* info, void* uc) {
    struct sigaction a = g_act[sig];
    void (*h)(int) = a.sa_handler;
    if (h == SIG_DFL || h == SIG_IGN) return;           /* (changed meanwhile) */
    if (has_sig()) {
        unsigned long long add = low(&a.sa_mask);
        if (add) __banana->sig_mask(0, &add, NULL);
        if (a.sa_flags & SA_NODEFER) { unsigned long long b = bit(sig); __banana->sig_mask(1, &b, NULL); }
    }
    if (a.sa_flags & SA_RESETHAND) {
        g_act[sig].sa_handler = SIG_DFL;
        g_act[sig].sa_flags = 0;
        if (has_sig()) __banana->sig_action(sig, BANANA_SIG_DEFAULT, 0);
    }
    if (a.sa_flags & SA_SIGINFO) a.sa_sigaction(sig, info, uc);
    else h(sig);
}

/* where the system starts every handled signal (on the thread's stack) */
static void entry(int sig, void* info, void* uc) {
    if (sig > 0 && sig < NSIG) dispatch(sig, (siginfo_t*)info, uc);
}

int sigaction(int sig, const struct sigaction* act, struct sigaction* old) {
    if (sig < 1 || sig >= NSIG || (act && (sig == SIGKILL || sig == SIGSTOP))) { errno = EINVAL; return -1; }
    if (old) *old = g_act[sig];
    if (!act) return 0;
    g_act[sig] = *act;
    if (!has_sig()) return 0;
    if (!g_entry_set) { __banana->sig_entry(entry); g_entry_set = 1; }
    int d = act->sa_handler == SIG_IGN ? BANANA_SIG_IGNORE : act->sa_handler == SIG_DFL ? BANANA_SIG_DEFAULT : BANANA_SIG_CATCH;
    if (__banana->sig_action(sig, d, (act->sa_flags & SA_ONSTACK) != 0) != 0) { errno = EINVAL; return -1; }
    return 0;
}

sighandler_t signal(int sig, sighandler_t handler) {
    struct sigaction a, old;
    memset(&a, 0, sizeof(a));
    a.sa_handler = handler;
    a.sa_flags = SA_RESTART;
    if (sigaction(sig, &a, &old) != 0) return SIG_ERR;
    return old.sa_handler;
}

int sigemptyset(sigset_t* s) { memset(s, 0, sizeof(*s)); return 0; }
int sigfillset(sigset_t* s) { set_low(s, ~0ull); return 0; }
int sigaddset(sigset_t* s, int sig) {
    if (sig < 1 || sig >= NSIG) { errno = EINVAL; return -1; }
    set_low(s, low(s) | bit(sig));
    return 0;
}
int sigdelset(sigset_t* s, int sig) {
    if (sig < 1 || sig >= NSIG) { errno = EINVAL; return -1; }
    set_low(s, low(s) & ~bit(sig));
    return 0;
}
int sigismember(const sigset_t* s, int sig) {
    if (sig < 1 || sig >= NSIG) { errno = EINVAL; return -1; }
    return (low(s) & bit(sig)) != 0;
}

int pthread_sigmask(int how, const sigset_t* set, sigset_t* old) {
    if (how < 0 || how > 2) return EINVAL;
    unsigned long long n = set ? low(set) : 0, o = 0;
    if (has_sig() && __banana->sig_mask(how, set ? &n : NULL, &o) != 0) return EINVAL;
    if (old) set_low(old, o);
    return 0;
}

int sigprocmask(int how, const sigset_t* set, sigset_t* old) {
    int r = pthread_sigmask(how, set, old);
    if (r) { errno = r; return -1; }
    return 0;
}

int sigpending(sigset_t* s) {
    set_low(s, has_sig() ? __banana->sig_pending() : 0);
    return 0;
}

/* the mask set to `mask` until a signal is handled, then back: EINTR */
int sigsuspend(const sigset_t* mask) {
    sigset_t old;
    pthread_sigmask(SIG_SETMASK, mask, &old);
    if (has_sig()) __banana->sleep_ms(0x7FFFFFFF);    /* (a signal ends the sleep, its handler runs) */
    pthread_sigmask(SIG_SETMASK, &old, NULL);
    errno = EINTR;
    return -1;
}

int sigaltstack(const stack_t* ss, stack_t* old) {
    if (!has_sig()) { errno = ENOSYS; return -1; }
    if (ss && !(ss->ss_flags & SS_DISABLE) && ss->ss_size < MINSIGSTKSZ) { errno = ENOMEM; return -1; }
    if (__banana->sig_altstack(ss, old) != 0) { errno = EINVAL; return -1; }
    return 0;
}

/* ── sending ──────────────────────────────────────────────────────── */

/* sig for the calling thread, as the system would deliver it (32-bit) */
static int local_raise(int sig) {
    void (*h)(int) = g_act[sig].sa_handler;
    if (h == SIG_IGN) return 0;
    if (h == SIG_DFL) {
        if (!ends_app(sig)) return 0;
        fprintf(stderr, "ended by signal %d\n", sig);
        _exit(128 + sig);
    }
    siginfo_t info;
    memset(&info, 0, sizeof(info));
    info.si_signo = sig;
    info.si_code = SI_TKILL;
    info.si_pid = getpid();
    dispatch(sig, &info, NULL);
    return 0;
}

int raise(int sig) {
    if (sig < 0 || sig >= NSIG) { errno = EINVAL; return -1; }
    if (!sig) return 0;
#ifdef __x86_64__
    /* (delivered as the call returns: the handler has run when raise does) */
    if (has_sig() && __banana->sig_send(__banana->thread_id(), sig) == 0) return 0;
#endif
    return local_raise(sig);
}

int kill(pid_t pid, int sig) {
    if (sig < 0 || sig >= NSIG) { errno = EINVAL; return -1; }
    if (pid != 0 && pid != -1 && pid != getpid()) { errno = ESRCH; return -1; }  /* (other apps: not ours) */
    if (!sig) return 0;
#ifdef __x86_64__
    if (has_sig() && __banana->sig_send(-1, sig) == 0) return 0;
#endif
    return local_raise(sig);
}

int killpg(pid_t pgrp, int sig) { return kill(pgrp, sig); }
int siginterrupt(int sig, int flag) { (void)sig; (void)flag; return 0; }

void psignal(int sig, const char* s) {
    if (s && *s) fprintf(stderr, "%s: ", s);
    fprintf(stderr, "signal %d\n", sig);
}

/* ── sigsetjmp / siglongjmp ───────────────────────────────────────── */

unsigned long long __sigmask_save(void) {
    unsigned long long o = 0;
    if (has_sig()) __banana->sig_mask(0, NULL, &o);
    return o;
}

void siglongjmp(sigjmp_buf env, int val) {
    if (env->__saved && has_sig()) __banana->sig_mask(2, &env->__mask, NULL);
    longjmp(env->__jb, val ? val : 1);
}
