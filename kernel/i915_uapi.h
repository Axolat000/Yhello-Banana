/* SPDX-License-Identifier: MIT */
/* The part of Linux's DRM / i915 interface (include/uapi/drm/drm.h and
 * i915_drm.h) Banana OS implements for Mesa's Intel drivers: the same
 * request numbers and the same structures, byte for byte, so Mesa talks to
 * it as it does to Linux. The structures are from those headers:
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef I915_UAPI_H
#define I915_UAPI_H
#include "types.h"

/* a request: _IOC(dir, 'd', nr, size) */
#define DRM_NR(req)    ((unsigned)(req) & 0xFF)
#define DRM_TYPE(req)  (((unsigned)(req) >> 8) & 0xFF)
#define DRM_SIZE(req)  (((unsigned)(req) >> 16) & 0x3FFF)
#define DRM_COMMAND_BASE 0x40

/* DRM core */
#define DRM_NR_VERSION          0x00
#define DRM_NR_GET_MAGIC        0x02
#define DRM_NR_GEM_CLOSE        0x09
#define DRM_NR_GEM_FLINK        0x0A
#define DRM_NR_GEM_OPEN         0x0B
#define DRM_NR_GET_CAP          0x0C
#define DRM_NR_SET_CLIENT_CAP   0x0D
#define DRM_NR_PRIME_TO_FD      0x2D
#define DRM_NR_PRIME_TO_HANDLE  0x2E
#define DRM_NR_SYNCOBJ_CREATE   0xBF
#define DRM_NR_SYNCOBJ_DESTROY  0xC0
#define DRM_NR_SYNCOBJ_TO_FD    0xC1
#define DRM_NR_SYNCOBJ_FROM_FD  0xC2
#define DRM_NR_SYNCOBJ_WAIT     0xC3
#define DRM_NR_SYNCOBJ_RESET    0xC4
#define DRM_NR_SYNCOBJ_SIGNAL   0xC5

/* i915 (DRM_COMMAND_BASE + these) */
#define I915_NR_GETPARAM          0x06
#define I915_NR_SETPARAM          0x07
#define I915_NR_GEM_BUSY          0x17
#define I915_NR_GEM_THROTTLE      0x18
#define I915_NR_GEM_CREATE        0x1B
#define I915_NR_GEM_PREAD         0x1C
#define I915_NR_GEM_PWRITE        0x1D
#define I915_NR_GEM_MMAP          0x1E
#define I915_NR_GEM_SET_DOMAIN    0x1F
#define I915_NR_GEM_SW_FINISH     0x20
#define I915_NR_GEM_SET_TILING    0x21
#define I915_NR_GEM_GET_TILING    0x22
#define I915_NR_GEM_GET_APERTURE  0x23
#define I915_NR_GEM_MMAP_GTT      0x24      /* (and MMAP_OFFSET: the same number) */
#define I915_NR_GEM_MADVISE       0x26
#define I915_NR_GEM_EXECBUFFER2   0x29
#define I915_NR_GEM_WAIT          0x2C
#define I915_NR_CONTEXT_CREATE    0x2D
#define I915_NR_CONTEXT_DESTROY   0x2E
#define I915_NR_GEM_SET_CACHING   0x2F
#define I915_NR_GEM_GET_CACHING   0x30
#define I915_NR_REG_READ          0x31
#define I915_NR_GET_RESET_STATS   0x32
#define I915_NR_GEM_USERPTR       0x33
#define I915_NR_CONTEXT_GETPARAM  0x34
#define I915_NR_CONTEXT_SETPARAM  0x35
#define I915_NR_QUERY             0x39
#define I915_NR_GEM_VM_CREATE     0x3A
#define I915_NR_GEM_CREATE_EXT    0x3C

struct drm_version {
    int32_t  version_major, version_minor, version_patchlevel;
    uint64_t name_len;  uint64_t name;
    uint64_t date_len;  uint64_t date;
    uint64_t desc_len;  uint64_t desc;
};
struct drm_gem_close { uint32_t handle, pad; };
struct drm_get_cap { uint64_t capability, value; };
#define DRM_CAP_SYNCOBJ 0x13
struct drm_syncobj_create { uint32_t handle, flags; };
#define DRM_SYNCOBJ_CREATE_SIGNALED 1
struct drm_syncobj_destroy { uint32_t handle, pad; };
struct drm_syncobj_wait {
    uint64_t handles;
    int64_t  timeout_nsec;                 /* absolute: CLOCK_MONOTONIC */
    uint32_t count_handles, flags, first_signaled, pad;
};
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL        1
#define DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT 2
struct drm_syncobj_array { uint64_t handles; uint32_t count_handles, pad; };

struct drm_i915_getparam { int32_t param; uint32_t pad; uint64_t value; };   /* (int __user *value) */
#define I915_PARAM_CHIPSET_ID            4
#define I915_PARAM_HAS_GEM               5
#define I915_PARAM_NUM_FENCES_AVAIL      6
#define I915_PARAM_HAS_EXECBUF2          9
#define I915_PARAM_HAS_BSD              10
#define I915_PARAM_HAS_BLT              11
#define I915_PARAM_HAS_RELAXED_FENCING  12
#define I915_PARAM_HAS_COHERENT_RINGS   13
#define I915_PARAM_HAS_RELAXED_DELTA    15
#define I915_PARAM_HAS_GEN7_SOL_RESET   16
#define I915_PARAM_HAS_LLC              17
#define I915_PARAM_HAS_ALIASING_PPGTT   18
#define I915_PARAM_HAS_WAIT_TIMEOUT     19
#define I915_PARAM_HAS_SEMAPHORES       20
#define I915_PARAM_HAS_VEBOX            22
#define I915_PARAM_HAS_SECURE_BATCHES   23
#define I915_PARAM_HAS_PINNED_BATCHES   24
#define I915_PARAM_HAS_EXEC_NO_RELOC    25
#define I915_PARAM_HAS_EXEC_HANDLE_LUT  26
#define I915_PARAM_HAS_WT               27
#define I915_PARAM_CMD_PARSER_VERSION   28
#define I915_PARAM_MMAP_VERSION         30
#define I915_PARAM_HAS_BSD2             31
#define I915_PARAM_REVISION             32
#define I915_PARAM_HAS_GPU_RESET        35
#define I915_PARAM_HAS_EXEC_SOFTPIN     37
#define I915_PARAM_MMAP_GTT_VERSION     40
#define I915_PARAM_HAS_SCHEDULER        41
#define I915_PARAM_HAS_EXEC_ASYNC       43
#define I915_PARAM_HAS_EXEC_FENCE       44
#define I915_PARAM_HAS_EXEC_CAPTURE     45
#define I915_PARAM_HAS_EXEC_BATCH_FIRST 48
#define I915_PARAM_HAS_EXEC_FENCE_ARRAY 49
#define I915_PARAM_HAS_CONTEXT_ISOLATION 50
#define I915_PARAM_CS_TIMESTAMP_FREQUENCY 51

struct drm_i915_gem_create { uint64_t size; uint32_t handle, pad; };
struct drm_i915_gem_pread { uint32_t handle, pad; uint64_t offset, size, data_ptr; };
struct drm_i915_gem_mmap { uint32_t handle, pad; uint64_t offset, size, addr_ptr, flags; };
#define I915_MMAP_WC 1
struct drm_i915_gem_set_domain { uint32_t handle, read_domains, write_domain; };
#define I915_GEM_DOMAIN_CPU 1
#define I915_GEM_DOMAIN_GTT 0x40
#define I915_GEM_DOMAIN_WC  0x80
struct drm_i915_gem_relocation_entry {
    uint32_t target_handle, delta;
    uint64_t offset, presumed_offset;
    uint32_t read_domains, write_domain;
};
struct drm_i915_gem_exec_object2 {
    uint32_t handle, relocation_count;
    uint64_t relocs_ptr, alignment, offset, flags, rsvd1, rsvd2;
};
#define EXEC_OBJECT_NEEDS_FENCE  (1u << 0)
#define EXEC_OBJECT_NEEDS_GTT    (1u << 1)
#define EXEC_OBJECT_WRITE        (1u << 2)
#define EXEC_OBJECT_PINNED       (1u << 4)
struct drm_i915_gem_exec_fence { uint32_t handle, flags; };
#define I915_EXEC_FENCE_WAIT   1
#define I915_EXEC_FENCE_SIGNAL 2
struct drm_i915_gem_execbuffer2 {
    uint64_t buffers_ptr;
    uint32_t buffer_count, batch_start_offset, batch_len, DR1, DR4, num_cliprects;
    uint64_t cliprects_ptr, flags, rsvd1, rsvd2;
};
#define I915_EXEC_RING_MASK     0x3F
#define I915_EXEC_DEFAULT       0
#define I915_EXEC_RENDER        1
#define I915_EXEC_SECURE        (1u << 9)
#define I915_EXEC_IS_PINNED     (1u << 10)
#define I915_EXEC_NO_RELOC      (1u << 11)
#define I915_EXEC_HANDLE_LUT    (1u << 12)
#define I915_EXEC_FENCE_IN      (1u << 16)
#define I915_EXEC_FENCE_OUT     (1u << 17)
#define I915_EXEC_BATCH_FIRST   (1u << 18)
#define I915_EXEC_FENCE_ARRAY   (1u << 19)
#define I915_EXEC_FENCE_SUBMIT  (1u << 20)
#define I915_EXEC_USE_EXTENSIONS (1u << 21)
struct drm_i915_gem_busy { uint32_t handle, busy; };
struct drm_i915_gem_caching { uint32_t handle, caching; };
struct drm_i915_gem_set_tiling { uint32_t handle, tiling_mode, stride, swizzle_mode; };
struct drm_i915_gem_get_tiling { uint32_t handle, tiling_mode, swizzle_mode, phys_swizzle_mode; };
#define I915_TILING_NONE 0
#define I915_TILING_X    1
#define I915_TILING_Y    2
#define I915_BIT_6_SWIZZLE_NONE 0
#define I915_BIT_6_SWIZZLE_9    1
#define I915_BIT_6_SWIZZLE_9_10 2
struct drm_i915_gem_get_aperture { uint64_t aper_size, aper_available_size; };
struct drm_i915_gem_madvise { uint32_t handle, madv, retained; };
struct drm_i915_gem_wait { uint32_t bo_handle, flags; int64_t timeout_ns; };
struct drm_i915_gem_context_create_ext { uint32_t ctx_id, flags; uint64_t extensions; };
struct drm_i915_gem_context_destroy { uint32_t ctx_id, pad; };
struct drm_i915_reg_read { uint64_t offset, val; };
#define I915_REG_READ_8B_WA 1
struct drm_i915_reset_stats { uint32_t ctx_id, flags, reset_count, batch_active, batch_pending, pad; };
struct drm_i915_gem_context_param { uint32_t ctx_id, size; uint64_t param, value; };
#define I915_CONTEXT_PARAM_BAN_PERIOD   1
#define I915_CONTEXT_PARAM_GTT_SIZE     3
#define I915_CONTEXT_PARAM_NO_ERROR_CAPTURE 4
#define I915_CONTEXT_PARAM_BANNABLE     5
#define I915_CONTEXT_PARAM_PRIORITY     6
#define I915_CONTEXT_PARAM_RECOVERABLE  8
struct drm_i915_query { uint32_t num_items, flags; uint64_t items_ptr; };
struct drm_i915_query_item { uint64_t query_id; int32_t length; uint32_t flags; uint64_t data_ptr; };

#endif
