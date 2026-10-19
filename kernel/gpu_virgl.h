/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef GPU_VIRGL_H
#define GPU_VIRGL_H
#include "types.h"
#include "../sdk/include/banana_gpu.h"
int gpu_virtio_command(void* request, unsigned length, void* response, unsigned capacity);
int gpu_virtio_alive(void);
void gpu_virgl_init(unsigned capsets, int context_init);
int gpu_virgl_available(void);
int gpu_virgl_call(int owner, unsigned op, const void* request, unsigned request_size, void* data, unsigned size);
void gpu_virgl_release(int owner);
void gpu_virgl_diagnostics(void);
void gpu_virgl_tests(void (*report)(const char*, int, const char*));
#endif
