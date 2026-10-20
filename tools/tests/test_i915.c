/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hosted run of kernel/gpu_i915.c and kernel/gpu_gem.c against a model of
 * an Intel GPU's command streamer, gen 5, 6, 7 and 7.5. The model: the
 * register BAR (forcewake acks, masked registers, the GT reset), the global
 * GTT in its upper half, the aliasing PPGTT (its directory in GTT entries,
 * its tables in memory), the render ring (HEAD chases TAIL) and the
 * commands the kernel and these tests use - MI_STORE_DATA_IMM,
 * MI_STORE_DWORD_INDEX, PIPE_CONTROL's post-sync write, MI_BATCH_BUFFER_
 * START / END, everything else skipped by its length - through the address
 * space each batch uses (privileged: the GTT, else the PPGTT from gen 6).
 * "Memory" is one arena below 4 GiB, so the GTT's entries hold it. Then
 * the i915 interface as Mesa uses it: buffers, maps, relocations, execbuffer2
 * with a fence array, waits, syncobjs, contexts, a hung batch and the
 * reset, buffers closed while busy, the app's end; and `gpu3d test`.
 *   cc -O1 -no-pie -w -I kernel -I tools/tests tools/tests/test_i915.c kernel/gpu_i915.c kernel/gpu_gem.c -o /tmp/i915 && /tmp/i915 */
#include "i915/model.h"

/* ── the app side ──────────────────────────────────────────────────── */
static uspace_t* fake_us = (uspace_t*)(uintptr_t)0x1000;
int app_user_ok(const void* p, uint64_t n) { (void)n; return p != NULL; }
uspace_t* app_user_space(void) { return fake_us; }
static int maps_live;
uintptr_t uvm_map_frames(uspace_t* us, const uint64_t* f, uint32_t n, int prot, int wc) {
    (void)us; (void)prot; (void)wc;
    for (uint32_t i = 1; i < n; i++) if (f[i] != f[0] + i * 4096ull) { printf("    (a buffer's pages are not contiguous here: the model cannot map it)\n"); return 0; }
    maps_live++;
    return (uintptr_t)f[0];
}
int uvm_unmap_frames(uspace_t* us, uintptr_t a, uint32_t n, uint64_t f0) { (void)us; (void)n; if (a != f0) return -1; maps_live--; return 0; }

#define IOC(dir, nr, size) ((unsigned long)(dir) << 30 | (unsigned long)(size) << 16 | 'd' << 8 | (nr))
#define W 1
#define R 2
#define WR 3
static int io(int owner, int dir, unsigned nr, void* arg, unsigned size) { return gem_ioctl(owner, IOC(dir, nr, size), arg); }
#define I915(nr) (DRM_COMMAND_BASE + (nr))

static int failed, checks;
static void check(const char* what, int ok) { checks++; if (!ok) { failed++; printf("    FAIL: %s\n", what); } }

static uint32_t create(int owner, uint64_t size) {
    struct drm_i915_gem_create c = { size, 0, 0 };
    int r = io(owner, WR, I915(I915_NR_GEM_CREATE), &c, sizeof c);
    return r ? 0 : c.handle;
}
static uint32_t* map(int owner, uint32_t h, uint64_t size) {
    struct drm_i915_gem_mmap m = { h, 0, 0, size, 0, 0 };
    if (io(owner, WR, I915(I915_NR_GEM_MMAP), &m, sizeof m)) return NULL;
    return (uint32_t*)(uintptr_t)m.addr_ptr;
}
static int gem_wait(int owner, uint32_t h, int64_t ns) {
    struct drm_i915_gem_wait w = { h, 0, ns };
    return io(owner, WR, I915(I915_NR_GEM_WAIT), &w, sizeof w);
}

static void report(const char* name, int ok, const char* d) { printf("    gpu3d test: %s %s: %s\n", ok ? "ok  " : "FAIL", name, d); check(name, ok); }

static void one(int g, int is_hsw, uint16_t devid) {
    gen = g; hsw = is_hsw;
    printf("gen %s (%04x)\n", hsw ? "7.5" : g == 7 ? "7" : g == 6 ? "6" : "5", devid);
    bar = (uint8_t*)arena_take(4u << 20, 4096);
    memset(bar, 0, 4u << 20);
    g_ran = 0; bad_writes = 0; hung = 0; g_resets_seen = 0;
    /* (a fresh driver: the globals of the two files start over by running the program again
     *  - so each generation runs in its own process: see main) */
    i915_hw_t hw;
    memset(&hw, 0, sizeof hw);
    hw.mmio = bar; hw.mmio_size = 4u << 20; hw.devid = devid; hw.revision = 9; hw.gen = g; hw.hsw = is_hsw;
    hw.gtt_entries = g == 5 ? 256u * 1024u : 512u * 1024u;
    hw.reserved_lo = 8192; hw.reserved_hi = 16;
    i915_attach(&hw);
    check("present", i915_present());

    int A = 7;                                           /* an app */
    g_on = 0;
    struct drm_version v0; memset(&v0, 0, sizeof v0);
    check("off (gpu3d off): apps get no GPU", io(A, WR, DRM_NR_VERSION, &v0, sizeof v0) == -19 && !i915_running());
    gem_set_enabled(1);
    check("gpu3d on", gem_enabled());
    struct drm_version v; memset(&v, 0, sizeof v);
    char name[16] = {0};
    v.name_len = sizeof name; v.name = (uint64_t)(uintptr_t)name;
    check("DRM_IOCTL_VERSION (starts the ring)", io(A, WR, DRM_NR_VERSION, &v, sizeof v) == 0 && !memcmp(name, "i915", 5));
    check("the ring runs", i915_running());
    int chip = 0;
    struct drm_i915_getparam gp = { I915_PARAM_CHIPSET_ID, 0, (uint64_t)(uintptr_t)&chip };
    check("GETPARAM chipset", io(A, WR, I915(I915_NR_GETPARAM), &gp, sizeof gp) == 0 && chip == devid);
    int llc = -1;
    gp.param = I915_PARAM_HAS_LLC; gp.value = (uint64_t)(uintptr_t)&llc;
    check("GETPARAM has_llc", io(A, WR, I915(I915_NR_GETPARAM), &gp, sizeof gp) == 0 && llc == (g >= 6));
    gp.param = 9999;
    check("GETPARAM unknown: EINVAL", io(A, WR, I915(I915_NR_GETPARAM), &gp, sizeof gp) == -22);

    struct drm_i915_gem_context_create_ext cc = { 0, 0, 0 };
    check("CONTEXT_CREATE", io(A, WR, I915(I915_NR_CONTEXT_CREATE), &cc, 8) == 0 && cc.ctx_id);
    struct drm_i915_gem_context_param cp = { cc.ctx_id, 0, I915_CONTEXT_PARAM_RECOVERABLE, 0 };
    check("CONTEXT_SETPARAM recoverable=0", io(A, WR, I915(I915_NR_CONTEXT_SETPARAM), &cp, sizeof cp) == 0);
    cp.param = I915_CONTEXT_PARAM_GTT_SIZE;
    check("CONTEXT_GETPARAM gtt size", io(A, WR, I915(I915_NR_CONTEXT_GETPARAM), &cp, sizeof cp) == 0 && cp.value == (uint64_t)hw.gtt_entries * 4096u);

    uint32_t hb = create(A, 8192), ht = create(A, 4096), hbig = create(A, 1u << 20);
    check("GEM_CREATE x3", hb && ht && hbig);
    uint32_t* bm = map(A, hb, 8192);
    uint32_t* tm = map(A, ht, 4096);
    check("GEM_MMAP", bm && tm);
    if (!bm || !tm) return;
    /* a batch: two stores and a PIPE_CONTROL write into the target, by relocation */
    /* (gen 5: unprivileged batches may not store to the GTT - Mesa writes with PIPE_CONTROL there) */
    int k = 0, nrel = 0;
    uint32_t rel_at[3], rel_delta[3];
    if (g != 5)
        for (int i = 0; i < 2; i++) {
            bm[k++] = (0x20u << 23) | 2u; bm[k++] = 0;
            rel_at[nrel] = (uint32_t)k; rel_delta[nrel++] = 64u + (uint32_t)i * 4u;
            bm[k++] = 0xDEAD0000u; bm[k++] = 0x11110000u + (uint32_t)i;
        }
    if (g == 5) { bm[k++] = (3u << 29 | 3u << 27 | 2u << 24 | 2u) | (1u << 14); rel_at[nrel] = (uint32_t)k; rel_delta[nrel++] = 72; bm[k++] = 0xDEAD0000u; bm[k++] = 0x22220000u; bm[k++] = 0; }
    else { bm[k++] = 3u << 29 | 3u << 27 | 2u << 24 | 2u; bm[k++] = (1u << 14) | (1u << 20) | (1u << 12); rel_at[nrel] = (uint32_t)k; rel_delta[nrel++] = 72; bm[k++] = 0xDEAD0000u; bm[k++] = 0x22220000u; }
    bm[k++] = 0x0Au << 23; bm[k++] = 0;
    struct drm_i915_gem_relocation_entry rl[3];
    for (int i = 0; i < nrel; i++) rl[i] = (struct drm_i915_gem_relocation_entry){ 1 /* LUT: the target */, rel_delta[i], rel_at[i] * 4u, 0, 2, 2 };
    struct drm_i915_gem_exec_object2 ex[2];
    memset(ex, 0, sizeof ex);
    ex[0].handle = hb; ex[0].relocation_count = (uint32_t)nrel; ex[0].relocs_ptr = (uint64_t)(uintptr_t)rl;
    ex[1].handle = ht; ex[1].flags = EXEC_OBJECT_WRITE;
    struct drm_syncobj_create sc = { 0, 0 };
    check("SYNCOBJ_CREATE", io(A, WR, DRM_NR_SYNCOBJ_CREATE, &sc, sizeof sc) == 0 && sc.handle);
    struct drm_i915_gem_exec_fence fe = { sc.handle, I915_EXEC_FENCE_SIGNAL };
    struct drm_i915_gem_execbuffer2 eb;
    memset(&eb, 0, sizeof eb);
    eb.buffers_ptr = (uint64_t)(uintptr_t)ex; eb.buffer_count = 2; eb.batch_len = (uint32_t)k * 4;
    eb.flags = I915_EXEC_RENDER | I915_EXEC_NO_RELOC | I915_EXEC_HANDLE_LUT | I915_EXEC_BATCH_FIRST | I915_EXEC_FENCE_ARRAY;
    eb.cliprects_ptr = (uint64_t)(uintptr_t)&fe; eb.num_cliprects = 1; eb.rsvd1 = cc.ctx_id;
    int r = io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb);
    check("EXECBUFFER2", r == 0);
    check("the offsets written back", ex[1].offset && ex[0].offset && ex[1].offset != ex[0].offset);
    check("the relocations' presumed offsets", rl[0].presumed_offset == ex[1].offset);
    check("GEM_WAIT", gem_wait(A, ht, 1000000000ll) == 0);
    if (g != 5) check("the stores landed (PPGTT)", tm[16] == 0x11110000u && tm[17] == 0x11110001u);
    check("the PIPE_CONTROL write landed", tm[18] == 0x22220000u);
    struct drm_i915_gem_busy busy = { ht, 7 };
    check("GEM_BUSY: idle", io(A, WR, I915(I915_NR_GEM_BUSY), &busy, sizeof busy) == 0 && busy.busy == 0);
    uint32_t hs = sc.handle;
    struct drm_syncobj_wait sw = { (uint64_t)(uintptr_t)&hs, (int64_t)(now_ms + 1000) * 1000000, 1, 0, 0, 0 };
    check("SYNCOBJ_WAIT: signalled", io(A, WR, DRM_NR_SYNCOBJ_WAIT, &sw, sizeof sw) == 0);
    struct drm_syncobj_create sc2 = { 0, 0 };
    io(A, WR, DRM_NR_SYNCOBJ_CREATE, &sc2, sizeof sc2);
    hs = sc2.handle;
    sw.timeout_nsec = (int64_t)(now_ms + 50) * 1000000;
    check("SYNCOBJ_WAIT: an unsubmitted one -> EINVAL", io(A, WR, DRM_NR_SYNCOBJ_WAIT, &sw, sizeof sw) == -22);
    sw.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
    check("SYNCOBJ_WAIT for submit: times out", io(A, WR, DRM_NR_SYNCOBJ_WAIT, &sw, sizeof sw) == -62);

    /* again, offsets now right: the kernel leaves the batch alone */
    uint32_t mark = bm[rel_at[nrel - 1]];
    tm[18] = 0;
    check("EXECBUFFER2 again", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb) == 0);
    check("no relocation rewritten", bm[rel_at[nrel - 1]] == mark);
    check("GEM_WAIT again", gem_wait(A, ht, -1) == 0 && tm[18] == 0x22220000u);

    /* many: the ring wraps */
    uint32_t t = 0;
    for (int i = 0; i < 3000 && !r; i++) { r = io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb); t++; }
    check("3000 submissions", r == 0 && gem_wait(A, ht, -1) == 0);
    check("the model ran every batch", g_ran == 3002);

    /* a big buffer away from the 64 KiB-aligned place: alignment asked for */
    struct drm_i915_gem_exec_object2 ex2[2];
    memset(ex2, 0, sizeof ex2);
    ex2[0] = ex[0]; ex2[0].relocation_count = 0;
    ex2[1].handle = hbig; ex2[1].alignment = 1u << 20;
    eb.buffers_ptr = (uint64_t)(uintptr_t)ex2;
    uint32_t fl = (uint32_t)eb.flags;
    eb.flags &= ~(uint64_t)I915_EXEC_FENCE_ARRAY;
    check("EXECBUFFER2 with a 1 MiB alignment", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb) == 0 && ex2[1].offset % (1u << 20) == 0);
    eb.flags = fl;
    eb.buffers_ptr = (uint64_t)(uintptr_t)ex;

    /* a hung batch: reset after 2 s; the context is lost (not recoverable) */
    uint32_t hh = create(A, 4096);
    uint32_t* hm = map(A, hh, 4096);
    hm[0] = 0x3Fu << 23; hm[1] = 0x0Au << 23;
    struct drm_i915_gem_exec_object2 exh = { hh, 0, 0, 0, 0, 0, 0, 0 };
    struct drm_i915_gem_execbuffer2 ebh = eb;
    ebh.buffers_ptr = (uint64_t)(uintptr_t)&exh; ebh.buffer_count = 1; ebh.flags = I915_EXEC_RENDER;
    quiet = 1;
    check("EXECBUFFER2 (a batch that hangs)", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &ebh, sizeof ebh) == 0);
    uint32_t t0 = now_ms;
    int wr = gem_wait(A, hh, -1);
    quiet = 0;
    check("GEM_WAIT: -EIO after the reset", wr == -5 && now_ms - t0 >= 2000);
    check("the GPU was reset", g_resets_seen == 1 && i915_resets() == 1 && i915_running());
    struct drm_i915_reset_stats st = { cc.ctx_id, 0, 0, 0, 0, 0 };
    check("GET_RESET_STATS: the batch was active", io(A, WR, I915(I915_NR_GET_RESET_STATS), &st, sizeof st) == 0 && st.batch_active == 1);
    check("the lost context: EXECBUFFER2 -> EIO", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb) == -5);
    struct drm_i915_gem_context_create_ext cc2 = { 0, 0, 0 };
    io(A, WR, I915(I915_NR_CONTEXT_CREATE), &cc2, 8);
    eb.rsvd1 = cc2.ctx_id;
    tm[18] = 0;
    check("a new context works after the reset", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb) == 0 && gem_wait(A, ht, -1) == 0 && tm[18] == 0x22220000u);

    /* closing a buffer the GPU still uses: freed when the GPU is done */
    uint32_t before = pages_out;
    check("EXECBUFFER2", io(A, W, I915(I915_NR_GEM_EXECBUFFER2), &eb, sizeof eb) == 0);
    struct drm_gem_close gc = { ht, 0 };
    check("GEM_CLOSE while busy", io(A, W, DRM_NR_GEM_CLOSE, &gc, sizeof gc) == 0);
    check("its handle is gone", gem_wait(A, ht, 0) == -2);
    task_sleep_ms(5);
    struct drm_i915_gem_get_aperture ap;
    io(A, R, I915(I915_NR_GEM_GET_APERTURE), &ap, sizeof ap);
    io(A, 0, I915(I915_NR_GEM_THROTTLE), NULL, 0);
    check("then freed", pages_out == before - 1);
    check("another app cannot use A's buffer", gem_wait(A + 1, hb, 0) == -2);
    gem_release(A);
    check("all of the app's pages back at its end", pages_out == 0);
    check("no stray GPU writes", bad_writes == 0);

    /* gpu3d test */
    i915_tests(report);
}

int main(int argc, char** argv) {
    arena = mmap(0, arena_size, 3, 0x22 | 0x40 /* MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT */, -1, 0);
    if (!arena || arena == (void*)-1) { printf("no arena\n"); return 2; }
    int which = argc > 1 ? argv[1][0] - '0' : -1;
    if (which < 0) {
        /* each generation in a process of its own (the drivers' state is global) */
        extern int system(const char*);
        char cmd[512];
        int bad = 0;
        for (int i = 0; i < 4; i++) { ksnprintf(cmd, sizeof cmd, "%s %d", argv[0], i); bad += system(cmd) != 0; }
        printf(bad ? "%d generation(s) FAILED\n" : "all 4 generations passed\n", bad);
        return bad != 0;
    }
    static const struct { int gen, hsw; uint16_t id; } G[4] = { { 5, 0, 0x0046 }, { 6, 0, 0x0116 }, { 7, 0, 0x0166 }, { 7, 1, 0x0416 } };
    one(G[which].gen, G[which].hsw, G[which].id);
    printf("  %d of %d checks passed\n", checks - failed, checks);
    return failed != 0;
}
