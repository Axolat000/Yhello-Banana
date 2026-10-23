/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Intel graphics, gen 8 to 11: the render engine with execlists
 * (kernel/gpu_i915_lrc.h). Broadwell (5th generation Core), Skylake, Kaby
 * Lake, Coffee Lake and Comet Lake (6th - 10th, gen 9 / 9.5) and Ice Lake
 * (10th, gen 11).
 *
 * After the Intel PRMs and how Linux's i915 drives these GPUs (they have no
 * "legacy" ring on gen 11, and Linux has used execlists on all of them for
 * years):
 *  - the global GTT: 64-bit entries in the upper half of the register BAR;
 *    the kernel's own pages (rings, the status page, context images) are
 *    there;
 *  - each app has its own GPU address space: a 4-level, 48-bit PPGTT (Mesa's
 *    iris places its buffers in it itself - "softpin");
 *  - logical ring contexts: a context is an image (a per-process status
 *    page, then the registers as MI_LOAD_REGISTER_IMM lists, then the 3D
 *    state the GPU saves) with a ring buffer of its own; the image names the
 *    ring and the address space, and a 64-bit descriptor (its GTT address,
 *    48-bit addressing) is written to the execlist submit port (ELSP; the
 *    submit queue, ELSQ, on gen 11). The GPU switches between them itself
 *    and reports in the context status buffer (CSB, mirrored into the global
 *    status page) - one context is submitted at a time here, the next once
 *    the CSB says the engine went idle;
 *  - in each ring: an invalidation, MI_BATCH_BUFFER_START of the app's
 *    batch (its PPGTT), and a PIPE_CONTROL writing the batch's seqno into
 *    the status page - what is finished is read there;
 *  - forcewake (gen 9+: the render and GT domains), the private PAT, the
 *    hardware fixes Linux's i915 applies (the context ones written into a
 *    "golden" image every new context starts from);
 *  - a hung GPU (no progress for 2 s): a GT reset, every context back to the
 *    golden image (their lost batches reported).
 * Nothing here is touched until the first use; the 3D of these GPUs is
 * experimental (written without the hardware: tested against the GPU model
 * of tools/tests/test_i915.c). */
#include "gpu_i915.h"
#include "gpu_i915_lrc.h"
#include "kheap.h"
#include "kstring.h"
#include "serial.h"
#include "timer.h"
#include "task.h"

#ifdef __x86_64__

/* ── registers ─────────────────────────────────────────────────────── */
#define RCS              0x2000
#define R_TAIL           (RCS + 0x30)
#define R_HEAD           (RCS + 0x34)
#define R_START          (RCS + 0x38)
#define R_CTL            (RCS + 0x3C)
#define R_ACTHD_UDW      (RCS + 0x5C)
#define R_IPEHR          (RCS + 0x68)
#define R_INSTDONE       (RCS + 0x6C)
#define R_ACTHD          (RCS + 0x74)
#define R_HWS_PGA        (RCS + 0x80)
#define R_HWSTAM         (RCS + 0x98)
#define R_MI_MODE        (RCS + 0x9C)
#define R_EIR            (RCS + 0xB0)
#define R_ESR            (RCS + 0xB8)
#define R_PWR_CLK_STATE  (RCS + 0xC8)
#define R_RESET_CTL      (RCS + 0xD0)
#define R_BB_STATE       (RCS + 0x110)
#define R_SBB_ADDR       (RCS + 0x114)
#define R_SBB_STATE      (RCS + 0x118)
#define R_SBB_ADDR_UDW   (RCS + 0x11C)
#define R_BB_ADDR        (RCS + 0x140)
#define R_BB_ADDR_UDW    (RCS + 0x168)
#define R_BB_PER_CTX     (RCS + 0x1C0)
#define R_INDIRECT_CTX   (RCS + 0x1C4)
#define R_INDIRECT_OFF   (RCS + 0x1C8)
#define R_ELSP           (RCS + 0x230)
#define R_CTX_CONTROL    (RCS + 0x244)
#define R_PDP_LDW(n)     (RCS + 0x270 + (n) * 8)
#define R_PDP_UDW(n)     (RCS + 0x274 + (n) * 8)
#define R_MODE           (RCS + 0x29C)          /* RING_MODE_GEN7 */
#define R_TIMESTAMP      (RCS + 0x358)
#define R_CSB_PTR        (RCS + 0x3A0)
#define R_CTX_TIMESTAMP  (RCS + 0x3A8)
#define R_ELSQ           (RCS + 0x510)          /* gen 11: the submit queue */
#define R_EL_CONTROL     (RCS + 0x550)
#define STOP_RING        (1u << 8)
#define MODE_IDLE        (1u << 9)
#define RUN_LIST_ENABLE  (1u << 15)
#define DISABLE_LEGACY   (1u << 3)              /* gen 11 */
#define RESET_REQUEST    (1u << 0)
#define READY_TO_RESET   (1u << 1)
#define BB_PPGTT         (1u << 5)
#define GFX_FLSH_CNTL    0x101008
#define GEN6_GDRST       0x941C

#define FORCEWAKE_MT        0xA188              /* gen 8; on gen 9+ the GT ("blitter") domain */
#define FORCEWAKE_ACK_MT    0x130044
#define FORCEWAKE_RENDER9   0xA278
#define FORCEWAKE_ACK_RENDER9 0x0D84

#define PAT_LO           0x40E0                 /* gen 8 / 9; gen 11: eight registers from here */
#define PAT_HI           0x40E4
#define CTC_MODE         0xA26C
#define RPM_CONFIG0      0x0D00
#define TS_OVERRIDE      0x44074

#define MASKED_EN(b)     ((uint32_t)(b) << 16 | (uint32_t)(b))
#define MASKED_DIS(b)    ((uint32_t)(b) << 16)
#define MASKED_FIELD(m, v) ((uint32_t)(m) << 16 | (uint32_t)(v))

/* ── commands ──────────────────────────────────────────────────────── */
#define MI(op, flags)          ((uint32_t)(op) << 23 | (uint32_t)(flags))
#define MI_NOOP                0u
#define MI_USER_INTERRUPT      MI(0x02, 0)
#define MI_ARB_ON_OFF          MI(0x08, 0)
#define MI_ARB_ENABLE          1u
#define MI_BATCH_BUFFER_END    MI(0x0A, 0)
#define MI_LRI(n)              MI(0x22, 2 * (n) - 1)
#define MI_LRI_POSTED          (1u << 12)
#define MI_BB_START_GEN8       MI(0x31, 1)
#define MI_BB_PPGTT            (1u << 8)
#define PIPE_CONTROL6          (3u << 29 | 3u << 27 | 2u << 24 | 4u)
#define PC_GLOBAL_GTT          (1u << 24)
#define PC_CS_STALL            (1u << 20)
#define PC_TLB_INVALIDATE      (1u << 18)
#define PC_QW_WRITE            (1u << 14)
#define PC_RT_FLUSH            (1u << 12)
#define PC_INSTRUCTION_INV     (1u << 11)
#define PC_TEXTURE_INV         (1u << 10)
#define PC_FLUSH_ENABLE        (1u << 7)
#define PC_DC_FLUSH            (1u << 5)
#define PC_VF_INV              (1u << 4)
#define PC_CONST_INV           (1u << 3)
#define PC_STATE_INV           (1u << 2)
#define PC_DEPTH_FLUSH         (1u << 0)

/* ── a context image (gen 8 - 11): page 0 the per-process status page,
 * page 1 the registers, from here (dword indexes) ── */
#define LRC_PAGES        24                    /* (gen 8: 20, gen 9: 22, gen 11: 14 - and room) */
#define LRC_REGS         1024u                 /* the register page's first dword */
#define CTX_LRI_0        0x01
#define CTX_CONTEXT_CONTROL 0x02
#define CTX_RING_HEAD    0x04
#define CTX_RING_TAIL    0x06
#define CTX_RING_START   0x08
#define CTX_RING_CTL     0x0A
#define CTX_BB_STATE     0x10
#define CTX_INDIRECT_OFF 0x1C
#define CTX_LRI_1        0x21
#define CTX_PDP0_UDW     0x30
#define CTX_PDP0_LDW     0x32
#define CTX_LRI_2        0x41
#define CTX_PWR_CLK      0x42
#define CTX_END          0x44
#define CTRL_RESTORE_INHIBIT (1u << 0)
#define CTRL_RS_CTX_ENABLE   (1u << 1)
#define CTRL_SAVE_INHIBIT    (1u << 2)
#define CTRL_INHIBIT_SYN     (1u << 3)

/* the descriptor */
#define DESC_VALID       (1ull << 0)
#define DESC_L3LLC       (1ull << 5)
#define DESC_PRIVILEGE   (1ull << 8)
#define DESC_64B         (3ull << 3)           /* legacy 64-bit (4-level) addressing */

/* the CSB: in the global status page from dword 0x10, two dwords an entry */
#define HWS_CSB          0x10
#define HWS_SEQNO        0x80                  /* (dword) the last seqno, as on the older GPUs: our own place */
#define CSB_IDLE_ACTIVE  (1u << 0)
#define CSB_ACTIVE_IDLE  (1u << 3)
#define CSB_COMPLETE     (1u << 4)

#define RING_BYTES       (32u << 10)
#define HANG_MS          2000
#define LRC_MAX          1024
#define VM_MAX           256

static i915_hw_t H;
static int g_state;                            /* 0 none, 1 attached, 2 running, -1 failed */
static char g_why[160] = "no Intel GPU of gen 8 - 11";

static volatile uint64_t* g_gsm;               /* the GTT's entries */
static uint64_t g_scratch_pte;
static uint8_t* g_used;
static uint32_t g_lo, g_hi, g_free, g_hint;

static volatile uint32_t* g_hws;              /* the global status page */
static uint32_t g_hws_gtt, g_scratch_gtt;
static uint32_t g_seqno;
static uint32_t g_csb_head, g_csb_n, g_csb_wr; /* CSB entries, the write pointer's dword */

/* address spaces: 4-level page tables; 0 is the kernel's (it mirrors what
 * the kernel binds in the GTT, at the same addresses) */
typedef struct { uint64_t* pml4; uint32_t contexts; uint8_t used, dying; } vm_t;
static vm_t g_vm[VM_MAX];

typedef struct {
    uint32_t* img;                             /* LRC_PAGES of it */
    uint32_t  img_gtt;
    uint32_t* ring;
    uint32_t  ring_gtt, tail;
    int       vm;
    uint32_t  last;                            /* its last seqno */
    uint8_t   used, dying, fresh, queued;      /* fresh: nothing to restore (the GPU's state as at start) */
} lrc_t;
static lrc_t*  g_lrc;
static uint32_t* g_golden;                     /* every new context's image: the state with the fixes applied */
static int     g_inflight = -1;                /* the context the GPU runs (-1: idle) */
static int16_t g_queue[LRC_MAX];
static int     g_qh, g_qn;
static uint32_t g_resets, g_submits;
static void (*g_reset_cb)(uint32_t, uint32_t);
static uint32_t g_seen_seq, g_seen_ms;
static uint64_t g_seen_acthd;

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

static const char* gen_name(void) {
    return H.gen == 8 ? "8" : H.gen == 9 ? "9" : H.gen == 11 ? "11" : "?";
}

static int fail(const char* what) {
    ksnprintf(g_why, sizeof(g_why), "%s (ACTHD %08x IPEHR %08x EIR %08x ESR %08x CSB %08x)",
              what, rd(R_ACTHD), rd(R_IPEHR), rd(R_EIR), rd(R_ESR), rd(R_CSB_PTR));
    klog("i915: %s\n", g_why);
    g_state = -1;
    return -1;
}

void lrc_attach(const i915_hw_t* hw) {
    if (hw->gen < 8 || hw->gen > 11 || hw->gen == 10 || !hw->gtt_entries) return;
    H = *hw;
    g_state = 1;
    kstrlcpy(g_why, "not started yet (the first app that uses the GPU starts it)", sizeof(g_why));
    klog("i915: gen %s render engine found (%u GTT entries, %u kept for the display) - started on first use (experimental)\n",
         gen_name(), H.gtt_entries, H.reserved_lo + H.reserved_hi);
}

int lrc_present(void) { return g_state != 0; }
int lrc_running(void) { return g_state == 2; }
const char* lrc_why(void) { return g_why; }
uint32_t lrc_resets(void) { return g_resets; }
uint32_t lrc_last_submitted(void) { return g_seqno; }
void lrc_on_reset(void (*fn)(uint32_t, uint32_t)) { g_reset_cb = fn; }

/* ── forcewake: the render and GT power wells kept awake ────────────── */
static int forcewake_get(void) {
    if (H.gen == 8) {
        wait_reg(FORCEWAKE_ACK_MT, 1, 0, 50);
        wr(FORCEWAKE_MT, MASKED_EN(1));
        return wait_reg(FORCEWAKE_ACK_MT, 1, 1, 50);
    }
    wait_reg(FORCEWAKE_ACK_RENDER9, 1, 0, 50);
    wait_reg(FORCEWAKE_ACK_MT, 1, 0, 50);
    wr(FORCEWAKE_RENDER9, MASKED_EN(1));
    wr(FORCEWAKE_MT, MASKED_EN(1));
    if (wait_reg(FORCEWAKE_ACK_RENDER9, 1, 1, 50)) return -1;
    return wait_reg(FORCEWAKE_ACK_MT, 1, 1, 50);
}

/* ── the private PAT: entry 0 write-back in the LLC (what the GTT's and the
 * PPGTTs' entries use), as Linux's i915 sets it up ── */
static void ppat_setup(void) {
    /* (cache: 0 UC, 1 WC, 2 WT, 3 WB; target: 2 << 2 the LLC, 3 << 2 LLC + eLLC; age << 4) */
    static const uint8_t pat[8] = { 3 | 2 << 2, 1 | 3 << 2, 2 | 3 << 2, 0, 3 | 3 << 2, 3 | 3 << 2 | 1 << 4, 3 | 3 << 2 | 2 << 4, 3 | 3 << 2 | 3 << 4 };
    if (H.gen >= 11) {
        for (int i = 0; i < 8; i++) wr(PAT_LO + (uint32_t)i * 4u, pat[i]);
        return;
    }
    uint32_t lo = 0, hi = 0;
    for (int i = 0; i < 4; i++) { lo |= (uint32_t)pat[i] << (i * 8); hi |= (uint32_t)pat[i + 4] << (i * 8); }
    wr(PAT_LO, lo);
    wr(PAT_HI, hi);
}

/* ── the hardware fixes (workarounds) Linux's i915 applies: the GT's and
 * the engine's in registers; the context ones (lrc_ctx_fixes) go into the
 * golden image ── */
static void gt_fixes(void) {
    if (H.gen == 8) {
        rmw(0x9430, 0, 1u << 14);                               /* GEN8_UCGCTL6: SDE unit clock gating off */
    } else if (H.gen == 9) {
        rmw(0xB004, 0, 1u << 7);                                /* GEN8_GARBCNTL: GAPS TSV credit fix */
        rmw(0x940C, 0, 1u << 14);                               /* GEN7_UCGCTL4: EU GA unit clock gating off */
        rmw(0x4AB0, 0, 1u << 18);                               /* GAMT_ECO: in-place decompression */
    } else {
        rmw(0x94D4, 0, 1u << 16);                               /* SUBSLICE_UNIT_LEVEL_CLKGATE: GW unit */
        rmw(0x94E4, 0, 1u << 5);                                /* UNSLICE_UNIT_LEVEL_CLKGATE2: PSD unit */
        wr(0x20EC, MASKED_EN(1u << 1));                         /* CS_DEBUG_MODE1: FF DOP clock gating off */
        rmw(0x20A0, 0, 1u << 19);                               /* FF_THREAD_MODE: DS ref count */
    }
    wr(R_MI_MODE, MASKED_EN(1u << 14));                         /* async flip performance mode off */
}

/* (register, masked value) pairs for the golden image */
static int lrc_ctx_fixes(uint32_t* out) {
    int n = 0;
#define FIX(r, v) (out[n++] = (r), out[n++] = (v))
    if (H.gen == 8) {
        FIX(0xE4F0, MASKED_EN(1u << 8));                        /* ROW_CHICKEN: partial instruction shootdown off */
        FIX(0x7300, MASKED_EN(1u << 4));                        /* HDC_CHICKEN0: don't fetch memory when masked */
        FIX(0x7000, MASKED_EN(1u << 2));                        /* CACHE_MODE_0: HiZ RAW stall optimisation off */
        FIX(0x7004, MASKED_EN(1u << 6));                        /* CACHE_MODE_1: 4x4 STC optimisation off */
        FIX(0x7008, MASKED_FIELD((1u << 9) | (1u << 7), 1u << 9));   /* GT_MODE: WiZ hashing 16x4 */
    } else if (H.gen == 9) {
        FIX(0xE4F0, MASKED_EN((1u << 15) | (1u << 8)));         /* ROW_CHICKEN: flow control, partial shootdown off */
        FIX(0xE194, MASKED_EN((1u << 4) | (1u << 2)));          /* HALF_SLICE_CHICKEN7: YV12 fix, GPGPU preemption */
        FIX(0x7004, MASKED_EN((1u << 6) | (1u << 1)));          /* CACHE_MODE_1: 4x4 STC, partial resolve in VC off */
        FIX(0xE188, MASKED_DIS(1u << 3));                       /* HALF_SLICE_CHICKEN5: CCS TLB prefetch off */
        FIX(0x7300, MASKED_EN((1u << 5) | (1u << 11)));         /* HDC_CHICKEN0: context save non-coherent */
    } else {
        FIX(0x7300, MASKED_EN(1u << 4));                        /* HDC_CHICKEN0: force non-coherent */
        FIX(0xE420, MASKED_EN(1u << 4));                        /* CACHE_MODE_SS: float blend optimisation */
        FIX(0x2580, MASKED_FIELD(3u << 1, 1u << 1));            /* CS_CHICKEN1: GPGPU preemption at thread-group level */
        FIX(0xE18C, MASKED_EN(1u << 5));                        /* SAMPLER_MODE: headerless messages */
        FIX(0xE48C, MASKED_EN(1u << 7));                        /* ROW_CHICKEN4: don't pick the 2nd EU */
    }
#undef FIX
    return n / 2;
}

/* ── the global GTT (64-bit entries) ───────────────────────────────── */
static uint64_t pte_of(uint64_t pa) { return (pa & 0x7FFFFFF000ull) | 1u; }     /* (PAT 0: WB in the LLC) */

static int used(uint32_t i) { return g_used[i >> 3] & (1u << (i & 7)); }
static void mark(uint32_t i, int on) { if (on) g_used[i >> 3] |= (uint8_t)(1u << (i & 7)); else g_used[i >> 3] &= (uint8_t)~(1u << (i & 7)); }
static void gtt_flushed(void) { wr(GFX_FLSH_CNTL, 1); (void)rd(GFX_FLSH_CNTL); }

int64_t lrc_gtt_alloc(uint32_t n, uint32_t align) {
    if (!n || !g_used) return -1;
    if (!align) align = 1;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t s = pass ? g_lo : (g_hint > g_lo ? g_hint : g_lo);
        s = (s + align - 1) & ~(align - 1);
        while (s + n <= g_hi) {
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

void lrc_gtt_free(uint32_t first, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (first + i < g_hi && used(first + i)) { mark(first + i, 0); g_free++; }
    if (first < g_hint) g_hint = first;
}

int lrc_vm_bind(int vm, uint64_t addr, const uint64_t* phys, uint32_t n);
void lrc_vm_unbind(int vm, uint64_t addr, const uint64_t* phys, uint32_t n);

/* (the kernel's address space mirrors the GTT: its tests' batches run from there) */
void lrc_gtt_bind(uint32_t first, const uint64_t* phys, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) g_gsm[first + i] = pte_of(phys[i]);
    gtt_flushed();
    lrc_vm_bind(0, (uint64_t)first * 4096u, phys, n);
}

void lrc_gtt_unbind(uint32_t first, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) g_gsm[first + i] = g_scratch_pte;
    gtt_flushed();
    lrc_vm_unbind(0, (uint64_t)first * 4096u, NULL, n);
}

uint64_t lrc_gtt_size(void) { return (uint64_t)H.gtt_entries * 4096u; }
uint64_t lrc_gtt_free_bytes(void) { return (uint64_t)g_free * 4096u; }

static int gtt_init(void) {
    g_gsm = (volatile uint64_t*)(H.mmio + H.mmio_size / 2);
    void* scratch = kmalloc_aligned(4096, 4096);
    if (!scratch) return -1;
    memset(scratch, 0, 4096);
    g_scratch_pte = pte_of((uint64_t)(uintptr_t)scratch);
    g_lo = H.reserved_lo;
    g_hi = H.gtt_entries > H.reserved_hi ? H.gtt_entries - H.reserved_hi : 0;
    if (g_hi > 1u << 20) g_hi = 1u << 20;                       /* (4 GiB: what 32-bit GTT addresses reach) */
    if (g_hi <= g_lo + 4096) return -1;
    g_used = (uint8_t*)kzalloc((g_hi + 7) / 8);
    if (!g_used) return -1;
    for (uint32_t i = 0; i < g_lo; i++) mark(i, 1);
    g_free = g_hi - g_lo;
    g_hint = g_lo;
    for (uint32_t i = g_lo; i < g_hi; i++) g_gsm[i] = g_scratch_pte;
    gtt_flushed();
    return 0;
}

static void* kernel_pages(uint32_t bytes, uint32_t* gtt) {
    uint32_t n = (bytes + 4095) / 4096;
    uint8_t* p = (uint8_t*)kmalloc_aligned(n * 4096u, 4096);
    if (!p) return NULL;
    memset(p, 0, n * 4096u);
    int64_t at = lrc_gtt_alloc(n, 16);
    if (at < 0) { kfree(p); return NULL; }
    for (uint32_t i = 0; i < n; i++) g_gsm[(uint32_t)at + i] = pte_of((uint64_t)(uintptr_t)p + i * 4096u);
    gtt_flushed();
    *gtt = (uint32_t)at * 4096u;
    return p;
}

/* ── address spaces: 48-bit, 4 levels of 512 entries ───────────────── */
#define PDE_BITS 3u                                    /* present, writable */

int lrc_vm_create(void) {
    for (int i = 1; i < VM_MAX; i++) {
        if (g_vm[i].used) continue;
        uint64_t* p = (uint64_t*)kmalloc_aligned(4096, 4096);
        if (!p) return -1;
        memset(p, 0, 4096);
        g_vm[i] = (vm_t){ p, 0, 1, 0 };
        return i;
    }
    return -1;
}

static void table_free(uint64_t* t, int level) {
    if (level > 1)
        for (int i = 0; i < 512; i++)
            if (t[i] & 1) table_free((uint64_t*)(uintptr_t)(t[i] & 0x7FFFFFF000ull), level - 1);
    kfree(t);
}

static void vm_free(int vm) {
    table_free(g_vm[vm].pml4, 4);
    memset(&g_vm[vm], 0, sizeof(g_vm[vm]));
}

/* its contexts gone: the tables go once the GPU is done with them */
void lrc_vm_destroy(int vm) {
    if (vm <= 0 || vm >= VM_MAX || !g_vm[vm].used) return;
    g_vm[vm].dying = 1;
    if (!g_vm[vm].contexts) vm_free(vm);
}

uint64_t lrc_vm_size(void) { return 1ull << 48; }

/* the page-table entry of addr (its tables made where missing, if make) */
static uint64_t* vm_pte(int vm, uint64_t addr, int make) {
    uint64_t* t = g_vm[vm].pml4;
    for (int level = 4; level > 1; level--) {
        uint32_t i = (uint32_t)(addr >> (12 + 9 * (level - 1))) & 511u;
        if (!(t[i] & 1)) {
            if (!make) return NULL;
            uint64_t* n = (uint64_t*)kmalloc_aligned(4096, 4096);
            if (!n) return NULL;
            memset(n, 0, 4096);
            t[i] = (uint64_t)(uintptr_t)n | PDE_BITS;
        }
        t = (uint64_t*)(uintptr_t)(t[i] & 0x7FFFFFF000ull);
    }
    return &t[(addr >> 12) & 511u];
}

int lrc_vm_bind(int vm, uint64_t addr, const uint64_t* phys, uint32_t n) {
    if (vm < 0 || vm >= VM_MAX || !g_vm[vm].used || (addr & 4095) || addr + (uint64_t)n * 4096u > (1ull << 48)) return -1;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t* e = vm_pte(vm, addr + (uint64_t)i * 4096u, 1);
        if (!e) return -1;
        *e = (phys[i] & 0x7FFFFFF000ull) | PDE_BITS;            /* (PAT 0: WB in the LLC) */
    }
    __asm__ volatile("sfence" ::: "memory");
    return 0;
}

/* what is still these pages' (phys NULL: whatever is there) back to nothing */
void lrc_vm_unbind(int vm, uint64_t addr, const uint64_t* phys, uint32_t n) {
    if (vm < 0 || vm >= VM_MAX || !g_vm[vm].used) return;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t* e = vm_pte(vm, addr + (uint64_t)i * 4096u, 0);
        if (e && (!phys || (*e & 0x7FFFFFF000ull) == (phys[i] & 0x7FFFFFF000ull))) *e = 0;
    }
}

/* ── the GPU's state: the status page and the CSB ──────────────────── */
static uint32_t hws_seqno(void) { return g_hws[HWS_SEQNO]; }

static void csb_reset(void) {
    g_csb_n = H.gen >= 11 ? 12 : 6;
    g_csb_wr = H.gen >= 11 ? 0x2F : 0x1F;
    g_csb_head = g_csb_n - 1;
    g_hws[g_csb_wr] = g_csb_n - 1;
    uint32_t m = H.gen >= 11 ? 0xF : 0x7;
    wr(R_CSB_PTR, MASKED_FIELD(m << 8, (g_csb_n - 1) << 8) | MASKED_FIELD(m, g_csb_n - 1));
    (void)rd(R_CSB_PTR);
}

static void kick(void);

/* what the GPU reported: the context it ran went idle (its image saved) */
static void csb_process(void) {
    uint32_t tail = g_hws[g_csb_wr];
    if (tail >= g_csb_n) return;                       /* (not written yet since the reset) */
    while (g_csb_head != tail) {
        g_csb_head = (g_csb_head + 1) % g_csb_n;
        uint32_t st = g_hws[HWS_CSB + g_csb_head * 2];
        if (st & CSB_ACTIVE_IDLE) g_inflight = -1;
    }
}

int lrc_done(uint32_t s) {
    if (!s || g_state != 2) return 1;
    if ((int32_t)(hws_seqno() - s) >= 0) return 1;
    kick();
    return (int32_t)(hws_seqno() - s) >= 0;
}

/* ── contexts ──────────────────────────────────────────────────────── */
static uint64_t descriptor(int c) {
    uint64_t d = (uint64_t)g_lrc[c].img_gtt | DESC_64B | DESC_VALID | DESC_PRIVILEGE;
    if (H.gen == 8) d |= DESC_L3LLC;
    if (H.gen >= 11) d |= (uint64_t)(c + 1) << 37;      /* (the software context id; class / instance 0: render) */
    else d |= (uint64_t)(c + 1) << 32;
    return d;
}

static uint32_t indirect_default(void) { return H.gen == 8 ? 0x17u : H.gen == 9 ? 0x26u : 0x1Au; }

/* the register page: the ring, the address space (built from scratch for
 * the first context; later ones start from the golden image) */
static void regs_init(int c, int from_scratch) {
    lrc_t* x = &g_lrc[c];
    uint32_t* r = x->img + LRC_REGS;
    if (from_scratch) {
        static const uint16_t ring_regs[14] = { 0x244, 0x34, 0x30, 0x38, 0x3C, 0x168, 0x140, 0x110, 0x11C, 0x114, 0x118, 0x1C0, 0x1C4, 0x1C8 };
        static const uint16_t pdp_regs[9] = { 0x3A8, 0x28C, 0x288, 0x284, 0x280, 0x27C, 0x278, 0x274, 0x270 };
        memset(r, 0, 4096);
        r[0] = MI_NOOP;
        r[CTX_LRI_0] = MI_LRI(14) | MI_LRI_POSTED;
        for (int i = 0; i < 14; i++) r[CTX_LRI_0 + 1 + 2 * i] = RCS + ring_regs[i];
        r[CTX_LRI_1] = MI_LRI(9) | MI_LRI_POSTED;
        for (int i = 0; i < 9; i++) r[CTX_LRI_1 + 1 + 2 * i] = RCS + pdp_regs[i];
        r[CTX_LRI_2] = MI_LRI(1);
        r[CTX_PWR_CLK] = R_PWR_CLK_STATE;
        r[CTX_END] = MI_BATCH_BUFFER_END | (H.gen >= 11 ? 1u : 0u);
    }
    for (int i = 0; i < 14; i++) if (i != 0 && i != 7 && i != 13) r[CTX_LRI_0 + 2 + 2 * i] = 0;
    r[CTX_RING_HEAD + 1] = 0;
    r[CTX_RING_TAIL + 1] = 0;
    r[CTX_RING_START + 1] = x->ring_gtt;
    r[CTX_RING_CTL + 1] = ((RING_BYTES - 4096u) & 0x1FF000u) | 1u;
    r[CTX_BB_STATE + 1] = BB_PPGTT;
    r[CTX_INDIRECT_OFF + 1] = indirect_default() << 6;
    uint64_t pml4 = (uint64_t)(uintptr_t)g_vm[x->vm].pml4;
    for (int i = 0; i < 4; i++) { r[CTX_PDP0_UDW + 1 - 4 * i] = 0; r[CTX_PDP0_LDW + 1 - 4 * i] = 0; }
    r[CTX_PDP0_UDW + 1] = (uint32_t)(pml4 >> 32);
    r[CTX_PDP0_LDW + 1] = (uint32_t)pml4;
    r[CTX_PWR_CLK + 1] = 0;
}

static int lrc_new(int vm) {
    if (!g_lrc || vm < 0 || vm >= VM_MAX || !g_vm[vm].used || g_vm[vm].dying) return -1;
    for (int i = 0; i < LRC_MAX; i++) {
        if (g_lrc[i].used) continue;
        uint32_t ig, rg;
        uint32_t* img = (uint32_t*)kernel_pages(LRC_PAGES * 4096u, &ig);
        if (!img) return -1;
        uint32_t* ring = (uint32_t*)kernel_pages(RING_BYTES, &rg);
        if (!ring) { lrc_gtt_free(ig / 4096u, LRC_PAGES); kfree(img); return -1; }
        g_lrc[i] = (lrc_t){ img, ig, ring, rg, 0, vm, 0, 1, 0, g_golden == NULL, 0 };
        if (g_golden) memcpy(img, g_golden, LRC_PAGES * 4096u);
        memset(img, 0, 4096);                                   /* (its own status page) */
        regs_init(i, g_golden == NULL);
        g_vm[vm].contexts++;
        return i;
    }
    return -1;
}

int lrc_ctx_create(int vm) { return lrc_new(vm); }

static void lrc_release(int c) {
    lrc_t* x = &g_lrc[c];
    for (uint32_t i = 0; i < LRC_PAGES; i++) g_gsm[x->img_gtt / 4096u + i] = g_scratch_pte;
    for (uint32_t i = 0; i < RING_BYTES / 4096u; i++) g_gsm[x->ring_gtt / 4096u + i] = g_scratch_pte;
    gtt_flushed();
    lrc_gtt_free(x->img_gtt / 4096u, LRC_PAGES);
    lrc_gtt_free(x->ring_gtt / 4096u, RING_BYTES / 4096u);
    kfree(x->img);
    kfree(x->ring);
    int vm = x->vm;
    memset(x, 0, sizeof(*x));
    if (--g_vm[vm].contexts == 0 && g_vm[vm].dying) vm_free(vm);
}

static void reap(void) {
    for (int i = 1; i < LRC_MAX; i++)
        if (g_lrc[i].used && g_lrc[i].dying && i != g_inflight && !g_lrc[i].queued && lrc_done(g_lrc[i].last)) lrc_release(i);
}

void lrc_ctx_destroy(int c) {
    if (!g_lrc || c <= 0 || c >= LRC_MAX || !g_lrc[c].used) return;
    g_lrc[c].dying = 1;
    reap();
}

/* ── submission ────────────────────────────────────────────────────── */
static void elsp_write(uint64_t d) {
    if (H.gen >= 11) {
        wr(R_ELSQ, (uint32_t)d);
        wr(R_ELSQ + 4, (uint32_t)(d >> 32));
        wr(R_EL_CONTROL, 1u);                                   /* EL_CTRL_LOAD */
    } else {
        wr(R_ELSP, 0); wr(R_ELSP, 0);                            /* (port 1: none) */
        wr(R_ELSP, (uint32_t)(d >> 32));
        wr(R_ELSP, (uint32_t)d);
    }
}

/* the engine idle and work queued: the next context to the GPU (its ring's
 * tail and whether to restore it written into its image first) */
static void kick(void) {
    if (g_state != 2) return;
    csb_process();
    if (g_inflight >= 0) return;
    while (g_qn) {
        int c = g_queue[g_qh];
        g_qh = (g_qh + 1) % LRC_MAX;
        g_qn--;
        lrc_t* x = &g_lrc[c];
        x->queued = 0;
        if (!x->used) continue;
        uint32_t* r = x->img + LRC_REGS;
        r[CTX_RING_TAIL + 1] = x->tail;
        uint32_t ctl = MASKED_EN(CTRL_INHIBIT_SYN) | MASKED_DIS(CTRL_RESTORE_INHIBIT);
        if (H.gen < 11) ctl |= MASKED_DIS(CTRL_SAVE_INHIBIT | CTRL_RS_CTX_ENABLE);
        if (x->fresh) ctl |= MASKED_EN(CTRL_RESTORE_INHIBIT);
        r[CTX_CONTEXT_CONTROL + 1] = ctl;
        x->fresh = 0;
        __asm__ volatile("sfence; mfence" ::: "memory");
        g_inflight = c;
        elsp_write(descriptor(c));
        return;
    }
}

static void enqueue(int c) {
    if (g_lrc[c].queued) return;
    g_lrc[c].queued = 1;
    g_queue[(g_qh + g_qn) % LRC_MAX] = (int16_t)c;
    g_qn++;
}

static int hang_check(void);

/* room for n dwords in context c's ring (it wraps with no-ops): 0, or -1 if the GPU is stuck */
static int ring_room(int c, uint32_t n) {
    lrc_t* x = &g_lrc[c];
    uint32_t bytes = n * 4u;
    if (x->tail + bytes <= RING_BYTES - 64) return 0;
    /* (the end of the ring: once everything in it is done, from the start again) */
    uint32_t t0 = timer_ms();
    while (!lrc_done(x->last) || g_inflight == c) {          /* (and switched out: its image saved) */
        kick();
        if (hang_check()) return -1;
        if (timer_ms() - t0 > HANG_MS * 2) return -1;
        task_sleep_ms(1);
    }
    while (x->tail < RING_BYTES) x->ring[x->tail / 4] = MI_NOOP, x->tail += 4;
    x->tail = 0;
    x->img[LRC_REGS + CTX_RING_HEAD + 1] = 0;                   /* (idle: it starts over at 0) */
    return 0;
}

static uint32_t* g_out;
static void out(uint32_t v) { *g_out++ = v; }

static void emit_pc(uint32_t flags, uint32_t addr, uint32_t data) {
    out(PIPE_CONTROL6); out(flags); out(addr); out(0); out(data); out(0);
}

#define EMIT_MAX 160

static uint32_t submit_in(int c, uint64_t batch, int with_batch, int privileged, const uint32_t* lri, int nlri) {
    if (!g_lrc || c < 0 || c >= LRC_MAX || !g_lrc[c].used) return 0;
    if (ring_room(c, EMIT_MAX + (uint32_t)nlri * 2u)) return 0;
    lrc_t* x = &g_lrc[c];
    uint32_t s = ++g_seqno;
    if (!s) s = ++g_seqno;
    g_out = x->ring + x->tail / 4;
    uint32_t* start = g_out;
    /* invalidate the caches and TLBs (gen 9: a PIPE_CONTROL with no flags first, before the VF invalidation) */
    if (H.gen == 9) emit_pc(0, 0, 0);
    emit_pc(PC_CS_STALL | PC_TLB_INVALIDATE | PC_INSTRUCTION_INV | PC_TEXTURE_INV | PC_VF_INV | PC_CONST_INV |
            PC_STATE_INV | PC_QW_WRITE | PC_GLOBAL_GTT, g_scratch_gtt, 0);
    for (int i = 0; i < nlri; i++) { out(MI_LRI(1)); out(lri[2 * i]); out(lri[2 * i + 1]); }
    if (with_batch) {
        out(MI_ARB_ON_OFF | MI_ARB_ENABLE);
        out(MI_BB_START_GEN8 | (privileged ? 0 : MI_BB_PPGTT));
        out((uint32_t)batch);
        out((uint32_t)(batch >> 32));
    }
    /* flush the render caches, then the seqno to the status page */
    emit_pc(PC_RT_FLUSH | PC_DEPTH_FLUSH | PC_DC_FLUSH | PC_CS_STALL | PC_QW_WRITE | PC_GLOBAL_GTT,
            g_hws_gtt + HWS_SEQNO * 4u, s);
    emit_pc(PC_FLUSH_ENABLE | PC_CS_STALL, 0, 0);
    out(MI_USER_INTERRUPT);
    out(MI_ARB_ON_OFF | MI_ARB_ENABLE);
    if ((g_out - x->ring) & 1) out(MI_NOOP);                    /* (the tail: 8 bytes aligned) */
    x->tail += (uint32_t)(g_out - start) * 4u;
    x->last = s;
    g_submits++;
    __asm__ volatile("sfence" ::: "memory");
    enqueue(c);
    kick();
    return s;
}

/* ── a hung GPU ────────────────────────────────────────────────────── */
static int hw_setup(void);

static int gt_reset(void) {
    wr(R_RESET_CTL, MASKED_EN(RESET_REQUEST));
    wait_reg(R_RESET_CTL, READY_TO_RESET, READY_TO_RESET, 2);
    wr(GEN6_GDRST, 1u);                                         /* the whole GT */
    int r = wait_reg(GEN6_GDRST, 1u, 0, 500);
    wr(R_RESET_CTL, MASKED_DIS(RESET_REQUEST));
    uint32_t t0 = timer_ms();
    while (timer_ms() - t0 < 2) __asm__ volatile("pause");
    return r;
}

static void hang(void) {
    uint32_t done = hws_seqno();
    klog("i915: the GPU hung at seqno %u of %u (context %d, ACTHD %08x%08x IPEHR %08x INSTDONE %08x EIR %08x ESR %08x) - reset\n",
         done, g_seqno, g_inflight, rd(R_ACTHD_UDW), rd(R_ACTHD), rd(R_IPEHR), rd(R_INSTDONE), rd(R_EIR), rd(R_ESR));
    g_resets++;
    if (g_reset_cb) g_reset_cb(done, g_seqno);
    /* every context: its ring empty, its state the golden image again */
    g_inflight = -1;
    g_qn = 0;
    for (int i = 0; i < LRC_MAX; i++) {
        lrc_t* x = &g_lrc[i];
        if (!x->used) continue;
        x->queued = 0;
        x->tail = 0;
        if (g_golden) { memcpy(x->img + 1024, g_golden + 1024, (LRC_PAGES - 1) * 4096u); x->fresh = 0; }
        else x->fresh = 1;
        regs_init(i, g_golden == NULL);
    }
    int ok = gt_reset() == 0 && hw_setup() == 0;
    g_hws[HWS_SEQNO] = g_seqno;                                 /* what was lost counts as finished */
    if (!ok) fail("the GPU did not come back from a reset");
    g_seen_ms = timer_ms();
}

static int hang_check(void) {
    uint32_t s = hws_seqno(), now = timer_ms();
    uint64_t a = (uint64_t)rd(R_ACTHD_UDW) << 32 | rd(R_ACTHD);
    if (s != g_seen_seq || a != g_seen_acthd || (int32_t)(s - g_seqno) >= 0) {
        g_seen_seq = s; g_seen_acthd = a; g_seen_ms = now;
        return 0;
    }
    if (now - g_seen_ms < HANG_MS) return 0;
    hang();
    return 1;
}

int lrc_wait(uint32_t s, int timeout_ms) {
    uint32_t t0 = timer_ms();
    while (!lrc_done(s)) {
        if (hang_check()) return -2;
        if (timeout_ms >= 0 && timer_ms() - t0 >= (uint32_t)timeout_ms) return -1;
        if (timeout_ms == 0) return -1;
        task_sleep_ms(1);
    }
    return 0;
}

static uint32_t submit_batch(int c, uint64_t batch, int privileged) {
    if (g_state != 2) return 0;
    hang_check();
    if (g_state != 2) return 0;
    reap();
    uint32_t s = submit_in(c, batch, 1, privileged, NULL, 0);
    if (!s && g_state == 2) { hang(); if (g_state == 2) s = submit_in(c, batch, 1, privileged, NULL, 0); }
    return s;
}
uint32_t lrc_submit_ctx(int c, uint64_t batch) { return submit_batch(c, batch, 0); }
uint32_t lrc_submit(uint64_t batch) { return submit_batch(0, batch, 0); }
uint32_t lrc_submit_privileged(uint64_t batch) { return submit_batch(0, batch, 1); }

uint64_t lrc_timestamp(void) {
    if (g_state != 2) return 0;
    uint32_t hi = rd(R_TIMESTAMP + 4), lo = rd(R_TIMESTAMP), hi2 = rd(R_TIMESTAMP + 4);
    if (hi != hi2) lo = rd(R_TIMESTAMP);
    return (uint64_t)hi2 << 32 | lo;
}

/* the command streamer's clock, as Linux's i915 works it out */
uint32_t lrc_timestamp_hz(void) {
    if (H.gen == 8) return 12500000;
    uint32_t ctc = rd(CTC_MODE);
    if (ctc & 1u) {                                             /* (the "divide logic" source) */
        uint32_t o = rd(TS_OVERRIDE);
        return ((o & 0x3FFu) + 1u) * 1000000u + 1000000u / (((o >> 12) & 0xFu) + 1u);
    }
    if (H.gen == 9) return 24000000u >> (3 - ((ctc >> 1) & 3u));
    uint32_t c0 = rd(RPM_CONFIG0);
    static const uint32_t crystal[4] = { 24000000, 19200000, 38400000, 25000000 };
    return crystal[(c0 >> 3) & 3u] >> (3 - ((c0 >> 1) & 3u));
}

/* ── start ─────────────────────────────────────────────────────────── */
static int engine_start(void) {
    wr(R_HWSTAM, 0xFFFFFFFFu);
    wr(R_MODE, H.gen >= 11 ? MASKED_EN(DISABLE_LEGACY) : MASKED_EN(RUN_LIST_ENABLE));
    wr(R_MI_MODE, MASKED_DIS(STOP_RING));
    wr(R_HWS_PGA, g_hws_gtt);
    (void)rd(R_HWS_PGA);
    csb_reset();
    g_inflight = -1;
    return 0;
}

static int hw_setup(void) {
    if (forcewake_get()) { kstrlcpy(g_why, "the GT did not wake up (forcewake)", sizeof(g_why)); return -1; }
    ppat_setup();
    gt_fixes();
    if (engine_start()) return fail("the render engine did not start");
    if (!g_lrc) {
        g_lrc = (lrc_t*)kzalloc(sizeof(lrc_t) * LRC_MAX);
        if (!g_lrc) return fail("no memory for contexts");
        g_vm[0].pml4 = (uint64_t*)kmalloc_aligned(4096, 4096);
        if (!g_vm[0].pml4) return fail("no memory for page tables");
        memset(g_vm[0].pml4, 0, 4096);
        g_vm[0].used = 1;
        if (lrc_new(0) != 0) return fail("no memory for the kernel's context");
    }
    /* the kernel's context first: from nothing, the context fixes written in
     * its ring; its image once saved is the golden one */
    uint32_t fx[32];
    int nf = g_golden ? 0 : lrc_ctx_fixes(fx);
    uint32_t s = submit_in(0, 0, 0, 1, fx, nf);
    if (!s) return fail("no room in a new ring");
    uint32_t t0 = timer_ms();
    for (;;) {
        kick();
        if (lrc_done(s) && g_inflight < 0) break;
        if (timer_ms() - t0 > 500) return fail(lrc_done(s) ? "the render engine does not go idle" : "the render engine does not run");
        task_sleep_ms(1);
    }
    if (!g_golden) {
        g_golden = (uint32_t*)kmalloc(LRC_PAGES * 4096u);
        if (g_golden) memcpy(g_golden, g_lrc[0].img, LRC_PAGES * 4096u);
    }
    return 0;
}

int lrc_start(void) {
    if (g_state == 2) return 0;
    if (g_state != 1) return -1;
    if (gtt_init()) return fail("no memory for the GPU's address space");
    uint32_t hws_gtt, scr_gtt;
    g_hws = (volatile uint32_t*)kernel_pages(4096, &hws_gtt);
    void* scr = kernel_pages(4096, &scr_gtt);
    if (!g_hws || !scr) return fail("no memory for the status page");
    g_hws_gtt = hws_gtt;
    g_scratch_gtt = scr_gtt;
    g_state = 2;
    if (hw_setup()) { g_state = -1; return -1; }
    g_seen_ms = timer_ms();
    klog("i915: render engine running (gen %s, execlists, 48-bit PPGTT, golden context %s, timestamps %u Hz, %u MiB of GTT free)\n",
         gen_name(), g_golden ? "saved" : "none", lrc_timestamp_hz(), g_free / 256u);
    kstrlcpy(g_why, "running", sizeof(g_why));
    return 0;
}

void lrc_info(char* buf, int cap) {
    if (g_state != 2) { ksnprintf(buf, (size_t)cap, "render engine: %s", g_why); return; }
    int vms = 0, ctxs = 0;
    for (int i = 0; i < VM_MAX; i++) vms += g_vm[i].used;
    for (int i = 0; i < LRC_MAX; i++) ctxs += g_lrc[i].used;
    ksnprintf(buf, (size_t)cap,
              "render engine: running (execlists), seqno %u of %u, %u reset(s), %u submissions\n"
              "%d contexts, %d address spaces; ACTHD %08x CSB %08x; %u MiB of %u MiB of GTT free",
              hws_seqno(), g_seqno, g_resets, g_submits, ctxs, vms, rd(R_ACTHD), rd(R_CSB_PTR),
              g_free / 256u, (g_hi - g_lo) / 256u);
}

/* ── the GPU's makeup: slices, subslices, EUs (from its fuses, as Linux's
 * i915 reads them) ── */
void lrc_topology(i915_topology_t* t) {
    memset(t, 0, sizeof(*t));
    if (H.gen == 8) {
        t->max_slices = 3; t->max_subslices = 3; t->max_eus = 8;
        uint32_t f2 = rd(0x9120), d0 = rd(0x9134), d1 = rd(0x9138), d2 = rd(0x913C);
        t->slice_mask = (f2 >> 25) & 7u;
        uint32_t ss = ~(f2 >> 21) & 7u;
        uint32_t eud[3] = { d0 & 0xFFFFFFu, (d0 >> 24) | (d1 & 0xFFFFu) << 8, (d1 >> 16) | (d2 & 0xFFu) << 16 };
        for (int s = 0; s < 3; s++) {
            t->subslice_mask[s] = (t->slice_mask >> s) & 1 ? (uint8_t)ss : 0;
            for (int k = 0; k < 3; k++) t->eu_mask[s][k] = (t->subslice_mask[s] >> k) & 1 ? (uint8_t)~(eud[s] >> (k * 8)) : 0;
        }
    } else if (H.gen == 9) {
        t->max_slices = 3; t->max_subslices = 4; t->max_eus = 8;
        uint32_t f2 = rd(0x9120);
        t->slice_mask = (f2 >> 25) & 7u;
        uint32_t ss = ~(f2 >> 20) & 0xFu;
        for (int s = 0; s < 3; s++) {
            uint32_t eud = rd(0x9134 + (uint32_t)s * 4u);
            t->subslice_mask[s] = (t->slice_mask >> s) & 1 ? (uint8_t)ss : 0;
            for (int k = 0; k < 4; k++) t->eu_mask[s][k] = (t->subslice_mask[s] >> k) & 1 ? (uint8_t)~(eud >> (k * 8)) : 0;
        }
    } else {
        t->max_slices = 1; t->max_subslices = 8; t->max_eus = 8;
        t->slice_mask = rd(0x9138) & 1u;
        uint32_t ss = ~rd(0x913C) & 0xFFu, eu = ~rd(0x9134) & 0xFFu;          /* (the same EUs in every subslice) */
        t->subslice_mask[0] = t->slice_mask ? (uint8_t)ss : 0;
        for (int k = 0; k < 8; k++) t->eu_mask[0][k] = (t->subslice_mask[0] >> k) & 1 ? (uint8_t)eu : 0;
    }
    /* (fuses that read as nothing - no GPU there at all: a whole first slice) */
    if (!t->slice_mask || !t->subslice_mask[0]) {
        t->slice_mask = 1;
        t->subslice_mask[0] = (uint8_t)((1u << t->max_subslices) - 1);
        for (int k = 0; k < t->max_subslices; k++) t->eu_mask[0][k] = 0xFF;
    }
    for (int s = 0; s < t->max_slices; s++)
        for (int k = 0; k < t->max_subslices; k++) t->eu_total += (uint32_t)__builtin_popcount(t->eu_mask[s][k]);
}

#endif
