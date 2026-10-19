#include "fpu.h"
#include "kstring.h"

/* XCR0: the state components xsave covers */
#define XF_X87    0x01ull
#define XF_SSE    0x02ull
#define XF_AVX    0x04ull               /* the upper halves of YMM0-15 */
#define XF_AVX512 0xE0ull               /* opmask, ZMM0-15's upper halves, ZMM16-31 */

#define CR4_OSFXSR     (1ul << 9)
#define CR4_OSXMMEXCPT (1ul << 10)
#define CR4_OSXSAVE    (1ul << 18)

enum { M_UNSET = 0, M_FNSAVE, M_FXSAVE, M_XSAVE };
static int      g_mode;
static uint64_t g_xcr0;                 /* M_XSAVE: what is saved */
static uint32_t g_size;
/* the registers a task starts with: x87 and SSE as after fninit / with
 * every SSE exception masked, everything else zero (the xsave header's
 * empty component bitmap makes xrstor reset AVX to zero too) */
static uint8_t  g_clean[FPU_AREA] __attribute__((aligned(64)));

static void cpuid(uint32_t leaf, uint32_t sub, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(sub));
}

static void xsetbv0(uint64_t v) {
    __asm__ volatile("xsetbv" :: "c"(0), "a"((uint32_t)v), "d"((uint32_t)(v >> 32)));
}

/* the bytes xsave needs for what XCR0 has on now */
static uint32_t xsave_size(void) {
    uint32_t a, b, c, d;
    cpuid(0xD, 0, &a, &b, &c, &d);
    return b;
}

/* the boot core, the first time: what to turn on */
static void decide(uint32_t c1, uint32_t d1) {
    g_mode = (d1 >> 24) & 1 ? M_FXSAVE : M_FNSAVE;          /* FXSR */
    int sse = ((d1 >> 24) & 1) && ((d1 >> 25) & 1);
    if (!sse || !((c1 >> 26) & 1) || !((c1 >> 28) & 1)) return;   /* no XSAVE or no AVX: fxsave */
    uint32_t a, b, c, d;
    cpuid(0, 0, &a, &b, &c, &d);
    if (a < 0xD) return;
    uint32_t max7 = a;
    cpuid(0xD, 0, &a, &b, &c, &d);
    uint64_t can = a | ((uint64_t)d << 32);
    uint64_t want = XF_X87 | XF_SSE | XF_AVX;
    if ((can & want) != want) return;
    if (max7 >= 7) {
        cpuid(7, 0, &a, &b, &c, &d);
        if (((b >> 16) & 1) && (can & XF_AVX512) == XF_AVX512) want |= XF_AVX512;   /* AVX512F */
    }
    uintptr_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 | CR4_OSXSAVE));
    xsetbv0(want);
    uint32_t size = xsave_size();
    if (size > FPU_AREA && (want & XF_AVX512)) {           /* (does not fit: AVX-512 stays off) */
        want &= ~XF_AVX512;
        xsetbv0(want);
        size = xsave_size();
    }
    if (size > FPU_AREA || size < 576) {                    /* (cannot happen: fxsave then) */
        xsetbv0(XF_X87 | XF_SSE);
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 & ~CR4_OSXSAVE));
        return;
    }
    g_mode = M_XSAVE;
    g_xcr0 = want;
    g_size = size;
}

void fpu_cpu_init(void) {
    uintptr_t cr0, cr4;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((uintptr_t)1 << 2 | (uintptr_t)1 << 3);       /* EM, TS */
    cr0 |= (uintptr_t)1 << 1 | (uintptr_t)1 << 5;          /* MP, NE: FPU errors reported natively */
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    __asm__ volatile("fninit");
    uint32_t a, b, c1, d1;
    cpuid(1, 0, &a, &b, &c1, &d1);
    int sse = ((d1 >> 24) & 1) && ((d1 >> 25) & 1);
    if (sse) {
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 | CR4_OSFXSR | CR4_OSXMMEXCPT));
    }
    if (g_mode == M_UNSET) {
        decide(c1, d1);
        /* the clean state (fxsave's layout, which xsave's begins with) */
        memset(g_clean, 0, sizeof(g_clean));
        *(uint16_t*)(g_clean + 0) = 0x037F;                 /* FCW: as fninit leaves it */
        *(uint32_t*)(g_clean + 24) = 0x1F80;                /* MXCSR: every SSE exception masked */
    } else if (g_mode == M_XSAVE) {
        /* another core, or this one after a wake: the same as the boot core */
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 | CR4_OSXSAVE));
        xsetbv0(g_xcr0);
    }
    if (sse) {
        uint32_t mxcsr = 0x1F80;
        __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));
    }
    if (g_mode == M_XSAVE) fpu_reset();                     /* (no stale AVX state from the firmware) */
}

void fpu_save(uint8_t* area) {
    switch (g_mode) {
    case M_XSAVE:
        __asm__ volatile("xsave (%0)" :: "r"(area), "a"((uint32_t)g_xcr0), "d"((uint32_t)(g_xcr0 >> 32)) : "memory");
        break;
    case M_FNSAVE:
        __asm__ volatile("fnsave (%0)" :: "r"(area) : "memory");
        break;
    default:
        __asm__ volatile("fxsave (%0)" :: "r"(area) : "memory");
    }
}

void fpu_restore(uint8_t* area) {
    switch (g_mode) {
    case M_XSAVE:
        __asm__ volatile("xrstor (%0)" :: "r"(area), "a"((uint32_t)g_xcr0), "d"((uint32_t)(g_xcr0 >> 32)) : "memory");
        break;
    case M_FNSAVE:
        __asm__ volatile("frstor (%0)" :: "r"(area) : "memory");
        break;
    default:
        __asm__ volatile("fxrstor (%0)" :: "r"(area) : "memory");
    }
}

void fpu_reset(void) {
    if (g_mode == M_FNSAVE || g_mode == M_UNSET) { __asm__ volatile("fninit"); return; }
    fpu_restore(g_clean);
}

int fpu_avx(void) { return g_mode == M_XSAVE && (g_xcr0 & XF_AVX); }

void fpu_describe(char* out, int cap) {
    if (g_mode == M_XSAVE)
        ksnprintf(out, (size_t)cap, "x87 SSE AVX%s (xsave, %u bytes a task)", (g_xcr0 & XF_AVX512) ? " AVX-512" : "", g_size);
    else if (g_mode == M_FXSAVE)
        ksnprintf(out, (size_t)cap, "x87 SSE (fxsave, 512 bytes a task)");
    else
        ksnprintf(out, (size_t)cap, "x87 (fnsave)");
}
