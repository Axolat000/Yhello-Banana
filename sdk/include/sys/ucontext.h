#ifndef _SYS_UCONTEXT_H
#define _SYS_UCONTEXT_H
#ifdef __cplusplus
extern "C" {
#endif
/* A signal handler's third argument: the registers the thread was stopped
 * with (x86_64, Linux's order) - change them and return to resume there.
 * (The system's banana_ucontext_t; getcontext / makecontext are not there.) */
#include <signal.h>
#include "banana_api.h"

typedef long long greg_t;
#define NGREG 23
typedef greg_t gregset_t[NGREG];
typedef struct _libc_fpstate* fpregset_t;
typedef banana_mcontext_t mcontext_t;
typedef banana_ucontext_t ucontext_t;

enum {
    REG_R8 = 0, REG_R9, REG_R10, REG_R11, REG_R12, REG_R13, REG_R14, REG_R15, REG_RDI, REG_RSI,
    REG_RBP, REG_RBX, REG_RDX, REG_RAX, REG_RCX, REG_RSP, REG_RIP, REG_EFL, REG_CSGSFS, REG_ERR,
    REG_TRAPNO, REG_OLDMASK, REG_CR2
};
#ifdef __cplusplus
}
#endif
#endif
