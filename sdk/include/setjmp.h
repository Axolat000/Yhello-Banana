/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _SETJMP_H
#define _SETJMP_H
#ifdef __cplusplus
extern "C" {
#endif
/* Banana OS SDK: non-local jumps (lib/setjmp.c) */
#ifdef __x86_64__
typedef long jmp_buf[8];
#else
typedef long jmp_buf[6];
#endif
int  setjmp(jmp_buf env) __attribute__((returns_twice));
void longjmp(jmp_buf env, int val) __attribute__((noreturn));
#define _setjmp setjmp
#define _longjmp longjmp

/* sigsetjmp(env, 1) also keeps the thread's signal mask, which siglongjmp
 * puts back - the way out of a signal handler (lib/signal.c) */
typedef struct { jmp_buf __jb; int __saved; unsigned long long __mask; } sigjmp_buf[1];
unsigned long long __sigmask_save(void);
void siglongjmp(sigjmp_buf env, int val) __attribute__((noreturn));
#define sigsetjmp(env, save) \
    ((env)->__saved = (save), (env)->__mask = (save) ? __sigmask_save() : 0, setjmp((env)->__jb))
#ifdef __cplusplus
}
#endif
#endif
