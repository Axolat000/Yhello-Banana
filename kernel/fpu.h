/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef FPU_H
#define FPU_H

#include "types.h"

/*
 * The FPU, SSE and AVX registers of tasks (kernel/fpu.c). The kernel itself
 * is built without SSE, but apps compute with SSE and - where the processor
 * has it - AVX / AVX2 / AVX-512, and the browser's JavaScript with the x87:
 * every task's registers are saved when it is switched out and restored
 * when it comes back. With AVX the save is xsave (the YMM / ZMM registers
 * too), else fxsave (x87 + SSE), or fnsave on processors without SSE.
 */

/* a task's save area: AVX-512's standard xsave layout (2688 bytes) fits */
#define FPU_AREA 3072

/* every processor core: when it starts (the boot core first - it decides
 * what is turned on), and again after a wake from sleep, where the
 * processor forgets the AVX setting */
void fpu_cpu_init(void);

/* the calling core's registers into / out of a task's area (64-byte aligned) */
void fpu_save(uint8_t* area);
void fpu_restore(uint8_t* area);
/* clean registers for a task starting: nothing of another task's left */
void fpu_reset(void);

/* what is on, for selftest and lscpu: "x87 SSE AVX AVX-512 (xsave, 2688 bytes)" */
void fpu_describe(char* out, int cap);
/* 1 if apps can use AVX (the processor has it and it is turned on) */
int  fpu_avx(void);

#endif
