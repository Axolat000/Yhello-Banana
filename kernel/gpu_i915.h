/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPU_I915_H
#define GPU_I915_H
#include "types.h"

/*
 * Intel graphics, gen 5 to 7.5 (1st to 4th generation Core: Ironlake, Sandy
 * Bridge, Ivy Bridge, Haswell): the render engine, for 3D through Mesa's
 * crocus driver. kernel/gpu_i915.c drives the hardware (the GPU's address
 * spaces, the render ring, finished work, a hung GPU reset);
 * kernel/gpu_gem.c gives apps the part of Linux's i915 interface Mesa uses
 * (buffers, submissions, fences: kernel/i915_uapi.h).
 *
 * Nothing is touched until the first use - an app opening the GPU, or
 * `gpu3d test`: the display driver (kernel/gpu_intel.c) only says what is
 * there.
 */

typedef struct {
    volatile uint8_t* mmio;     /* the registers (BAR 0): the GTT at + 2 MiB */
    uint32_t mmio_size;
    uint16_t devid;
    uint8_t  revision;
    int      gen;               /* 5, 6, 7 */
    int      hsw;               /* gen 7.5 */
    uint32_t gtt_entries;       /* of the global GTT */
    uint32_t reserved_lo;       /* entries at the bottom the firmware's framebuffer keeps */
    uint32_t reserved_hi;       /* entries at the top the display driver keeps (the cursor) */
} i915_hw_t;

/* kernel/gpu_intel.c, at probe (nothing is touched) */
void i915_attach(const i915_hw_t* hw);
int  i915_present(void);                /* a GPU this driver knows */
int  i915_running(void);                /* its render ring is up */
int  i915_start(void);                  /* brings the render ring up (once): 0, -1 */
const char* i915_why(void);             /* why it is not running */
const i915_hw_t* i915_hw(void);

/* the global GTT: pages of the GPU's address space */
int64_t  i915_gtt_alloc(uint32_t pages, uint32_t align_pages);   /* the first page, or -1 */
void     i915_gtt_free(uint32_t first, uint32_t pages);
void     i915_gtt_bind(uint32_t first, const uint64_t* phys, uint32_t pages);
void     i915_gtt_unbind(uint32_t first, uint32_t pages);
uint64_t i915_gtt_size(void);           /* bytes of address space */
uint64_t i915_gtt_free_bytes(void);     /* bytes of it free */
int      i915_llc(void);                /* the CPU's last-level cache is shared with the GPU */
uint32_t i915_swizzle_x(void);          /* I915_BIT_6_SWIZZLE_* of X-tiled surfaces (Y: _9 or NONE) */

/* the render ring: a batch at gtt (in the GPU's address space) runs; its
 * number (a seqno), or 0 if the GPU is hung and could not be reset */
uint32_t i915_submit(uint32_t batch_gtt);
uint32_t i915_submit_privileged(uint32_t batch_gtt);     /* (the kernel's own tests) */
/* hardware contexts (gen 6 - 7.5; none on Ironlake): an image the GPU's
 * state is saved into and restored from when batches of different
 * contexts follow each other. i915_ctx_create: its number (> 0), or -1
 * (none: no contexts here, or no memory); destroy: freed once the GPU is
 * done with it; submit_ctx: a batch in context ctx (0: the kernel's) */
int      i915_has_contexts(void);
int      i915_ctx_create(void);
void     i915_ctx_destroy(int ctx);
uint32_t i915_submit_ctx(int ctx, uint32_t batch_gtt);
int      i915_done(uint32_t seqno);     /* that one has finished (0: nothing to wait for) */
/* waits until it has (timeout_ms < 0: for ever): 0, -1 timed out, -2 the GPU hung (reset) */
int      i915_wait(uint32_t seqno, int timeout_ms);
uint32_t i915_last_submitted(void);
uint32_t i915_resets(void);             /* hangs the GPU was reset from */
uint64_t i915_timestamp(void);          /* the render engine's clock (12.5 MHz) */
void     i915_info(char* buf, int cap);
/* a hang reset: every context with work in flight is told (kernel/gpu_gem.c) */
void     i915_on_reset(void (*fn)(uint32_t done, uint32_t submitted));

/* kernel/gpu_gem.c: the i915 interface for apps (owner: the app's id + 1) */
int  gem_ioctl(int owner, unsigned long req, void* arg);
void gem_release(int owner);
int  gem_enabled(void);                 /* apps may use it (gpu3d on: a setting) */
void gem_set_enabled(int on);            /* the app ended: its buffers, contexts, fences */
void gem_stats(char* buf, int cap);
/* `gpu3d test`: checks on the real GPU, reported one by one */
void i915_tests(void (*report)(const char* name, int ok, const char* detail));

#endif
