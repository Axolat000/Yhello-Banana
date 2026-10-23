/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The GPU model and kernel stubs of tools/tests/test_i915.c, shared with
 * tools/tests/crocus_host (Mesa's crocus on the model). */
#include "gpu_i915.h"
#include "i915_uapi.h"
#include "uvm.h"
#include "kstring.h"

extern int printf(const char*, ...);
extern int vsnprintf(char*, size_t, const char*, __builtin_va_list);
extern void* mmap(void*, size_t, int, int, int, long);
extern void exit(int);

/* ── memory: an arena below 4 GiB (physical = virtual) ─────────────── */
#ifndef MODEL_ARENA_MB
#define MODEL_ARENA_MB 512
#endif
static uint8_t* arena;
static uint64_t arena_used, arena_size = (uint64_t)MODEL_ARENA_MB << 20;
static void* arena_take(uint64_t n, uint64_t align) {
    arena_used = (arena_used + align - 1) & ~(align - 1);
    if (arena_used + n > arena_size) { printf("arena full\n"); exit(2); }
    void* p = arena + arena_used;
    arena_used += n;
    return p;
}
void* kmalloc(size_t n) { return arena_take(n ? n : 1, 16); }
void* kzalloc(size_t n) { void* p = kmalloc(n); memset(p, 0, n); return p; }
void* kmalloc_aligned(size_t n, size_t a) { return arena_take(n, a); }
void  kfree(void* p) { (void)p; }
static void* freelist[65536];
static int nfree;
static uint32_t pages_out;
void* upage_alloc(void) {
    pages_out++;
#ifdef MODEL_NO_REUSE                       /* (a buffer's pages stay contiguous: they can be "mapped") */
    return arena_take(4096, 4096);
#else
    return nfree ? freelist[--nfree] : arena_take(4096, 4096);
#endif
}
void  upage_free(void* p) { pages_out--; if (nfree < 65536) freelist[nfree++] = p; }

void* memcpy(void* d, const void* s, size_t n) { uint8_t* a = d; const uint8_t* b = s; while (n--) *a++ = *b++; return d; }
void* memset(void* d, int c, size_t n) { uint8_t* a = d; while (n--) *a++ = (uint8_t)c; return d; }
int memcmp(const void* x, const void* y, size_t n) { const uint8_t* a = x; const uint8_t* b = y; for (; n; n--, a++, b++) if (*a != *b) return *a - *b; return 0; }
size_t strlen(const char* s) { size_t n = 0; while (s[n]) n++; return n; }
size_t kstrlcpy(char* d, const char* s, size_t n) { size_t l = strlen(s); if (n) { size_t c = l < n - 1 ? l : n - 1; memcpy(d, s, c); d[c] = 0; } return l; }
int ksnprintf(char* b, size_t n, const char* f, ...) { __builtin_va_list ap; __builtin_va_start(ap, f); int r = vsnprintf(b, n, f, ap); __builtin_va_end(ap); return r; }
static int quiet;
void klog(const char* f, ...) { if (quiet) return; char b[512]; __builtin_va_list ap; __builtin_va_start(ap, f); vsnprintf(b, sizeof b, f, ap); __builtin_va_end(ap); printf("    klog: %s", b); }

/* ── the GPU model ─────────────────────────────────────────────────── */
static uint8_t* bar;                       /* 4 MiB: registers, then the GTT */
static int gen, hsw;
static int hung;                            /* a batch is stuck */
static uint32_t now_ms = 1000;
static int pumping;
#define REG(r) (*(volatile uint32_t*)(bar + (r)))
static uint32_t g_ran, g_resets_seen;

static uint32_t* phys_ptr(uint64_t pa) { return (uint32_t*)(uintptr_t)pa; }
static uint64_t pte_phys(uint32_t e) {
    uint64_t hi = gen == 5 ? (e & 0xF0u) : hsw ? (e & 0x7F0u) : (e & 0xFF0u);
    return (uint64_t)(e & 0xFFFFF000u) | hi << 28;
}
/* an address of the GPU's: through the GTT, or the PPGTT (its directory: PP_DIR_BASE) */
static uint32_t* xlate(uint32_t a, int ppgtt) {
    volatile uint32_t* gsm = (volatile uint32_t*)(bar + (2u << 20));
    uint32_t e;
    if (ppgtt && gen >= 6) {
        uint32_t base = REG(0x2228) >> 10;                 /* the directory's byte offset in the GTT's entries */
        uint32_t pde = gsm[base / 4 + (a >> 22)];
        if (!(pde & 1)) return NULL;
        uint32_t* pt = phys_ptr(pte_phys(pde));
        e = pt[(a >> 12) & 1023];
    } else e = gsm[a >> 12];
    if (!(e & 1)) return NULL;
    return phys_ptr(pte_phys(e) + (a & 4095));
}
static int bad_writes;
static void gpu_write(uint32_t a, uint32_t v, int ppgtt) {
    uint32_t* p = xlate(a & ~3u, ppgtt);
    if (!p) { bad_writes++; printf("    model: a write to unmapped %08x (%s)\n", a, ppgtt ? "PPGTT" : "GTT"); return; }
    *p = v;
}

/* hardware contexts (gen 6+): the registers batches set with
 * MI_LOAD_REGISTER_IMM stand for the 3D state; MI_SET_CONTEXT saves them
 * into the image the GPU holds and restores the next one's (unless
 * MI_RESTORE_INHIBIT) - the image: a count, then (register, value) pairs */
#define CTXREG_MAX 64
static uint32_t ctxreg[CTXREG_MAX], nctxreg;
static uint32_t ctx_cur;                    /* the image's GTT address (0: none) */
static uint32_t ctx_switches, ctx_inhibits, ctx_restores;
static void ctxreg_note(uint32_t r) {
    if (r < 0x2000 || r >= 0x8000 || (r >= 0x2000 && r < 0x2400)) return;   /* (3D state, not the ring's own) */
    for (uint32_t i = 0; i < nctxreg; i++) if (ctxreg[i] == r) return;
    if (nctxreg < CTXREG_MAX) ctxreg[nctxreg++] = r;
}
static uint32_t* xlate(uint32_t a, int ppgtt);
static void ctx_save(void) {
    if (!ctx_cur) return;
    uint32_t* img = xlate(ctx_cur + 0x400, 0);
    if (!img) return;
    img[0] = nctxreg;
    for (uint32_t i = 0; i < nctxreg; i++) { img[1 + 2 * i] = ctxreg[i]; img[2 + 2 * i] = REG(ctxreg[i]); }
}
static void ctx_load(uint32_t a, int inhibit) {
    if (inhibit) { ctx_inhibits++; return; }
    uint32_t* img = xlate(a + 0x400, 0);
    if (!img || img[0] > CTXREG_MAX) return;
    ctx_restores++;
    for (uint32_t i = 0; i < nctxreg; i++) REG(ctxreg[i]) = 0;     /* (what the image does not have: as at start) */
    for (uint32_t i = 0; i < img[0]; i++) { ctxreg_note(img[1 + 2 * i]); REG(img[1 + 2 * i]) = img[2 + 2 * i]; }
}
/* what the GPU's registers say before the driver touches them */
static void model_regs(void) {
    if (gen == 6) REG(0x21A0) = 63u << 6 | 63u;                 /* CXT_SIZE: 126 x 64 bytes */
    if (gen == 7) REG(0x21A8) = 100u << 9 | 20u;                /* GEN7_CXT_SIZE: 120 x 64 bytes */
    ctx_cur = 0; nctxreg = 0;
}

/* runs commands at a (through the GTT or the PPGTT) until a batch end; 0 when done, -1 stuck */
static int run(uint32_t a, int ppgtt, int depth, uint32_t end, int is_ring);
static uint32_t hws_base(void) { return gen == 5 ? REG(0x2080) : REG(0x4080); }

static int exec_cmd(uint32_t* c, uint32_t* len, int ppgtt, int depth) {
    uint32_t h = c[0], client = h >> 29;
    *len = 1;
    if (client == 0) {                                   /* MI */
        uint32_t op = (h >> 23) & 0x3F;
        if (op >= 0x10) *len = (h & 0x3F) + 2;
        if (op == 0x22 || op == 0x1A) *len = (h & 0xFF) + 2;              /* LOAD_REGISTER_IMM, MATH */
        if (op == 0x31) *len = (gen == 5 ? (h & 0x3F) : (h & 0xFF)) + 2;   /* (gen 5: bits 7:6 are MI_BATCH_GTT) */
        switch (op) {
        case 0x0A: return 1;                             /* MI_BATCH_BUFFER_END */
        case 0x21: gpu_write(hws_base() + c[1], c[2], 0); break;     /* STORE_DWORD_INDEX */
        case 0x20: {                                     /* STORE_DATA_IMM */
            int global = (h >> 22) & 1;
            if (gen == 5 && ppgtt && global) { printf("    model: an unprivileged batch stored to the GTT\n"); bad_writes++; break; }
            gpu_write(c[2], c[3], gen >= 6 && ppgtt && !global);
            break;
        }
        case 0x24: gpu_write(c[2], REG(c[1] & 0x3FFFFC), gen >= 6 && ppgtt); break;  /* STORE_REGISTER_MEM */
        case 0x22:                                       /* LOAD_REGISTER_IMM: (register, value) pairs */
            for (uint32_t k = 1; k + 1 < *len; k += 2) { uint32_t r = c[k] & 0x3FFFFC; REG(r) = c[k + 1]; ctxreg_note(r); }
            break;
        case 0x18: {                                     /* SET_CONTEXT */
            uint32_t a = c[1] & ~0xFFFu, fl = c[1] & 0xFFFu;
            if (gen < 6 || depth > 0) { printf("    model: MI_SET_CONTEXT %s\n", gen < 6 ? "on gen 5" : "in a batch"); bad_writes++; break; }
            if (!(fl & (1u << 8)) || (gen == 6 && (a & 0xFFFF))) { printf("    model: MI_SET_CONTEXT %08x: not GTT / misaligned\n", c[1]); bad_writes++; }
            ctx_save();
            ctx_load(a, fl & 1);
            ctx_cur = a;
            ctx_switches++;
            REG(0x2180) = a | 1;                         /* CCID */
            break;
        }
        case 0x3F: hung = 1; return -1;                  /* (the tests' "hang here") */
        case 0x31: {                                     /* BATCH_BUFFER_START */
            int unpriv = gen == 5 ? 0 : (h >> 8) & 1;
            if (gen == 5 && !((h >> 7) & 1)) { printf("    model: a gen 5 batch without MI_BATCH_GTT\n"); bad_writes++; }
            if (depth > 2) return -1;
            g_ran++;
            int r = run(c[1], unpriv, depth + 1, 0, 0);
            if (r < 0) return -1;
            if (depth > 0) return 1;                     /* (from a batch: a chain, it does not come back) */
            break;
        }
        }
        return 0;
    }
    if (client == 3) {                                   /* render: PIPE_CONTROL and the rest */
        *len = (h & 0xFF) + 2;
        if (((h >> 24) & 0x1F) == 0x1A) {                /* 3,3,2: PIPE_CONTROL */
            if (gen == 5) {
                if (h & (1u << 14)) gpu_write(c[1] & ~7u, c[2], 0);
            } else {
                uint32_t flags = c[1];
                if (((flags >> 14) & 3) == 1) {
                    int global = gen == 6 ? (c[2] >> 2) & 1 : (flags >> 24) & 1;
                    gpu_write(c[2] & ~7u, c[3], ppgtt && !global);
                }
            }
        }
        return 0;
    }
    if (client == 2) { *len = (h & 0xFF) + 2; return 0; }
    printf("    model: an unknown command %08x\n", h);
    bad_writes++;
    return 0;
}

static int run(uint32_t a, int ppgtt, int depth, uint32_t end, int is_ring) {
    for (int guard = 0; guard < 1000000; guard++) {
        if (is_ring && a == end) return 0;
        uint32_t* c = xlate(a, ppgtt);
        if (!c) { printf("    model: fetch from unmapped %08x\n", a); bad_writes++; return -1; }
        uint32_t len;
        int r = exec_cmd(c, &len, ppgtt, depth);
        if (r) return r > 0 ? 0 : -1;
        a += len * 4;
        if (is_ring) {
            uint32_t size = ((REG(0x203C) & 0x1FF000u) + 4096u);
            uint32_t start = REG(0x2038);
            if (a >= start + size) a = start;
            REG(0x2034) = a - start;
            REG(0x2074) = a;                             /* ACTHD */
        }
    }
    return -1;
}

static void pump(void) {
    if (pumping) return;
    pumping = 1;
    /* forcewake: the acks follow */
    if (gen >= 6) {
        REG(0x130090) = REG(0xA18C) & 1;
        uint32_t mt = REG(0xA188);
        static uint32_t mt_state;
        if (mt >> 16) { mt_state = (mt_state & ~(mt >> 16)) | (mt & (mt >> 16)); REG(0xA188) = 0; }
        REG(hsw ? 0x130044 : 0x130040) = mt_state & 1;
        REG(0xA180) = gen == 7 && !hsw && (mt_state & 1) ? 1u << 5 : 0;  /* ECOBUS: MT forcewake set up */
        REG(0x13805C) = 0;
    }
    /* masked registers settle: STOP_RING -> idle; sync flush done */
    uint32_t mi = REG(0x209C);
    if (mi >> 16) { static uint32_t mode; mode = (mode & ~(mi >> 16)) | (mi & (mi >> 16)); REG(0x209C) = (mode & 0xFFFF) | ((mode & (1u << 8)) ? 1u << 9 : 0); }
    uint32_t ip = REG(0x20C0);
    if (ip >> 16) REG(0x20C0) = ip & 0xFFFF & ~(1u << 5);
    /* the GT reset */
    if (gen >= 6 && (REG(0x941C) & 1)) {
        REG(0x941C) = 0; hung = 0; g_resets_seen++; ctx_cur = 0;
        REG(0x2034) = 0; REG(0x2030) = 0; REG(0x203C) = 0; REG(0x2038) = 0;
    }
    if (gen == 5 && (REG(0x12CA4) & 1)) {
        REG(0x12CA4) &= ~1u;
        if (((REG(0x12CA4) >> 1) & 3) == 1) { hung = 0; g_resets_seen++; REG(0x2034) = 0; REG(0x2030) = 0; REG(0x203C) = 0; }
    }
    REG(0x2358) = now_ms * 12500u;                       /* the engine's clock: 12.5 MHz */
    /* the ring: HEAD runs to TAIL */
    if ((REG(0x203C) & 1) && !hung && !(REG(0x209C) & (1u << 8))) {
        uint32_t start = REG(0x2038), head = REG(0x2034) & 0x1FFFFC, tail = REG(0x2030) & 0x1FFFF8;
        if (head != tail) run(start + head, 0, 0, start + tail, 1);
    }
    pumping = 0;
}

uint32_t timer_ms(void) { pump(); return now_ms++; }
uint64_t timer_counter(void) { return (uint64_t)now_ms * 1000000ull; }
void task_sleep_ms(uint32_t ms) { now_ms += ms ? ms : 1; pump(); }
void task_yield(void) { pump(); }

/* the settings: gpu3d on */
static int g_on = 1;
int  settings_get(const char* k, char* out, int cap) { (void)k; if (cap < 4) return 0; memcpy(out, g_on ? "on" : "off", g_on ? 3 : 4); return 1; }
void settings_set(const char* k, const char* v) { (void)k; g_on = v[1] == 110; }
int  strcmp(const char* a, const char* b) { while (*a && *a == *b) a++, b++; return (unsigned char)*a - (unsigned char)*b; }

