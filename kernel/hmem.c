#include "hmem.h"
#include "kheap.h"
#include "paging.h"
#include "serial.h"

#define FRAME   0x200000ull
#define LOW     (4ull << 30)            /* below: the kernel heap's world */
#define HIGH    (512ull << 30)          /* an app's page tables reach this far */
#define RANGES  32

/* one stretch of RAM from the memory map, cut to whole frames; a bitmap
 * of the frames in use */
typedef struct {
    uint64_t base;
    uint32_t frames;
    uint32_t used;
    uint8_t* map;
} hrange_t;

static hrange_t g_r[RANGES];
static int      g_n;
static uint64_t g_total, g_used;

typedef struct { uint32_t type, size; } __attribute__((packed)) mb2_tag_t;
typedef struct { uint32_t type, size, entry_size, entry_version; } __attribute__((packed)) mb2_tag_mmap_t;
typedef struct { uint64_t base, length; uint32_t type, reserved; } __attribute__((packed)) mb2_mmap_entry_t;

#ifdef __x86_64__
static void add_range(uint64_t lo, uint64_t hi) {
    if (lo < LOW) lo = LOW;
    if (hi > HIGH) hi = HIGH;
    lo = (lo + FRAME - 1) & ~(FRAME - 1);
    hi &= ~(FRAME - 1);
    if (hi <= lo || g_n >= RANGES) return;
    uint32_t frames = (uint32_t)((hi - lo) / FRAME);
    uint8_t* map = (uint8_t*)kzalloc((frames + 7) / 8);
    if (!map || !paging_map_ram(lo, hi - lo)) { kfree(map); return; }
    g_r[g_n++] = (hrange_t){ lo, frames, 0, map };
    g_total += hi - lo;
}
#endif

void hmem_init(uint32_t mb2) {
#ifdef __x86_64__
    if (!mb2) return;
    uint32_t total = *(uint32_t*)(uintptr_t)mb2;
    for (uint32_t off = 8; off + 8 <= total; ) {
        mb2_tag_t* tag = (mb2_tag_t*)(uintptr_t)(mb2 + off);
        if (tag->type == 0) break;
        if (tag->type == 6) {
            mb2_tag_mmap_t* mm = (mb2_tag_mmap_t*)tag;
            for (uint32_t pos = sizeof(*mm); mm->entry_size && pos + mm->entry_size <= mm->size; pos += mm->entry_size) {
                mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)((uintptr_t)mm + pos);
                if (e->type == 1 && e->base + e->length > LOW) add_range(e->base, e->base + e->length);
            }
        }
        uint32_t adv = (tag->size + 7) & ~7u;
        off += adv < 8 ? 8 : adv;
    }
    if (g_total) klog("hmem: %llu MiB above 4 GiB for apps\n", (unsigned long long)(g_total >> 20));
#else
    (void)mb2;
#endif
}

static int used_at(const hrange_t* r, uint32_t i) { return (r->map[i >> 3] >> (i & 7)) & 1; }

static void mark(hrange_t* r, uint32_t i, uint32_t n, int used) {
    for (uint32_t k = i; k < i + n; k++) {
        if (used) r->map[k >> 3] |= (uint8_t)(1u << (k & 7));
        else r->map[k >> 3] &= (uint8_t)~(1u << (k & 7));
    }
}

uint64_t hmem_alloc(uint32_t n) {
    if (!n) return 0;
    for (int ri = 0; ri < g_n; ri++) {
        hrange_t* r = &g_r[ri];
        if (r->frames - r->used < n) continue;
        uint32_t len = 0;
        for (uint32_t i = 0; i < r->frames; i++) {
            if (used_at(r, i)) { len = 0; continue; }
            if (++len < n) continue;
            uint32_t first = i + 1 - n;
            mark(r, first, n, 1);
            r->used += n;
            g_used += (uint64_t)n * FRAME;
            return r->base + (uint64_t)first * FRAME;
        }
    }
    return 0;
}

void hmem_free(uint64_t addr, uint32_t n) {
    for (int ri = 0; ri < g_n; ri++) {
        hrange_t* r = &g_r[ri];
        if (addr < r->base || addr >= r->base + (uint64_t)r->frames * FRAME) continue;
        uint32_t first = (uint32_t)((addr - r->base) / FRAME);
        if (first + n > r->frames) return;
        mark(r, first, n, 0);
        r->used -= n;
        g_used -= (uint64_t)n * FRAME;
        return;
    }
}

uint64_t hmem_total_bytes(void) { return g_total; }
uint64_t hmem_used_bytes(void)  { return g_used; }
