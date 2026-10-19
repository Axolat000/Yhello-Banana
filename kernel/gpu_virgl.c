/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gpu_virgl.h"
#include "gpu.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"
#include "terminal.h"

#define CONTEXTS 32
#define RESOURCES 256
#define RESOURCE_LIMIT (64u << 20)
#define TOTAL_LIMIT (128u << 20)
#define SUBMIT_LIMIT (512u << 10)
typedef struct __attribute__((packed)) { uint32_t type, flags; uint64_t fence; uint32_t ctx, pad; } header_t;
typedef struct { int owner; uint32_t id; } context_t;
typedef struct { int owner; uint32_t id, bytes, attached; void* memory; uint32_t width, height, depth, levels; } resource_t;
static context_t contexts[CONTEXTS];
static resource_t resources[RESOURCES];
static uint32_t next_context = 1, next_resource = 16, memory_used;
static banana_gpu_info_t info;
static uint8_t caps[4096];
static int has_context_init;

static int command(void* req, unsigned n) {
    header_t response;
    return gpu_virtio_command(req, n, &response, sizeof(response));
}
int gpu_virgl_available(void) { return info.cap_bytes && gpu_virtio_alive(); }

void gpu_virgl_init(unsigned count, int context_init) {
    has_context_init = context_init;
    if (count > 16) count = 16;
    for (unsigned i = 0; i < count; i++) {
        struct { header_t h; uint32_t index, pad; } req = {{.type = 0x108}, i, 0};
        struct { header_t h; uint32_t id, version, size, pad; } reply;
        if (gpu_virtio_command(&req, sizeof(req), &reply, sizeof(reply))) continue;
        if (!reply.id || reply.id > 2 || !reply.size || reply.size > sizeof(caps) || reply.id < info.capset) continue;
        struct { header_t h; uint32_t id, version; } get = {{.type = 0x109}, reply.id, reply.version > 2 ? 2 : reply.version};
        uint8_t response[sizeof(header_t) + sizeof(caps)];
        if (gpu_virtio_command(&get, sizeof(get), response, sizeof(header_t) + reply.size)) continue;
        memcpy(caps, response + sizeof(header_t), reply.size);
        info = (banana_gpu_info_t){1, reply.id, get.version, reply.size, RESOURCE_LIMIT, SUBMIT_LIMIT, {0, 0}};
    }
    if (info.cap_bytes) klog("gpu: virgl capset %u version %u (%u bytes), optional application 3D transport\n", info.capset, info.cap_version, info.cap_bytes);
}

static int context_index(int owner, uint32_t id) {
    for (int i = 0; i < CONTEXTS; i++) if (contexts[i].id == id && contexts[i].owner == owner && id) return i;
    return -1;
}
static resource_t* resource(int owner, uint32_t id) {
    for (int i = 0; i < RESOURCES; i++) if (resources[i].id == id && resources[i].owner == owner && id) return &resources[i];
    return NULL;
}
static int attach(resource_t* r, int c, int remove) {
    struct { header_t h; uint32_t resource, pad; } req = {{.type = remove ? 0x203 : 0x202, .ctx = contexts[c].id}, r->id, 0};
    if (command(&req, sizeof(req))) return -1;
    if (remove) r->attached &= ~(1u << c); else r->attached |= 1u << c;
    return 0;
}
static int destroy_resource(resource_t* r) {
    for (int c = 0; c < CONTEXTS; c++) if ((r->attached >> c) & 1) if (attach(r, c, 1)) return -1;
    struct { header_t h; uint32_t id, pad; } req = {{.type = 0x107}, r->id, 0};
    if (command(&req, sizeof(req))) return -1;
    req.h.type = 0x102;
    if (command(&req, sizeof(req))) return -1;
    kfree(r->memory); memory_used -= r->bytes; memset(r, 0, sizeof(*r)); return 0;
}

int gpu_virgl_call(int owner, unsigned op, const void* request, unsigned request_size, void* data, unsigned size) {
    if (!gpu_virgl_available() || owner <= 0 || request_size != sizeof(banana_gpu_request_t) || !request || (!data && size)) return -1;
    banana_gpu_request_t q; memcpy(&q, request, sizeof(q));
    if (op == BGPU_INFO) { if (size != sizeof(info)) return -1; memcpy(data, &info, size); return 0; }
    if (op == BGPU_CAPS) {
        if (size > sizeof(caps)) return -1;
        unsigned n = size < info.cap_bytes ? size : info.cap_bytes;
        memcpy(data, caps, n); return (int)n;
    }
    if (op == BGPU_CONTEXT_CREATE) {
        int c; for (c = 0; c < CONTEXTS; c++) if (!contexts[c].id) break;
        if (c == CONTEXTS || next_context >= 0x7fffffffu) return -1;
        struct { header_t h; uint32_t len, init; char name[64]; } req = {{.type = 0x200, .ctx = next_context}, 9, 0, "banana-os"};
        if (has_context_init) req.init = info.capset;
        if (command(&req, sizeof(req))) return -1;
        contexts[c] = (context_t){owner, next_context++}; return (int)contexts[c].id;
    }
    int c = context_index(owner, q.context);
    resource_t* r = resource(owner, q.resource);
    if (op == BGPU_CONTEXT_DESTROY) {
        if (c < 0) return -1;
        for (int i = 0; i < RESOURCES; i++) if ((resources[i].attached >> c) & 1) if (attach(&resources[i], c, 1)) return -1;
        header_t req = {.type = 0x201, .ctx = q.context};
        if (command(&req, sizeof(req))) return -1;
        memset(&contexts[c], 0, sizeof(contexts[c])); return 0;
    }
    if (op == BGPU_RESOURCE_CREATE) {
        unsigned bytes = q.p[10];
        if (!bytes || bytes > RESOURCE_LIMIT || bytes > TOTAL_LIMIT - memory_used || next_resource >= 0x7fffffffu ||
            q.p[0] > 8 || !q.p[3] || q.p[3] > (q.p[0] == 0 ? RESOURCE_LIMIT : 8192) || !q.p[4] || q.p[4] > 8192 || !q.p[5] || q.p[5] > 2048 ||
            q.p[6] > 2048 || q.p[7] > 16 || q.p[8] > 16) return -1;
        int i; for (i = 0; i < RESOURCES; i++) if (!resources[i].id) break;
        if (i == RESOURCES) return -1;
        void* mem = kmalloc_aligned(bytes, 4096);
        if (!mem) return -1;
        memset(mem, 0, bytes);
        uint32_t id = next_resource++;
        struct { header_t h; uint32_t p[12]; } req = {.h = {.type = 0x204}};
        req.p[0] = id; memcpy(req.p + 1, q.p, 10 * 4);
        resources[i] = (resource_t){owner, id, bytes, 0, mem, q.p[3], q.p[4], q.p[5] > q.p[6] ? q.p[5] : q.p[6], q.p[7]}; memory_used += bytes;
        if (command(&req, sizeof(req))) { kfree(mem); memory_used -= bytes; memset(&resources[i], 0, sizeof(resources[i])); return -1; }
        struct __attribute__((packed)) { header_t h; uint32_t id, n; uint64_t address; uint32_t bytes, pad; } backing = {{.type = 0x106}, id, 1, (uintptr_t)mem, bytes, 0};
        if (command(&backing, sizeof(backing))) {
            struct { header_t h; uint32_t id, pad; } drop = {{.type = 0x102}, id, 0};
            if (!command(&drop, sizeof(drop))) { kfree(mem); memory_used -= bytes; memset(&resources[i], 0, sizeof(resources[i])); }
            return -1;
        }
        return (int)id;
    }
    if (op == BGPU_RESOURCE_DESTROY) return r ? destroy_resource(r) : -1;
    if (op == BGPU_ATTACH) {
        if (!r || c < 0) return -1;
        return (r->attached & (1u << c)) ? 0 : attach(r, c, 0);
    }
    if (op == BGPU_WRITE || op == BGPU_READ) {
        if (!r || q.offset > r->bytes || size > r->bytes - q.offset) return -1;
        if (op == BGPU_WRITE) memcpy((uint8_t*)r->memory + q.offset, data, size);
        else memcpy(data, (uint8_t*)r->memory + q.offset, size);
        return 0;
    }
    if (op == BGPU_SUBMIT) {
        if (c < 0 || !size || size > SUBMIT_LIMIT || (size & 3)) return -1;
        struct { header_t h; uint32_t bytes, pad; }* req = kmalloc(sizeof(*req) + size);
        if (!req) return -1;
        *req = (typeof(*req)){.h = {.type = 0x207, .ctx = q.context}, .bytes = size};
        memcpy(req + 1, data, size);
        int rc = command(req, sizeof(*req) + size);
        /* The transport retains its own DMA copy on an unacknowledged reset. */
        kfree(req);
        return rc;
    }
    if (op == BGPU_TRANSFER_PUT || op == BGPU_TRANSFER_GET) {
        if (!r || c < 0 || !(r->attached & (1u << c)) || q.offset >= r->bytes || q.p[6] > r->levels ||
            !q.p[3] || !q.p[4] || !q.p[5]) return -1;
        uint32_t w = r->width >> q.p[6], h = r->height >> q.p[6];
        if (!w) w = 1;
        if (!h) h = 1;
        if (q.p[3] > w || q.p[0] > w - q.p[3] || q.p[4] > h || q.p[1] > h - q.p[4] ||
            q.p[5] > r->depth || q.p[2] > r->depth - q.p[5] || q.p[7] > r->bytes || q.p[8] > r->bytes) return -1;
        struct { header_t h; uint32_t box[6]; uint64_t offset; uint32_t id, level, stride, layer; } req =
            {.h = {.type = op == BGPU_TRANSFER_PUT ? 0x205 : 0x206, .ctx = q.context}, .offset = q.offset,
             .id = q.resource, .level = q.p[6], .stride = q.p[7], .layer = q.p[8]};
        memcpy(req.box, q.p, 24);
        return command(&req, sizeof(req));
    }
    return -1;
}

void gpu_virgl_release(int owner) {
    if (!gpu_virgl_available()) return; /* retained storage is quarantined after device failure */
    for (int i = 0; i < RESOURCES; i++) if (resources[i].id && resources[i].owner == owner) destroy_resource(&resources[i]);
    for (int i = 0; i < CONTEXTS; i++) if (contexts[i].id && contexts[i].owner == owner) {
        banana_gpu_request_t q = {.context = contexts[i].id};
        gpu_virgl_call(owner, BGPU_CONTEXT_DESTROY, &q, sizeof(q), NULL, 0);
    }
}

void gpu_virgl_diagnostics(void) {
    if (!info.cap_bytes) return;
    unsigned nc = 0, nr = 0;
    for (int i = 0; i < CONTEXTS; i++) nc += !!contexts[i].id;
    for (int i = 0; i < RESOURCES; i++) nr += !!resources[i].id;
    char line[160];
    ksnprintf(line, sizeof(line), "    virgl: %u contexts, %u resources, %u KiB backing (128 MiB limit)%s",
              nc, nr, memory_used >> 10, gpu_virtio_alive() ? "" : " - quarantined after reset");
    terminal_writeln(line);
}

void gpu_virgl_tests(void (*report)(const char*, int, const char*)) {
    if (!gpu_virgl_available()) { terminal_writeln("  skip  virgl - use GPU=virgl with a GL-capable QEMU display"); return; }
    const int owner = 0x7ffffffe;
    banana_gpu_request_t q = {0};
    banana_gpu_info_t description;
    report("virgl host capabilities are available", !gpu_virgl_call(owner, BGPU_INFO, &q, sizeof(q), &description, sizeof(description)) && description.cap_bytes, "");
    int context = gpu_virgl_call(owner, BGPU_CONTEXT_CREATE, &q, sizeof(q), NULL, 0);
    report("virgl creates an application context", context > 0, "");
    if (context <= 0) return;
    q.context = (unsigned)context;
    q.p[0] = 0; q.p[1] = 64; q.p[2] = 1 << 4; q.p[3] = 4096; q.p[4] = q.p[5] = q.p[6] = 1; q.p[10] = 4096;
    int id = gpu_virgl_call(owner, BGPU_RESOURCE_CREATE, &q, sizeof(q), NULL, 0);
    q.resource = (unsigned)id;
    int ok = id > 0 && !gpu_virgl_call(owner, BGPU_ATTACH, &q, sizeof(q), NULL, 0);
    report("virgl creates and attaches an owned buffer", ok, "");
    if (ok) {
        uint8_t a[4096], b[4096];
        for (unsigned i = 0; i < sizeof(a); i++) a[i] = (uint8_t)(i ^ (i >> 3));
        ok = !gpu_virgl_call(owner, BGPU_WRITE, &q, sizeof(q), a, sizeof(a));
        memset(q.p, 0, sizeof(q.p)); q.p[3] = 4096; q.p[4] = q.p[5] = 1;
        ok &= !gpu_virgl_call(owner, BGPU_TRANSFER_PUT, &q, sizeof(q), NULL, 0);
        memset(b, 0, sizeof(b)); gpu_virgl_call(owner, BGPU_WRITE, &q, sizeof(q), b, sizeof(b));
        ok &= !gpu_virgl_call(owner, BGPU_TRANSFER_GET, &q, sizeof(q), NULL, 0);
        ok &= !gpu_virgl_call(owner, BGPU_READ, &q, sizeof(q), b, sizeof(b));
        report("virgl host upload/readback preserves every byte", ok && !memcmp(a, b, sizeof(a)), "4 KiB through the host 3D resource");
        report("another application cannot access GPU resources", gpu_virgl_call(owner - 1, BGPU_READ, &q, sizeof(q), b, sizeof(b)) < 0 &&
               gpu_virgl_call(owner - 1, BGPU_CONTEXT_DESTROY, &q, sizeof(q), NULL, 0) < 0, "");
        q.offset = 0; q.p[0] = 4096; q.p[3] = 1;
        report("GPU resource boxes reject coordinates beyond their allocation", gpu_virgl_call(owner, BGPU_TRANSFER_GET, &q, sizeof(q), NULL, 0) < 0, "");
        q.offset = 4090;
        report("GPU buffer transfers reject out-of-bounds access", gpu_virgl_call(owner, BGPU_WRITE, &q, sizeof(q), a, 16) < 0, "");
    }
    gpu_virgl_release(owner);
    report("GPU resources are released with their application", !resource(owner, (unsigned)id) && context_index(owner, context) < 0, "");
}
