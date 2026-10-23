/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPU_I915_LRC_H
#define GPU_I915_LRC_H
#include "gpu_i915.h"

/*
 * Intel graphics, gen 8 to 11 (5th to 10th generation Core: Broadwell,
 * Skylake, Kaby Lake, Coffee Lake, Comet Lake, Ice Lake): the render engine
 * with execlists - kernel/gpu_i915_lrc.c. kernel/gpu_i915.c's functions
 * come here on these GPUs; the address spaces (one per app: 48 bits, its
 * buffers where Mesa puts them) are these GPUs' own.
 */

void     lrc_attach(const i915_hw_t* hw);
int      lrc_present(void);
int      lrc_running(void);
int      lrc_start(void);
const char* lrc_why(void);
int64_t  lrc_gtt_alloc(uint32_t pages, uint32_t align);
void     lrc_gtt_free(uint32_t first, uint32_t pages);
void     lrc_gtt_bind(uint32_t first, const uint64_t* phys, uint32_t pages);
void     lrc_gtt_unbind(uint32_t first, uint32_t pages);
uint64_t lrc_gtt_size(void);
uint64_t lrc_gtt_free_bytes(void);
int      lrc_vm_create(void);
void     lrc_vm_destroy(int vm);
int      lrc_vm_bind(int vm, uint64_t addr, const uint64_t* phys, uint32_t pages);
void     lrc_vm_unbind(int vm, uint64_t addr, const uint64_t* phys, uint32_t pages);
uint64_t lrc_vm_size(void);
int      lrc_ctx_create(int vm);
void     lrc_ctx_destroy(int ctx);
uint32_t lrc_submit(uint64_t batch);
uint32_t lrc_submit_privileged(uint64_t batch);
uint32_t lrc_submit_ctx(int ctx, uint64_t batch);
int      lrc_done(uint32_t seqno);
int      lrc_wait(uint32_t seqno, int timeout_ms);
uint32_t lrc_last_submitted(void);
uint32_t lrc_resets(void);
uint64_t lrc_timestamp(void);
uint32_t lrc_timestamp_hz(void);
void     lrc_info(char* buf, int cap);
void     lrc_on_reset(void (*fn)(uint32_t, uint32_t));
void     lrc_topology(i915_topology_t* t);

#endif
