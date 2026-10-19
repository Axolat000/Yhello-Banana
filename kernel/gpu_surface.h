/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPU_SURFACE_H
#define GPU_SURFACE_H
#include "types.h"

/* A bounded XRGB surface. Stride is in pixels, including row padding. */
typedef struct { uint32_t* pixels; int width, height, stride; } gpu_surface_t;
typedef struct { int x, y, w, h; } gpu_rect_t;
int gpu_rect_clip(gpu_rect_t* r, int width, int height);
uint64_t gpu_surface_copy(gpu_surface_t dst, gpu_surface_t src, gpu_rect_t r);
/* Exact comparisons against ordinary RAM, never reads from video memory.
 * The callback receives changed horizontal runs of 32x32 tiles. */
uint64_t gpu_surface_damage(gpu_surface_t src, gpu_surface_t previous, int valid,
                           void (*changed)(gpu_rect_t, void*), void* context);
void gpu_surface_tests(void (*report)(const char*, int, const char*));
#endif
