/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Mesa's crocus or iris, built for Linux (the reference build of ports/mesa
 * with CROCUS=1 or IRIS=1), driven on the hosted kernel i915 interface (kernel/gpu_i915.c,
 * gpu_i915_lrc.c, gpu_gem.c) and the GPU model of tools/tests/i915/model.h - as an
 * LD_PRELOAD library: /dev/dri/card0 and its ioctls, libdrm's device query
 * and the buffers' maps are answered here. The model runs every command
 * stream the driver builds (it skips what it does not execute by its length,
 * keeps PIPE_CONTROL and MI writes - the driver's fences and queries) - so the
 * whole path is checked, from glClear to the read-back, but not the pixels.
 *   see tools/tests/crocus_host/run.sh and tools/tests/iris_host/run.sh */
#define MODEL_NO_REUSE 1
#define MODEL_ARENA_MB 768                  /* (MAP_32BIT: the first 2 GiB) */
#include "i915/model.h"

typedef unsigned long ulong;
extern int printf(const char*, ...);
extern char* getenv(const char*);
extern int atoi(const char*);
extern void* dlsym(void*, const char*);
#define RTLD_NEXT ((void*)-1l)
extern int* __errno_location(void);
extern char* strdup(const char*);
extern void* calloc(size_t, size_t);
extern void free(void*);
extern int pthread_mutex_lock(void*);
extern int pthread_mutex_unlock(void*);
static uint64_t g_mx[8];                    /* (a pthread_mutex_t: 40 bytes on x86_64) */

static uspace_t* fake_us = (uspace_t*)(uintptr_t)0x1000;
int app_user_ok(const void* p, uint64_t n) { (void)n; return p != NULL; }
uspace_t* app_user_space(void) { return fake_us; }
uintptr_t uvm_map_frames(uspace_t* us, const uint64_t* f, uint32_t n, int prot, int wc) {
    (void)us; (void)prot; (void)wc;
    for (uint32_t i = 1; i < n; i++) if (f[i] != f[0] + i * 4096ull) { printf("preload: a buffer's pages are not contiguous\n"); return 0; }
    return (uintptr_t)f[0];
}
int uvm_unmap_frames(uspace_t* us, uintptr_t a, uint32_t n, uint64_t f0) { (void)us; (void)n; return a == f0 ? 0 : -1; }

/* ── the device ────────────────────────────────────────────────────── */
static int g_ready;
static char g_drm[1024];                     /* fds that are /dev/dri/card0 */
static int (*real_open)(const char*, int, ...);
static int (*real_close)(int);
static int (*real_ioctl)(int, ulong, ...);
static int (*real_fcntl)(int, int, ...);
static int (*real_munmap)(void*, size_t);
static ulong g_ioctls, g_execs;

static void boot(void) {
    if (g_ready) return;
    g_ready = 1;
    real_open = dlsym(RTLD_NEXT, "open");
    real_close = dlsym(RTLD_NEXT, "close");
    real_ioctl = dlsym(RTLD_NEXT, "ioctl");
    real_fcntl = dlsym(RTLD_NEXT, "fcntl");
    real_munmap = dlsym(RTLD_NEXT, "munmap");
    arena = mmap(0, arena_size, 3, 0x22 | 0x40, -1, 0);
    if (arena == (void*)-1) { printf("preload: no memory below 2 GiB for the model\n"); exit(2); }
    const char* g = getenv("MODEL_GEN");
    int which = g ? atoi(g) : 7;
    gen = which == 75 ? 7 : which; hsw = which == 75;
    /* 9: Skylake, 95: Coffee Lake (gen 9.5) */
    uint16_t id = gen == 5 ? 0x0046 : gen == 6 ? 0x0116 : hsw ? 0x0416 : gen == 7 ? 0x0166 : gen == 8 ? 0x1616 :
                  gen == 9 ? 0x1912 : gen == 95 ? 0x3E92 : 0x8A52;
    if (gen == 95) gen = 9;
    bar = (uint8_t*)arena_take(bar_size(), 4096);
    memset(bar, 0, bar_size());
    i915_hw_t hw;
    memset(&hw, 0, sizeof hw);
    hw.mmio = bar; hw.mmio_size = bar_size(); hw.devid = id; hw.revision = 9; hw.gen = gen; hw.hsw = hsw;
    hw.gtt_entries = gen == 5 ? 256u * 1024u : gen >= 8 ? 1024u * 1024u : 512u * 1024u;
    hw.reserved_lo = 8192; hw.reserved_hi = 16;
    quiet = getenv("MODEL_QUIET") != NULL;
    model_regs();
    i915_attach(&hw);
    printf("preload: a gen %d%s GPU model (%04x) on /dev/dri/card0\n", gen, hsw ? ".5" : "", id);
}

static int is_drm(int fd) { return fd >= 0 && fd < 1024 && g_drm[fd]; }

int open(const char* path, int flags, ...) {
    boot();
    __builtin_va_list ap; __builtin_va_start(ap, flags); int mode = __builtin_va_arg(ap, int); __builtin_va_end(ap);
    if (path && (!strcmp(path, "/dev/dri/card0") || !strcmp(path, "/dev/dri/renderD128"))) {
        int fd = real_open("/dev/null", 2);
        if (fd >= 0 && fd < 1024) g_drm[fd] = 1;
        return fd;
    }
    return real_open(path, flags, mode);
}
int open64(const char* path, int flags, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, flags); int mode = __builtin_va_arg(ap, int); __builtin_va_end(ap);
    return open(path, flags, mode);
}
int close(int fd) { boot(); if (is_drm(fd)) g_drm[fd] = 0; return real_close(fd); }
int fcntl(int fd, int cmd, ...) {
    boot();
    __builtin_va_list ap; __builtin_va_start(ap, cmd); long arg = __builtin_va_arg(ap, long); __builtin_va_end(ap);
    int r = real_fcntl(fd, cmd, arg);
    if ((cmd == 0 || cmd == 1030) && r >= 0 && r < 1024 && is_drm(fd)) g_drm[r] = 1;   /* F_DUPFD(_CLOEXEC) */
    return r;
}
/* (Mesa is built with 64-bit file offsets: glibc's fcntl is fcntl64 there) */
int fcntl64(int fd, int cmd, ...) {
    __builtin_va_list ap; __builtin_va_start(ap, cmd); long arg = __builtin_va_arg(ap, long); __builtin_va_end(ap);
    return fcntl(fd, cmd, arg);
}
int ioctl(int fd, ulong req, ...) {
    boot();
    __builtin_va_list ap; __builtin_va_start(ap, req); void* arg = __builtin_va_arg(ap, void*); __builtin_va_end(ap);
    if (!is_drm(fd)) return real_ioctl(fd, req, arg);
    pthread_mutex_lock(g_mx);
    g_ioctls++;
    if (((req >> 8) & 0xFF) == 'd' && (req & 0xFF) == 0x40 + 0x29) g_execs++;
    int r = gem_ioctl(1, req, arg);
    pthread_mutex_unlock(g_mx);
    if (r < 0) { *__errno_location() = -r; return -1; }
    return r;
}
/* (a buffer's map is the model's memory itself: unmapping it is a no-op) */
int munmap(void* p, size_t n) {
    boot();
    if ((uint8_t*)p >= arena && (uint8_t*)p < arena + arena_size) return 0;
    return real_munmap(p, n);
}

/* libdrm: the device is the model */
typedef struct { char** nodes; int available_nodes, bustype; void* businfo; void* deviceinfo; } dd_t;
typedef struct { uint16_t domain; uint8_t bus, dev, func; } pci_bus_t;
typedef struct { uint16_t vendor_id, device_id, subvendor_id, subdevice_id; uint8_t revision_id; } pci_info_t;
int drmGetDevice2(int fd, uint32_t flags, dd_t** out) {
    (void)fd; (void)flags;
    boot();
    struct { dd_t d; char* nodes[3]; pci_bus_t b; pci_info_t i; }* w = calloc(1, sizeof(*w));
    w->nodes[0] = strdup("/dev/dri/card0"); w->nodes[2] = strdup("/dev/dri/renderD128");
    w->d.nodes = w->nodes; w->d.available_nodes = 5; w->d.bustype = 0;   /* DRM_BUS_PCI */
    w->b.dev = 2;
    w->i.vendor_id = 0x8086; w->i.device_id = i915_hw()->devid; w->i.revision_id = 9;
    w->d.businfo = &w->b; w->d.deviceinfo = &w->i;
    *out = &w->d;
    return 0;
}
int drmGetDevice(int fd, dd_t** out) { return drmGetDevice2(fd, 0, out); }
void drmFreeDevice(dd_t** d) { if (d && *d) { free(*d); *d = NULL; } }
int drmIoctl(int fd, ulong req, void* arg) { return ioctl(fd, req, arg); }

/* a summary at the end */
__attribute__((destructor)) static void bye(void) {
    if (!g_ready) return;
    char b[200];
    gem_stats(b, sizeof b);
    printf("preload: %lu requests, %lu submissions; %s; %d stray GPU writes, %u GPU reset(s)\n",
           g_ioctls, g_execs, b, bad_writes, i915_resets());
}
