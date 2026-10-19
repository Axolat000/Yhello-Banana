/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _FENV_H
#define _FENV_H
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned short fexcept_t;
typedef struct { unsigned short cw, sw; unsigned int mxcsr; } fenv_t;
#define FE_INVALID    0x01
#define FE_DIVBYZERO  0x04
#define FE_OVERFLOW   0x08
#define FE_UNDERFLOW  0x10
#define FE_INEXACT    0x20
#define FE_ALL_EXCEPT 0x3d
#define FE_TONEAREST  0
#define FE_DOWNWARD   0x400
#define FE_UPWARD     0x800
#define FE_TOWARDZERO 0xc00
int fegetround(void);
int fesetround(int mode);
int fegetenv(fenv_t* e);
int fesetenv(const fenv_t* e);
int feclearexcept(int e);
int fetestexcept(int e);
#ifdef __cplusplus
}
#endif
#endif
