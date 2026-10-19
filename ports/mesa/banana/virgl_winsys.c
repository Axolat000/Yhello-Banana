/* SPDX-License-Identifier: MIT
 * Banana OS Gallium winsys: owned kernel resources, fenced virtio commands.
 * OSMesa reads the finished host-rendered color buffer back for the desktop.
 */
struct pipe_screen;
struct pipe_screen* banana_virgl_create_screen(void);
#ifdef __BANANA_OS__
#include <banana.h>
#include <banana_gpu.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "util/format/u_format.h"
#include "virgl/virgl_public.h"
#include "virgl/virgl_winsys.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "util/u_memory.h"

struct virgl_hw_res {
    struct pipe_reference reference;
    unsigned id, bytes, references;
    void* memory;
};
struct banana_ws { struct virgl_winsys base; unsigned transfer_context; int failed; };
struct banana_cmd {
    struct virgl_cmd_buf base;
    struct banana_ws* ws;
    unsigned context, count;
    struct virgl_hw_res* resources[256];
};
struct pipe_fence_handle { struct pipe_reference reference; };
static int call(unsigned op, banana_gpu_request_t* request, void* data, unsigned size) {
    if (!__banana || !BANANA_HAS(__banana, gpu_call) || !__banana->gpu_call) return -1;
    return __banana->gpu_call(op, request, sizeof(*request), data, size);
}
static void resource_reference(struct virgl_winsys* ws, struct virgl_hw_res** dst, struct virgl_hw_res* src) {
    struct virgl_hw_res* old = *dst;
    if (pipe_reference(old ? &old->reference : NULL, src ? &src->reference : NULL)) {
        banana_gpu_request_t q = {.resource = old->id};
        call(BGPU_RESOURCE_DESTROY, &q, NULL, 0); free(old->memory); free(old);
    }
    *dst = src; (void)ws;
}
static struct virgl_hw_res* resource_create(struct virgl_winsys* vws, enum pipe_texture_target target,
    const void* front, uint32_t format, uint32_t bind, uint32_t width, uint32_t height, uint32_t depth,
    uint32_t array, uint32_t level, uint32_t samples, uint32_t flags, uint32_t size) {
    (void)front;
    struct banana_ws* ws = (struct banana_ws*)vws;
    banana_gpu_request_t q = {.p = {target, pipe_to_virgl_format(format), bind, width, height, depth, array, level, samples, flags, size}};
    struct virgl_hw_res* res = calloc(1, sizeof(*res));
    if (!res) return NULL;
    res->memory = calloc(1, size ? size : 1);
    if (!res->memory) { free(res); return NULL; }
    int id = call(BGPU_RESOURCE_CREATE, &q, NULL, 0);
    if (id < 0) { free(res->memory); free(res); return NULL; }
    res->id = id; res->bytes = size;
    pipe_reference_init(&res->reference, 1);
    q.resource = id; q.context = ws->transfer_context;
    if (call(BGPU_ATTACH, &q, NULL, 0)) { resource_reference(vws, &res, NULL); return NULL; }
    return res;
}
static void* resource_map(struct virgl_winsys* ws, struct virgl_hw_res* res) { (void)ws; return res->memory; }
static void resource_wait(struct virgl_winsys* ws, struct virgl_hw_res* res) { (void)ws; (void)res; }
static bool resource_busy(struct virgl_winsys* ws, struct virgl_hw_res* res) { (void)ws; (void)res; return false; }
static uint32_t resource_size(struct virgl_winsys* ws, struct virgl_hw_res* res) { (void)ws; return res->bytes; }
static int transfer(struct virgl_winsys* vws, struct virgl_hw_res* res, const struct pipe_box* box,
    uint32_t stride, uint32_t layer, uint32_t offset, uint32_t level, bool get) {
    struct banana_ws* ws = (struct banana_ws*)vws;
    banana_gpu_request_t q = {.context = ws->transfer_context, .resource = res->id};
    int rc = 0;
    if (!get) rc = call(BGPU_WRITE, &q, res->memory, res->bytes);
    q.offset = offset;
    q.p[0] = box->x; q.p[1] = box->y; q.p[2] = box->z;
    q.p[3] = box->width; q.p[4] = box->height; q.p[5] = box->depth;
    q.p[6] = level; q.p[7] = stride; q.p[8] = layer;
    if (!rc) rc = call(get ? BGPU_TRANSFER_GET : BGPU_TRANSFER_PUT, &q, NULL, 0);
    q.offset = 0;
    if (!rc && get) rc = call(BGPU_READ, &q, res->memory, res->bytes);
    if (rc) { ws->failed = 1; fprintf(stderr, "Banana virgl: resource transfer failed\n"); }
    return rc;
}
static int transfer_put(struct virgl_winsys* ws, struct virgl_hw_res* r, const struct pipe_box* b,
    uint32_t stride, uint32_t layer, uint32_t offset, uint32_t level) { return transfer(ws, r, b, stride, layer, offset, level, false); }
static int transfer_get(struct virgl_winsys* ws, struct virgl_hw_res* r, const struct pipe_box* b,
    uint32_t stride, uint32_t layer, uint32_t offset, uint32_t level) { return transfer(ws, r, b, stride, layer, offset, level, true); }
static void release_references(struct banana_cmd* cmd) {
    for (unsigned i = 0; i < cmd->count; i++) {
        p_atomic_dec(&cmd->resources[i]->references);
        resource_reference(&cmd->ws->base, &cmd->resources[i], NULL);
    }
    cmd->count = 0;
}
static struct virgl_cmd_buf* cmd_create(struct virgl_winsys* ws, uint32_t size) {
    struct banana_cmd* cmd = calloc(1, sizeof(*cmd));
    if (!cmd) return NULL;
    cmd->base.buf = calloc(size, 4);
    banana_gpu_request_t q = {0};
    int ctx = call(BGPU_CONTEXT_CREATE, &q, NULL, 0);
    if (!cmd->base.buf || ctx < 0) {
        if (ctx > 0) { q.context = ctx; call(BGPU_CONTEXT_DESTROY, &q, NULL, 0); }
        free(cmd->base.buf); free(cmd); return NULL;
    }
    cmd->ws = (struct banana_ws*)ws; cmd->context = ctx;
    return &cmd->base;
}
static void cmd_destroy(struct virgl_cmd_buf* base) {
    struct banana_cmd* cmd = (struct banana_cmd*)base;
    release_references(cmd);
    banana_gpu_request_t q = {.context = cmd->context};
    call(BGPU_CONTEXT_DESTROY, &q, NULL, 0); free(base->buf); free(cmd);
}
static void emit_res(struct virgl_winsys* vws, struct virgl_cmd_buf* base, struct virgl_hw_res* res, bool write) {
    struct banana_cmd* cmd = (struct banana_cmd*)base;
    if (write) base->buf[base->cdw++] = res->id;
    for (unsigned i = 0; i < cmd->count; i++) if (cmd->resources[i] == res) return;
    if (cmd->count == 256) { cmd->ws->failed = 1; return; }
    banana_gpu_request_t q = {.context = cmd->context, .resource = res->id};
    if (call(BGPU_ATTACH, &q, NULL, 0)) { cmd->ws->failed = 1; return; }
    resource_reference(vws, &cmd->resources[cmd->count++], res); p_atomic_inc(&res->references);
}
static bool res_referenced(struct virgl_winsys* ws, struct virgl_cmd_buf* cmd, struct virgl_hw_res* res) {
    (void)ws; (void)cmd; return p_atomic_read(&res->references) != 0;
}
static struct pipe_fence_handle* fence_create(struct virgl_winsys* ws, int fd) {
    (void)ws; if (fd >= 0) return NULL;
    struct pipe_fence_handle* f = calloc(1, sizeof(*f));
    if (f) pipe_reference_init(&f->reference, 1);
    return f;
}
static bool fence_wait(struct virgl_winsys* ws, struct pipe_fence_handle* f, uint64_t timeout) {
    (void)f; (void)timeout; return !((struct banana_ws*)ws)->failed;
}
static void fence_reference(struct virgl_winsys* ws, struct pipe_fence_handle** dst, struct pipe_fence_handle* src) {
    (void)ws; struct pipe_fence_handle* old = *dst;
    if (pipe_reference(old ? &old->reference : NULL, src ? &src->reference : NULL)) free(old);
    *dst = src;
}
static int submit_cmd(struct virgl_winsys* vws, struct virgl_cmd_buf* base, struct pipe_fence_handle** fence) {
    struct banana_cmd* cmd = (struct banana_cmd*)base;
    banana_gpu_request_t q = {.context = cmd->context};
    int rc = cmd->ws->failed ? -1 : base->cdw ? call(BGPU_SUBMIT, &q, base->buf, base->cdw * 4) : 0;
    if (rc) { cmd->ws->failed = 1; fprintf(stderr, "Banana virgl: command submission failed\n"); }
    if (fence && !rc) {
        struct pipe_fence_handle* f = fence_create(vws, -1);
        fence_reference(vws, fence, f); fence_reference(vws, &f, NULL);
    }
    release_references(cmd); base->cdw = 0; return rc;
}
static int get_caps(struct virgl_winsys* ws, struct virgl_drm_caps* caps) {
    (void)ws;
    virgl_ws_fill_new_caps_defaults(caps);
    banana_gpu_request_t q = {0};
    if (call(BGPU_CAPS, &q, &caps->caps, sizeof(caps->caps)) < 0) return -1;
    /* No encoded transfers, coherent mappings, resource blobs or imported
     * host FDs. Keep resource backing complete for the ordinary copy path. */
    caps->caps.v2.num_video_caps = 0;
    if (caps->caps.v2.max_texture_2d_size > 8192) caps->caps.v2.max_texture_2d_size = 8192;
    if (caps->caps.v2.max_texture_cube_size > 8192) caps->caps.v2.max_texture_cube_size = 8192;
    if (caps->caps.v2.max_texture_3d_size > 2048) caps->caps.v2.max_texture_3d_size = 2048;
    if (caps->caps.v1.max_texture_array_layers > 2048) caps->caps.v1.max_texture_array_layers = 2048;
    caps->caps.v2.capability_bits &= ~VIRGL_CAP_COPY_TRANSFER;
    caps->caps.v2.capability_bits_v2 &= ~(VIRGL_CAP_V2_UNTYPED_RESOURCE | VIRGL_CAP_V2_VIDEO_MEMORY | VIRGL_CAP_V2_COPY_TRANSFER_BOTH_DIRECTIONS);
    return 0;
}
static void flush_frontbuffer(struct virgl_winsys* ws, struct virgl_cmd_buf* cmd, struct virgl_hw_res* res,
    unsigned level, unsigned layer, void* drawable, struct pipe_box* box) {
    (void)ws; (void)cmd; (void)res; (void)level; (void)layer; (void)drawable; (void)box;
}
static void destroy(struct virgl_winsys* base) {
    struct banana_ws* ws = (struct banana_ws*)base;
    banana_gpu_request_t q = {.context = ws->transfer_context};
    call(BGPU_CONTEXT_DESTROY, &q, NULL, 0); free(ws);
}
struct pipe_screen* banana_virgl_create_screen(void) {
    banana_gpu_request_t q = {0}; banana_gpu_info_t info;
    int rc = call(BGPU_INFO, &q, &info, sizeof(info));
    if (rc) { fprintf(stderr, "Banana virgl: info failed %d (API %u, %u bytes, need %u)\n", rc,
        __banana ? __banana->version : 0, __banana ? __banana->size : 0, (unsigned)sizeof(*__banana)); return NULL; }
    struct banana_ws* ws = calloc(1, sizeof(*ws));
    if (!ws) return NULL;
    int ctx = call(BGPU_CONTEXT_CREATE, &q, NULL, 0);
    if (ctx < 0) { fprintf(stderr, "Banana virgl: context create failed %d\n", ctx); free(ws); return NULL; }
    ws->transfer_context = ctx;
    ws->base.destroy = destroy;
    ws->base.transfer_put = transfer_put; ws->base.transfer_get = transfer_get;
    ws->base.resource_create = resource_create; ws->base.resource_reference = resource_reference;
    ws->base.resource_map = resource_map; ws->base.resource_wait = resource_wait;
    ws->base.resource_is_busy = resource_busy; ws->base.resource_get_storage_size = resource_size;
    ws->base.cmd_buf_create = cmd_create; ws->base.cmd_buf_destroy = cmd_destroy;
    ws->base.emit_res = emit_res; ws->base.res_is_referenced = res_referenced;
    ws->base.submit_cmd = submit_cmd; ws->base.get_caps = get_caps;
    ws->base.cs_create_fence = fence_create; ws->base.fence_wait = fence_wait; ws->base.fence_reference = fence_reference;
    ws->base.flush_frontbuffer = flush_frontbuffer;
    struct pipe_screen* screen = virgl_create_screen(&ws->base, NULL);
    if (!screen) destroy(&ws->base);
    return screen;
}
#else
struct pipe_screen;
struct pipe_screen* banana_virgl_create_screen(void) { return 0; }
#endif
