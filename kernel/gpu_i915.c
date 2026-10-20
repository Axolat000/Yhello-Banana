/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Intel graphics, gen 5 to 7.5: the render engine (kernel/gpu_i915.h).
 *
 * After the Intel PRMs and how Linux's i915 drives these GPUs with a
 * "legacy" ring buffer:
 *  - the GPU's address spaces: the global GTT (a table of page entries in
 *    the upper half of the register BAR) and, from gen 6, an aliasing
 *    per-process GTT (PPGTT) with the same addresses - the batches apps
 *    submit use it (a non-privileged batch), the ring's own commands the
 *    global one;
 *  - the render ring: 128 KiB of commands the GPU reads from HEAD to TAIL;
 *    each batch an app submits is a MI_BATCH_BUFFER_START between a cache
 *    invalidation and a flush that writes its number (the seqno) to the
 *    status page - what is finished is read there;
 *  - "forcewake" (gen 6+): the GT's power well held awake while the
 *    driver runs, so its registers answer;
 *  - the hardware fixes (workarounds and clock gating) Linux applies to
 *    these generations, and bit-6 swizzling as the firmware set it up;
 *  - a hung GPU (no progress for 2 s): a reset of the render engine and
 *    everything set up again; the batches lost are reported.
 * There are no hardware contexts (MI_SET_CONTEXT): every batch of Mesa's
 * crocus sets the whole GPU state again (ports/mesa: the Banana patch). */
#include "gpu_i915.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"
#include "timer.h"
#include "task.h"

#ifdef __x86_64__

/* ── registers ─────────────────────────────────────────────────────── */
#define RCS             0x2000
#define R_TAIL          (RCS + 0x30)
#define R_HEAD          (RCS + 0x34)
#define R_START         (RCS + 0x38)
#define R_CTL           (RCS + 0x3C)
#define R_IPEIR         (RCS + 0x64)
#define R_IPEHR         (RCS + 0x68)
#define R_INSTDONE      (RCS + 0x6C)
#define R_ACTHD         (RCS + 0x74)
#define R_HWS_PGA5      (RCS + 0x80)
#define R_HWSTAM        (RCS + 0x98)
#define R_MI_MODE       (RCS + 0x9C)
#define R_EIR           (RCS + 0xB0)
#define R_ESR           (RCS + 0xB8)
#define R_PP_DCLV       (RCS + 0x220)
#define R_PP_BASE       (RCS + 0x228)
#define R_MODE7         (RCS + 0x29C)       /* RING_MODE_GEN7 */
#define R_TIMESTAMP     (RCS + 0x358)
#define R_HWS_PGA6      0x4080
#define R_INSTPM        0x20C0
#define R_GFX_MODE6     0x2520
#define HEAD_ADDR       0x001FFFFCu
#define RING_VALID      1u
#define MODE_IDLE       (1u << 9)
#define STOP_RING       (1u << 8)
#define GFX_FLSH_CNTL   0x101008

#define FORCEWAKE       0xA18C
#define FORCEWAKE_ACK   0x130090
#define FORCEWAKE_MT    0xA188
#define FORCEWAKE_MT_ACK  0x130040
#define FORCEWAKE_ACK_HSW 0x130044
#define ECOBUS          0xA180
#define GT_THREAD_STATUS 0x13805C
#define GEN6_GDRST      0x941C
#define ILK_GDSR        0x12CA4

#define GAC_ECO_BITS    0x14090
#define GAM_ECOCHK      0x4090
#define GAB_CTL         0x24000
#define TILECTL         0x101000
#define ARB_MODE        0x4030
#define DISP_ARB_CTL    0x45000

#define MASKED_EN(b)    ((uint32_t)(b) << 16 | (uint32_t)(b))
#define MASKED_DIS(b)   ((uint32_t)(b) << 16)
#define MASKED_FIELD(mask, v) ((uint32_t)(mask) << 16 | (uint32_t)(v))

/* ── commands ──────────────────────────────────────────────────────── */
#define MI(op, flags)          ((uint32_t)(op) << 23 | (uint32_t)(flags))
#define MI_NOOP                0u
#define MI_USER_INTERRUPT      MI(0x02, 0)
#define MI_FLUSH               MI(0x04, 0)
#define   MI_EXE_FLUSH         (1u << 1)
#define   MI_INVALIDATE_ISP    (1u << 5)
#define MI_BATCH_BUFFER_END    MI(0x0A, 0)
#define MI_STORE_DWORD_INDEX   MI(0x21, 1)
#define MI_BATCH_BUFFER_START  MI(0x31, 0)
#define   MI_BATCH_GTT         (2u << 6)    /* gen 5 */
#define   MI_BATCH_NON_SECURE  (1u << 8)    /* gen 6 - 7.5: the PPGTT, unprivileged */
#define   MI_BATCH_NON_SECURE_HSW (1u << 13)
#define PIPE_CONTROL(len)      (3u << 29 | 3u << 27 | 2u << 24 | ((len) - 2))
#define   PC_GLOBAL_GTT_IVB    (1u << 24)
#define   PC_CS_STALL          (1u << 20)
#define   PC_TLB_INVALIDATE    (1u << 18)
#define   PC_MEDIA_STATE_CLEAR (1u << 16)
#define   PC_QW_WRITE          (1u << 14)
#define   PC_RT_FLUSH          (1u << 12)
#define   PC_INSTRUCTION_INV   (1u << 11)
#define   PC_TEXTURE_INV       (1u << 10)
#define   PC_FLUSH_ENABLE      (1u << 7)
#define   PC_DC_FLUSH          (1u << 5)
#define   PC_VF_INV            (1u << 4)
#define   PC_CONST_INV         (1u << 3)
#define   PC_STATE_INV         (1u << 2)
#define   PC_STALL_AT_SB       (1u << 1)
#define   PC_DEPTH_FLUSH       (1u << 0)
#define   PC_GLOBAL_GTT        (1u << 2)    /* (gen 5, 6: in the address dword) */

#define RING_BYTES   (128u << 10)
#define HWS_SEQNO    0x40                   /* dword of the status page with the last seqno */
#define HWS_SCRATCH  0x80
#define HANG_MS      2000

static i915_hw_t H;
static int g_state;                         /* 0 none, 1 attached, 2 running, -1 failed */
static char g_why[160] = "no Intel GPU of gen 5 - 7.5";
static int g_gt1;                           /* Ivy Bridge GT1 (HD 2500) */
static int g_fw_legacy;                     /* Ivy Bridge without multi-threaded forcewake */

static volatile uint32_t* g_gsm;            /* the GTT's entries */
static uint64_t g_scratch_phys;
static uint32_t g_scratch_pte;
static uint8_t* g_used;                     /* the GTT's pages handed out (a bitmap) */
static uint32_t g_lo, g_hi, g_free, g_hint;
static uint32_t* g_pt;                      /* the aliasing PPGTT's 512 page tables (gen 6+) */
static uint32_t g_pd_first;                 /* the GTT entries that hold its directory */
static uint32_t g_swizzle;

static uint32_t* g_ring;
static uint32_t  g_ring_gtt, g_tail;
static volatile uint32_t* g_hws;
static uint32_t  g_hws_gtt, g_scratch_gtt;
static uint32_t  g_seqno;                   /* the last one emitted */
static uint32_t  g_resets;
static void (*g_reset_cb)(uint32_t, uint32_t);
/* hang detection: the seqno and command address seen last, and when they last moved */
static uint32_t g_seen_seq, g_seen_acthd, g_seen_ms;

static uint32_t rd(uint32_t r) { return r + 4 <= H.mmio_size ? *(volatile uint32_t*)(H.mmio + r) : 0; }
static void wr(uint32_t r, uint32_t v) { if (r + 4 <= H.mmio_size) *(volatile uint32_t*)(H.mmio + r) = v; }
static void rmw(uint32_t r, uint32_t clr, uint32_t set) { wr(r, (rd(r) & ~clr) | set); }

static int wait_reg(uint32_t r, uint32_t mask, uint32_t val, uint32_t ms) {
    uint32_t t0 = timer_ms();
    for (;;) {
        if ((rd(r) & mask) == val) return 0;
        if (timer_ms() - t0 > ms) return -1;
        __asm__ volatile("pause");
    }
}

static void clflush(const volatile void* p, uint32_t n) {
    uintptr_t a = (uintptr_t)p & ~(uintptr_t)63, e = (uintptr_t)p + n;
    for (; a < e; a += 64) __asm__ volatile("clflush (%0)" :: "r"(a) : "memory");
    __asm__ volatile("mfence" ::: "memory");
}

void i915_attach(const i915_hw_t* hw) {
    if (hw->gen < 5 || hw->gen > 7 || !hw->gtt_entries) return;
    H = *hw;
    g_gt1 = H.gen == 7 && !H.hsw && (H.devid == 0x0152 || H.devid == 0x0156 || H.devid == 0x015A);
    g_state = 1;
    kstrlcpy(g_why, "not started yet (the first app that uses the GPU starts it)", sizeof(g_why));
    klog("i915: gen %s render engine found (%u GTT entries, %u kept for the display) - started on first use\n",
         H.hsw ? "7.5" : H.gen == 7 ? "7" : H.gen == 6 ? "6" : "5", H.gtt_entries, H.reserved_lo + H.reserved_hi);
}

int i915_present(void) { return g_state != 0; }
int i915_running(void) { return g_state == 2; }
const char* i915_why(void) { return g_why; }
const i915_hw_t* i915_hw(void) { return &H; }
int i915_llc(void) { return H.gen >= 6; }
uint32_t i915_swizzle_x(void) { return g_swizzle; }
uint32_t i915_resets(void) { return g_resets; }
uint32_t i915_last_submitted(void) { return g_seqno; }
void i915_on_reset(void (*fn)(uint32_t, uint32_t)) { g_reset_cb = fn; }

static int fail(const char* what) {
    ksnprintf(g_why, sizeof(g_why), "%s (HEAD %08x TAIL %08x CTL %08x ACTHD %08x IPEHR %08x EIR %08x)",
              what, rd(R_HEAD), rd(R_TAIL), rd(R_CTL), rd(R_ACTHD), rd(R_IPEHR), rd(R_EIR));
    klog("i915: %s\n", g_why);
    g_state = -1;
    return -1;
}

/* ── forcewake: the GT kept awake (gen 6+) ─────────────────────────── */
static int forcewake_get(void) {
    if (H.gen < 6) return 0;
    if (H.gen == 6 || g_fw_legacy) {
        wait_reg(FORCEWAKE_ACK, 1, 0, 50);
        wr(FORCEWAKE, 1);
        (void)rd(ECOBUS);
        if (wait_reg(FORCEWAKE_ACK, 1, 1, 50)) return -1;
    } else {
        uint32_t ack = H.hsw ? FORCEWAKE_ACK_HSW : FORCEWAKE_MT_ACK;
        wait_reg(ack, 1, 0, 50);
        wr(FORCEWAKE_MT, MASKED_EN(1));
        (void)rd(ECOBUS);
        if (wait_reg(ack, 1, 1, 50)) return -1;
    }
    wait_reg(GT_THREAD_STATUS, 7, 0, 5);                /* the GT's threads awake (C0) */
    return 0;
}

/* Ivy Bridge: multi-threaded forcewake only where the firmware set it up */
static void forcewake_detect(void) {
    if (H.gen != 7 || H.hsw) return;
    wr(FORCEWAKE, 0);
    (void)rd(ECOBUS);
    wr(FORCEWAKE_MT, MASKED_EN(1));
    (void)rd(ECOBUS);
    wait_reg(FORCEWAKE_MT_ACK, 1, 1, 50);
    uint32_t eco = rd(ECOBUS);
    wr(FORCEWAKE_MT, MASKED_DIS(1));
    wait_reg(FORCEWAKE_MT_ACK, 1, 0, 50);
    g_fw_legacy = !(eco & (1u << 5));
}

/* ── the hardware fixes Linux's i915 applies (workarounds, clock gating) ── */
static void workarounds(void) {
    if (H.gen == 5) {
        wr(0x46020, rd(0x46020) | (1u << 18) | (1u << 1));    /* PCH_3DCGDIS0: MARIUNIT, SVSMUNIT */
        wr(0x46024, rd(0x46024) | (1u << 11));                 /* PCH_3DCGDIS1: VFMUNIT */
        wr(0x2120, MASKED_DIS(1u << 0));                       /* CACHE_MODE_0: no RC operational flush */
        wr(0x2120, MASKED_EN(1u << 8));                        /*   pipelined render flush off */
        wr(0x208C, MASKED_EN(1u << 14));                       /* _3D_CHICKEN2: WM read pipelined */
    } else if (H.gen == 6) {
        wr(0x9400, rd(0x9400) | (1u << 5) | (1u << 7));        /* UCGCTL1: BLB, CS clock gating off */
        wr(0x9404, (1u << 12) | (1u << 11));                   /* UCGCTL2: RCPB, RCC */
        wr(0x2084, MASKED_EN(1u << 10));                       /* _3D_CHICKEN: no HiZ planes with 4x MSAA */
        wr(0x2090, MASKED_EN((1u << 5) | (1u << 1)));          /* _3D_CHICKEN3: SF fast clip, pipelined attribute fetch off */
        wr(0x20D0, MASKED_FIELD((1u << 9) | (1u << 7), 1u << 9));  /* GT_MODE: WiZ hashing 16x4 */
        wr(0x2120, MASKED_DIS(1u << 0));                       /* CACHE_MODE_0: no RC operational flush */
        wr(0x2120, MASKED_DIS(1u << 5));                       /*   no LRA replacement in the STC */
        wr(R_GFX_MODE6, MASKED_EN(1u << 13));                  /* GFX_MODE: explicit TLB invalidation */
    } else {
        /* gen 7 / 7.5 */
        if (!H.hsw) {
            wr(0xE4F4, MASKED_EN(1u << 0));                    /* ROW_CHICKEN2: DOP clock gating off */
            if (!g_gt1) wr(0xF4F4, MASKED_EN(1u << 0));        /*   (and the second half of GT2) */
            wr(0x9404, 1u << 13);                              /* UCGCTL2: RCZ */
            rmw(0x900C, 3u << 21, 1u << 21);                   /* MBCUNIT_SNPCR: medium snoop priority */
            wr(0x7010, MASKED_DIS(1u << 10));                  /* COMMON_SLICE_CHICKEN1: RHWO optimisation */
            wr(0xB01C, 0x3C47FF8C);                            /* L3CNTLREG1, L3 chicken mode */
            wr(0xB030, 0x20000000);
            rmw(0xB034, 1u << 27, 0);                          /* L3SQCREG4: L3 serialisation */
            wr(0x2090, MASKED_EN(1u << 10));                   /* _3D_CHICKEN3: no early cull */
            rmw(0x20A0, 0x0077070, 0);                         /* FF_THREAD_MODE: hardware dispatch */
            if (g_gt1) wr(0xE100, MASKED_EN(1u << 3));         /* HALF_SLICE_CHICKEN1: single-port PSD */
        } else {
            wr(0xB038, 1u << 27);                              /* SCRATCH1: no L3 data atomics */
            wr(0xE49C, MASKED_EN(1u << 6));                    /* ROW_CHICKEN3: no L3 global atomics */
            rmw(0x20A0, 1u << 15, 0);                          /* FF_THREAD_MODE: VS ref count */
            wr(0xE184, MASKED_EN(1u << 9));                    /* HALF_SLICE_CHICKEN3: sample_c */
            wr(GAM_ECOCHK, rd(GAM_ECOCHK) | (1u << 6));        /* SOL/VF arbitration */
        }
        rmw(0x9030, 0, 1u << 11);                              /* SQ_CHICKEN_MBCUNIT_CONFIG */
        wr(R_MODE7, MASKED_EN((1u << 13) | (1u << 11)));       /* explicit TLB invalidation, replay mode */
        wr(0x7000, MASKED_DIS(1u << 0));                       /* CACHE_MODE_0: no RC operational flush */
        wr(0x7004, MASKED_EN(1u << 6));                        /* CACHE_MODE_1: no 4x2 subspan optimisation */
        wr(0x7008, MASKED_FIELD((1u << 9) | (1u << 7), 1u << 9));  /* GT_MODE: WiZ hashing 16x4 */
    }
    if (H.gen >= 6) {
        wr(R_MI_MODE, MASKED_EN(1u << 14));                    /* async flip performance mode off */
        wr(R_INSTPM, MASKED_EN(1u << 7));                      /* forced ordering */
    }
    if (H.gen <= 6) wr(R_MI_MODE, MASKED_EN(1u << 6));         /* timed single-vertex dispatch */
}

/* bit-6 swizzling of tiled surfaces: Ironlake always; Sandy / Ivy Bridge
 * and Haswell as the firmware set the display arbiter up (both the GPU's
 * registers made to agree) */
static void swizzling(void) {
    if (H.gen == 5) { g_swizzle = 2; return; }                 /* I915_BIT_6_SWIZZLE_9_10 */
    if (rd(DISP_ARB_CTL) & (1u << 13)) {
        g_swizzle = 2;
        wr(TILECTL, rd(TILECTL) | 1u);
        wr(ARB_MODE, MASKED_EN(H.gen == 6 ? 1u << 4 : 1u << 5));
    } else {
        g_swizzle = 0;
    }
}

/* ── the global GTT ────────────────────────────────────────────────── */
static uint32_t pte_of(uint64_t pa) {
    uint32_t lo = (uint32_t)(pa & 0xFFFFF000u);
    if (H.gen == 5) return lo | (uint32_t)((pa >> 28) & 0xF0u) | 1u;          /* uncached: no LLC */
    if (H.hsw) return lo | (uint32_t)((pa >> 28) & 0x7F0u) | 0x4u | 1u;       /* write-back in the LLC */
    return lo | (uint32_t)((pa >> 28) & 0xFF0u) | 0x4u | 1u;                  /* GEN6_PTE_CACHE_LLC */
}

static int used(uint32_t i) { return g_used[i >> 3] & (1u << (i & 7)); }
static void mark(uint32_t i, int on) { if (on) g_used[i >> 3] |= (uint8_t)(1u << (i & 7)); else g_used[i >> 3] &= (uint8_t)~(1u << (i & 7)); }

static void set_entry(uint32_t i, uint32_t e) {
    g_gsm[i] = e;
    if (g_pt && i < 512u * 1024u) g_pt[i] = e;                   /* the PPGTT: the same addresses */
}

static void gtt_flushed(void) {
    if (H.gen >= 6) { wr(GFX_FLSH_CNTL, 1); (void)rd(GFX_FLSH_CNTL); }
    else (void)g_gsm[H.gtt_entries - 1];
}

int64_t i915_gtt_alloc(uint32_t n, uint32_t align) {
    if (!n || !g_used) return -1;
    if (!align) align = 1;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t s = pass ? g_lo : (g_hint > g_lo ? g_hint : g_lo);
        uint32_t end = g_hi;
        s = (s + align - 1) & ~(align - 1);
        while (s + n <= end) {
            uint32_t k = 0;
            while (k < n && !used(s + k)) k++;
            if (k == n) {
                for (uint32_t i = 0; i < n; i++) mark(s + i, 1);
                g_free -= n;
                g_hint = s + n;
                return s;
            }
            s = (s + k + 1 + align - 1) & ~(align - 1);
        }
    }
    return -1;
}

void i915_gtt_free(uint32_t first, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (first + i < g_hi && used(first + i)) { mark(first + i, 0); g_free++; }
    if (first < g_hint) g_hint = first;
}

void i915_gtt_bind(uint32_t first, const uint64_t* phys, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) set_entry(first + i, pte_of(phys[i]));
    gtt_flushed();
}

void i915_gtt_unbind(uint32_t first, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) set_entry(first + i, g_scratch_pte);
    gtt_flushed();
}

uint64_t i915_gtt_size(void) { return (uint64_t)H.gtt_entries * 4096u; }
uint64_t i915_gtt_free_bytes(void) { return (uint64_t)g_free * 4096u; }

/* the GTT: every entry not the display's points at a page of zeros; then,
 * from gen 6, the aliasing PPGTT - 512 page tables mirroring it, their
 * directory written into 512 entries of the GTT itself (as i915 does) */
static int gtt_init(void) {
    g_gsm = (volatile uint32_t*)(H.mmio + (2u << 20));
    void* scratch = kmalloc_aligned(4096, 4096);
    if (!scratch) return -1;
    memset(scratch, 0, 4096);
    clflush(scratch, 4096);
    g_scratch_phys = (uint64_t)(uintptr_t)scratch;
    g_scratch_pte = pte_of(g_scratch_phys);
    g_lo = H.reserved_lo;
    g_hi = H.gtt_entries > H.reserved_hi ? H.gtt_entries - H.reserved_hi : 0;
    if (H.gen >= 6 && g_hi > 512u * 1024u) g_hi = 512u * 1024u;   /* (what the PPGTT covers: 2 GiB) */
    if (g_hi <= g_lo + 4096) return -1;
    g_used = (uint8_t*)kzalloc((g_hi + 7) / 8);
    if (!g_used) return -1;
    for (uint32_t i = 0; i < g_lo; i++) mark(i, 1);
    g_free = g_hi - g_lo;
    g_hint = g_lo;
    if (H.gen >= 6) {
        g_pt = (uint32_t*)kmalloc_aligned(512u * 4096u, 4096);
        if (!g_pt) return -1;
        for (uint32_t i = 0; i < 512u * 1024u; i++) g_pt[i] = g_scratch_pte;
        for (uint32_t i = 0; i < g_lo && i < 512u * 1024u; i++) g_pt[i] = g_gsm[i];   /* (the display's: as they are) */
    }
    for (uint32_t i = g_lo; i < g_hi; i++) g_gsm[i] = g_scratch_pte;
    if (H.gen >= 6) {
        int64_t pd = i915_gtt_alloc(512, 16);
        if (pd < 0) return -1;
        g_pd_first = (uint32_t)pd;
        for (uint32_t k = 0; k < 512; k++) {
            uint64_t pa = (uint64_t)(uintptr_t)g_pt + (uint64_t)k * 4096u;
            g_gsm[g_pd_first + k] = (uint32_t)(pa & 0xFFFFF000u) | (uint32_t)((pa >> 28) & 0xFF0u) | 1u;
        }
    }
    gtt_flushed();
    return 0;
}

/* a page (or n contiguous) of ours, mapped in the GTT */
static void* kernel_pages(uint32_t bytes, uint32_t* gtt) {
    uint32_t n = (bytes + 4095) / 4096;
    uint8_t* p = (uint8_t*)kmalloc_aligned(n * 4096u, 4096);
    if (!p) return NULL;
    memset(p, 0, n * 4096u);
    if (H.gen == 5) clflush(p, n * 4096u);
    int64_t at = i915_gtt_alloc(n, 16);
    if (at < 0) { kfree(p); return NULL; }
    for (uint32_t i = 0; i < n; i++) set_entry((uint32_t)at + i, pte_of((uint64_t)(uintptr_t)p + i * 4096u));
    gtt_flushed();
    *gtt = (uint32_t)at * 4096u;
    return p;
}

/* ── the ring ──────────────────────────────────────────────────────── */
static uint32_t hws_seqno(void) {
    if (H.gen == 5) clflush(&g_hws[HWS_SEQNO], 4);
    return g_hws[HWS_SEQNO];
}

int i915_done(uint32_t s) { return !s || g_state != 2 || (int32_t)(hws_seqno() - s) >= 0; }

static void ppgtt_enable(void) {
    if (H.gen == 6) {
        wr(GAC_ECO_BITS, rd(GAC_ECO_BITS) | (1u << 13) | (3u << 8));
        wr(GAB_CTL, rd(GAB_CTL) | (1u << 8));
        wr(GAM_ECOCHK, rd(GAM_ECOCHK) | (1u << 10) | (3u << 3));
        wr(R_GFX_MODE6, MASKED_EN(1u << 9));
    } else if (H.gen == 7) {
        wr(GAC_ECO_BITS, rd(GAC_ECO_BITS) | (3u << 8));
        uint32_t e = rd(GAM_ECOCHK);
        if (H.hsw) e |= 3u << 3;
        else e = (e | (1u << 3)) & ~(1u << 4);
        wr(GAM_ECOCHK, e);
    }
}

static int ring_start(void) {
    /* stop it, empty it */
    if (H.gen >= 5) { wr(R_MI_MODE, MASKED_EN(STOP_RING)); wait_reg(R_MI_MODE, MODE_IDLE, MODE_IDLE, 1000); }
    wr(R_HEAD, rd(R_TAIL));
    (void)rd(R_HEAD);
    wr(R_CTL, 0);
    (void)rd(R_CTL);
    wr(R_HEAD, 0);
    wr(R_TAIL, 0);
    if (rd(R_HEAD) & HEAD_ADDR) return -1;
    /* the status page */
    if (H.gen == 5) wr(R_HWS_PGA5, g_hws_gtt); else wr(R_HWS_PGA6, g_hws_gtt);
    (void)rd(H.gen == 5 ? R_HWS_PGA5 : R_HWS_PGA6);
    wr(R_HWSTAM, 0xFFFFFFFFu);
    if (H.gen >= 6) {
        wr(R_INSTPM, MASKED_EN((1u << 9) | (1u << 5)));       /* TLB invalidate, sync flush */
        wait_reg(R_INSTPM, 1u << 5, 0, 2);
    }
    (void)rd(R_HEAD);
    wr(R_START, g_ring_gtt);
    if (H.gen >= 6) {
        wr(R_PP_DCLV, 0xFFFFFFFFu);
        wr(R_PP_BASE, (g_pd_first * 4u) << 10);
        if (H.gen == 7) wr(R_MODE7, MASKED_EN(1u << 9));      /* PPGTT on */
    }
    g_tail = 0;
    wr(R_HEAD, 0);
    wr(R_TAIL, 0);
    (void)rd(R_TAIL);
    wr(R_CTL, ((RING_BYTES - 4096) & 0x1FF000u) | RING_VALID);
    if (wait_reg(R_CTL, RING_VALID, RING_VALID, 50)) return -1;
    wr(R_MI_MODE, MASKED_DIS(STOP_RING));
    return 0;
}

/* room for n dwords at the tail (it wraps with no-ops): 0, or -1 if the GPU is stuck */
static int ring_room(uint32_t n) {
    uint32_t bytes = n * 4u;
    uint32_t t0 = timer_ms();
    for (;;) {
        uint32_t head = rd(R_HEAD) & HEAD_ADDR;
        uint32_t need = bytes + (g_tail + bytes > RING_BYTES ? RING_BYTES - g_tail : 0);
        uint32_t space = (head - g_tail - 64u) & (RING_BYTES - 1);   /* (never full: 64 bytes apart) */
        if (space >= need) break;
        if (timer_ms() - t0 > HANG_MS) return -1;
        task_sleep_ms(1);
    }
    if (g_tail + bytes > RING_BYTES) {
        while (g_tail < RING_BYTES) g_ring[g_tail / 4] = MI_NOOP, g_tail += 4;
        if (H.gen == 5) clflush(g_ring + (RING_BYTES - 64) / 4, 64);
        g_tail = 0;
    }
    return 0;
}

static uint32_t g_emit_start;
static void out(uint32_t v) { g_ring[g_tail / 4] = v; g_tail += 4; }
static void advance(void) {
    if (g_tail & 7) out(MI_NOOP);
    if (H.gen == 5) clflush(g_ring + g_emit_start / 4, g_tail - g_emit_start);
    __asm__ volatile("sfence; mfence" ::: "memory");
    wr(R_TAIL, g_tail & (RING_BYTES - 1));
    if (g_tail == RING_BYTES) g_tail = 0;
}

/* invalidate the caches and TLBs before a batch */
static void emit_invalidate(void) {
    if (H.gen == 5) {
        out(MI_FLUSH | MI_EXE_FLUSH | MI_INVALIDATE_ISP);
        out(PIPE_CONTROL(4) | PC_QW_WRITE); out(g_scratch_gtt | PC_GLOBAL_GTT); out(0); out(0);
        for (int i = 0; i < 12; i++) out(MI_FLUSH);
        out(PIPE_CONTROL(4) | PC_QW_WRITE); out(g_scratch_gtt | PC_GLOBAL_GTT); out(0); out(0);
        out(MI_FLUSH | MI_EXE_FLUSH | MI_INVALIDATE_ISP);
    } else if (H.gen == 6) {
        /* (a post-sync write must come first: the Sandy Bridge workaround) */
        out(PIPE_CONTROL(5)); out(PC_CS_STALL | PC_STALL_AT_SB); out(g_scratch_gtt | PC_GLOBAL_GTT); out(0); out(0); out(MI_NOOP);
        out(PIPE_CONTROL(5)); out(PC_QW_WRITE); out(g_scratch_gtt | PC_GLOBAL_GTT); out(0); out(0); out(MI_NOOP);
        out(PIPE_CONTROL(4));
        out(PC_TLB_INVALIDATE | PC_INSTRUCTION_INV | PC_TEXTURE_INV | PC_VF_INV | PC_CONST_INV | PC_STATE_INV |
            PC_QW_WRITE | PC_CS_STALL);
        out(g_scratch_gtt | PC_GLOBAL_GTT); out(0);
    } else {
        out(PIPE_CONTROL(4)); out(PC_CS_STALL | PC_STALL_AT_SB); out(0); out(0);
        out(PIPE_CONTROL(4));
        out(PC_CS_STALL | PC_QW_WRITE | PC_GLOBAL_GTT_IVB | PC_TLB_INVALIDATE | PC_INSTRUCTION_INV | PC_TEXTURE_INV |
            PC_VF_INV | PC_CONST_INV | PC_STATE_INV | PC_MEDIA_STATE_CLEAR);
        out(g_scratch_gtt); out(0);
    }
}

/* flush the render caches, then the seqno to the status page */
static void emit_breadcrumb(uint32_t s) {
    uint32_t at = g_hws_gtt + HWS_SEQNO * 4u;
    if (H.gen == 5) {
        out(MI_FLUSH);
        for (int i = 0; i < 8; i++) { out(MI_STORE_DWORD_INDEX); out(HWS_SCRATCH * 4u); out(s); }
        for (int i = 0; i < 8; i++) { out(MI_STORE_DWORD_INDEX); out(HWS_SEQNO * 4u); out(s); }
        out(MI_USER_INTERRUPT);
    } else if (H.gen == 6) {
        out(PIPE_CONTROL(4)); out(PC_CS_STALL | PC_STALL_AT_SB); out(0); out(0);
        out(PIPE_CONTROL(4)); out(PC_QW_WRITE); out(g_scratch_gtt | PC_GLOBAL_GTT); out(0);
        out(PIPE_CONTROL(4)); out(PC_RT_FLUSH | PC_DEPTH_FLUSH | PC_DC_FLUSH | PC_QW_WRITE | PC_CS_STALL);
        out(at | PC_GLOBAL_GTT); out(s);
        out(MI_USER_INTERRUPT); out(MI_NOOP);
    } else {
        out(PIPE_CONTROL(4));
        out(PC_RT_FLUSH | PC_DEPTH_FLUSH | PC_DC_FLUSH | PC_FLUSH_ENABLE | PC_QW_WRITE | PC_GLOBAL_GTT_IVB | PC_CS_STALL);
        out(at); out(s);
        out(MI_USER_INTERRUPT); out(MI_NOOP);
    }
}

#define EMIT_MAX 96                          /* dwords: invalidate + start + breadcrumb, at most */

static uint32_t submit(uint32_t batch_gtt, int with_batch, int privileged) {
    if (ring_room(EMIT_MAX)) return 0;
    g_emit_start = g_tail;
    uint32_t s = ++g_seqno;
    if (!s) s = ++g_seqno;                   /* (0 means "nothing") */
    emit_invalidate();
    if (with_batch) {
        uint32_t un = privileged ? 0 : H.hsw ? MI_BATCH_NON_SECURE | MI_BATCH_NON_SECURE_HSW : MI_BATCH_NON_SECURE;
        out(MI_BATCH_BUFFER_START | (H.gen == 5 ? MI_BATCH_GTT : 0) | un);
        out(batch_gtt);
    }
    emit_breadcrumb(s);
    advance();
    return s;
}

/* ── a hung GPU ────────────────────────────────────────────────────── */
static int engine_reset(void) {
    int r;
    if (H.gen == 5) {
        wr(ILK_GDSR, (1u << 1) | 1u);                                /* render */
        r = wait_reg(ILK_GDSR, 1, 0, 500);
        if (!r) { wr(ILK_GDSR, (3u << 1) | 1u); r = wait_reg(ILK_GDSR, 1, 0, 500); }   /* media */
        wr(ILK_GDSR, 0);
        (void)rd(ILK_GDSR);
    } else {
        wr(GEN6_GDRST, 1u);                                         /* the whole GT */
        r = wait_reg(GEN6_GDRST, 1u, 0, 500);
        uint32_t t0 = timer_ms();
        while (timer_ms() - t0 < 2) __asm__ volatile("pause");
    }
    return r;
}

static int hw_setup(void);

static void hang(void) {
    uint32_t done = g_hws[HWS_SEQNO];
    klog("i915: the GPU hung at seqno %u of %u (HEAD %08x ACTHD %08x IPEHR %08x INSTDONE %08x EIR %08x ESR %08x) - reset\n",
         done, g_seqno, rd(R_HEAD), rd(R_ACTHD), rd(R_IPEHR), rd(R_INSTDONE), rd(R_EIR), rd(R_ESR));
    g_resets++;
    if (g_reset_cb) g_reset_cb(done, g_seqno);
    int ok = engine_reset() == 0 && hw_setup() == 0;
    g_hws[HWS_SEQNO] = g_seqno;                  /* what was lost counts as finished */
    if (H.gen == 5) clflush(&g_hws[HWS_SEQNO], 4);
    if (!ok) fail("the GPU did not come back from a reset");
    g_seen_ms = timer_ms();
}

/* no progress for HANG_MS while work is in flight: 1 (and the GPU was reset) */
static int hang_check(void) {
    uint32_t s = hws_seqno(), a = rd(R_ACTHD), now = timer_ms();
    if (s != g_seen_seq || a != g_seen_acthd || (int32_t)(s - g_seqno) >= 0) {
        g_seen_seq = s; g_seen_acthd = a; g_seen_ms = now;
        return 0;
    }
    if (now - g_seen_ms < HANG_MS) return 0;
    hang();
    return 1;
}

int i915_wait(uint32_t s, int timeout_ms) {
    uint32_t t0 = timer_ms();
    while (!i915_done(s)) {
        if (hang_check()) return -2;
        if (timeout_ms >= 0 && timer_ms() - t0 >= (uint32_t)timeout_ms) return -1;
        if (timeout_ms == 0) return -1;
        task_sleep_ms(1);
    }
    return 0;
}

static uint32_t submit_batch(uint32_t batch_gtt, int privileged) {
    if (g_state != 2) return 0;
    hang_check();
    if (g_state != 2) return 0;
    uint32_t s = submit(batch_gtt, 1, privileged);
    if (!s) { hang(); if (g_state == 2) s = submit(batch_gtt, 1, privileged); }
    return s;
}
uint32_t i915_submit(uint32_t batch_gtt) { return submit_batch(batch_gtt, 0); }
uint32_t i915_submit_privileged(uint32_t batch_gtt) { return submit_batch(batch_gtt, 1); }

uint64_t i915_timestamp(void) {
    if (g_state != 2) return 0;
    uint32_t hi = rd(R_TIMESTAMP + 4), lo = rd(R_TIMESTAMP), hi2 = rd(R_TIMESTAMP + 4);
    if (hi != hi2) lo = rd(R_TIMESTAMP);
    return (uint64_t)hi2 << 32 | lo;
}

/* everything but memory: forcewake, the fixes, the PPGTT, the ring (at
 * start and after a reset) */
static int hw_setup(void) {
    if (forcewake_get()) { kstrlcpy(g_why, "the GT did not wake up (forcewake)", sizeof(g_why)); return -1; }
    workarounds();
    swizzling();
    ppgtt_enable();
    if (ring_start()) return fail("the render ring did not start");
    /* nothing yet: the ring's first work, a flush and a seqno */
    uint32_t s = submit(0, 0, 0);
    if (!s) return fail("no room in a new ring");
    uint32_t t0 = timer_ms();
    while (!i915_done(s)) {
        if (timer_ms() - t0 > 500) return fail("the render ring does not run");
        task_sleep_ms(1);
    }
    return 0;
}

int i915_start(void) {
    if (g_state == 2) return 0;
    if (g_state != 1) return -1;
    forcewake_detect();
    if (gtt_init()) return fail("no memory for the GPU's address space");
    uint32_t ring_gtt, hws_gtt, scr_gtt;
    g_ring = (uint32_t*)kernel_pages(RING_BYTES, &ring_gtt);
    g_hws = (volatile uint32_t*)kernel_pages(4096, &hws_gtt);
    void* scr = kernel_pages(4096, &scr_gtt);
    if (!g_ring || !g_hws || !scr) return fail("no memory for the ring");
    g_ring_gtt = ring_gtt;
    g_hws_gtt = hws_gtt;
    g_scratch_gtt = scr_gtt;
    g_state = 2;                                   /* (submit() and i915_done() run from here) */
    if (hw_setup()) { g_state = -1; return -1; }
    g_seen_ms = timer_ms();
    klog("i915: render ring running (gen %s, %s forcewake, PPGTT %s, swizzle %s, %u MiB of GPU address space free)\n",
         H.hsw ? "7.5" : H.gen == 7 ? "7" : H.gen == 6 ? "6" : "5",
         H.gen < 6 ? "no" : g_fw_legacy || H.gen == 6 ? "legacy" : "multi-threaded",
         H.gen >= 6 ? "aliasing" : "none", g_swizzle ? "9/10" : "none", g_free / 256u);
    kstrlcpy(g_why, "running", sizeof(g_why));
    return 0;
}

void i915_info(char* buf, int cap) {
    if (g_state != 2) { ksnprintf(buf, (size_t)cap, "render engine: %s", g_why); return; }
    ksnprintf(buf, (size_t)cap,
              "render engine: running, seqno %u of %u, %u reset(s)\n"
              "ring HEAD %08x TAIL %08x ACTHD %08x, %u MiB of %u MiB of GPU address space free",
              hws_seqno(), g_seqno, g_resets, rd(R_HEAD), rd(R_TAIL), rd(R_ACTHD),
              g_free / 256u, (g_hi - g_lo) / 256u);
}

#else   /* the 32-bit kernel: no 3D */

void i915_attach(const i915_hw_t* hw) { (void)hw; }
int  i915_present(void) { return 0; }
int  i915_running(void) { return 0; }
int  i915_start(void) { return -1; }
const char* i915_why(void) { return "64-bit Banana OS only"; }
const i915_hw_t* i915_hw(void) { return NULL; }
int64_t  i915_gtt_alloc(uint32_t p, uint32_t a) { (void)p; (void)a; return -1; }
void     i915_gtt_free(uint32_t f, uint32_t p) { (void)f; (void)p; }
void     i915_gtt_bind(uint32_t f, const uint64_t* ph, uint32_t p) { (void)f; (void)ph; (void)p; }
void     i915_gtt_unbind(uint32_t f, uint32_t p) { (void)f; (void)p; }
uint64_t i915_gtt_size(void) { return 0; }
uint64_t i915_gtt_free_bytes(void) { return 0; }
int      i915_llc(void) { return 0; }
uint32_t i915_swizzle_x(void) { return 0; }
uint32_t i915_submit(uint32_t b) { (void)b; return 0; }
uint32_t i915_submit_privileged(uint32_t b) { (void)b; return 0; }
int      i915_done(uint32_t s) { (void)s; return 1; }
int      i915_wait(uint32_t s, int t) { (void)s; (void)t; return 0; }
uint32_t i915_last_submitted(void) { return 0; }
uint32_t i915_resets(void) { return 0; }
uint64_t i915_timestamp(void) { return 0; }
void     i915_info(char* buf, int cap) { ksnprintf(buf, (size_t)cap, "render engine: 64-bit Banana OS only"); }
void     i915_on_reset(void (*fn)(uint32_t, uint32_t)) { (void)fn; }

#endif
