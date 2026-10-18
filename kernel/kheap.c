#include "kheap.h"
#include "kstring.h"
#include "smp.h"
#include "spinlock.h"

/* Every block (free or used) starts with this header; blocks tile the
 * whole heap in address order, so neighbours are found through prev/next
 * and merging on free is O(1). The header is 16 bytes, keeping payloads
 * 16-byte aligned. */
typedef struct block {
    uint32_t      size;   /* payload bytes (excludes header) */
    uint32_t      used;   /* 0 = free, HEAP_MAGIC = allocated */
    struct block* prev;
    struct block* next;
} __attribute__((aligned(16))) block_t;   /* 16 or 32 bytes: payloads stay 16-aligned */

#define HEAP_MAGIC 0xB16B00B5u
#define HDR        ((uint32_t)sizeof(block_t))
#define ALIGN      16u
#define MIN_SPLIT  64u   /* don't leave slivers smaller than this */

extern char _kernel_end[];  /* boot/linker.ld */

static block_t* g_head = NULL;
/* the heap is used on other cores too (apps' system calls there,
 * kernel/app.c) and from interrupt handlers: held with interrupts off */
static spinlock_t g_lock = SPINLOCK_INIT;
static uint32_t g_total = 0;
static uint32_t g_used = 0;

typedef struct { uint32_t type, size; } __attribute__((packed)) mb2_tag_t;
typedef struct {
    uint32_t type, size, entry_size, entry_version;
} __attribute__((packed)) mb2_tag_mmap_t;
typedef struct {
    uint64_t base, length;
    uint32_t type, reserved;
} __attribute__((packed)) mb2_mmap_entry_t;

static uint32_t align_up(uint32_t v, uint32_t a) { return (v + a - 1u) & ~(a - 1u); }

#define HEAP_MIN_ADDR 0x100000ull       /* below 1 MiB: BIOS/firmware leftovers */
#define HEAP_MAX_ADDR 0xE0000000ull     /* 3.5 GiB: stay clear of the 32-bit MMIO hole
                                         * (and inside the 64-bit kernel's 1:1 map) */

/* Largest piece of [lo, hi) that avoids [xlo, xhi): updates *best_lo/hi */
static void consider(uint64_t lo, uint64_t hi, uint64_t* best_lo, uint64_t* best_hi,
                     const uint64_t* xlo, const uint64_t* xhi, int nx) {
    if (lo < HEAP_MIN_ADDR) lo = HEAP_MIN_ADDR;
    if (hi > HEAP_MAX_ADDR) hi = HEAP_MAX_ADDR;
    if (hi <= lo) return;
    /* cut out the first excluded range that overlaps, recurse on both sides */
    for (int i = 0; i < nx; i++) {
        if (xhi[i] <= lo || xlo[i] >= hi) continue;
        consider(lo, xlo[i], best_lo, best_hi, xlo, xhi, nx);
        consider(xhi[i], hi, best_lo, best_hi, xlo, xhi, nx);
        return;
    }
    if (hi - lo > *best_hi - *best_lo) { *best_lo = lo; *best_hi = hi; }
}

/* The largest available RAM range from the Multiboot2 memory map that
 * does not overlap the kernel image or the Multiboot2 information.
 * (Firmware - UEFI in particular - splits memory into many regions; the
 * one the kernel was loaded into can be just a few MiB.) */
static int pick_region(uint32_t mb2, uint64_t* out_lo, uint64_t* out_hi) {
    if (!mb2) return 0;
    uint32_t total = *(uint32_t*)(uintptr_t)mb2;
    uint64_t xlo[2] = { 0x100000ull, mb2 };
    uint64_t xhi[2] = { align_up((uint32_t)(uintptr_t)_kernel_end, 4096u), (uint64_t)mb2 + total };
    uint64_t best_lo = 0, best_hi = 0;
    uint32_t off = 8;
    while (off + 8 <= total) {
        mb2_tag_t* tag = (mb2_tag_t*)(uintptr_t)(mb2 + off);
        if (tag->type == 0) break;
        if (tag->type == 6) {
            mb2_tag_mmap_t* mm = (mb2_tag_mmap_t*)tag;
            for (uint32_t pos = sizeof(*mm); pos + mm->entry_size <= mm->size; pos += mm->entry_size) {
                mb2_mmap_entry_t* e = (mb2_mmap_entry_t*)((uintptr_t)mm + pos);
                if (e->type != 1) continue;
                consider(e->base, e->base + e->length, &best_lo, &best_hi, xlo, xhi, 2);
            }
        }
        uint32_t adv = align_up(tag->size, 8);
        off += adv < 8 ? 8 : adv;
    }
    if (best_hi <= best_lo) return 0;
    *out_lo = best_lo;
    *out_hi = best_hi;
    return 1;
}

static void heap_bins_reset(struct block* first);

void kheap_init(uint32_t mb2_info_addr) {
    /* Every Multiboot2 consumer (fb.c, sysinfo.c) has copied what it needs
     * by now, but the info block is kept out of the heap anyway. */
    uint64_t lo, hi;
    uint32_t start, end;
    if (pick_region(mb2_info_addr, &lo, &hi) && hi - lo >= (1u << 20)) {
        start = align_up((uint32_t)lo, 4096u);
        end = (uint32_t)hi;
    } else {
        /* No usable map: right after the kernel, on a conservative 16 MiB machine. */
        start = align_up((uint32_t)(uintptr_t)_kernel_end, 4096u);
        end = 16u << 20;
    }
    end &= ~(ALIGN - 1u);

    g_head = (block_t*)(uintptr_t)start;
    g_head->size = end - start - HDR;
    g_head->used = 0;
    g_head->prev = NULL;
    g_head->next = NULL;
    g_total = g_head->size;
    g_used = 0;
    heap_bins_reset(g_head);
}

/* ── free blocks by size ─────────────────────────────────────────────
 * Each free block is also in the list of its size class (2^k .. 2^(k+1)-1
 * bytes), linked through its own payload: an allocation looks at the
 * classes that can hold it, not at every block of the heap (with the
 * block cache's thousands of 4 KiB blocks, walking them all - interrupts
 * off - made every kmalloc slower than a disk read). */

#define NBINS 32
typedef struct { block_t* prev; block_t* next; } links_t;
static block_t* g_bins[NBINS];

static links_t* L(block_t* b) { return (links_t*)((uint8_t*)b + HDR); }
static int bin_of(uint32_t size) { return size ? 31 - __builtin_clz(size) : 0; }

static void bin_insert(block_t* b) {
    int i = bin_of(b->size);
    links_t* l = L(b);
    l->prev = NULL;
    l->next = g_bins[i];
    if (g_bins[i]) L(g_bins[i])->prev = b;
    g_bins[i] = b;
}

static void heap_bins_reset(block_t* first) {
    for (int i = 0; i < NBINS; i++) g_bins[i] = NULL;
    bin_insert(first);
}

static void bin_remove(block_t* b) {
    links_t* l = L(b);
    if (l->prev) L(l->prev)->next = l->next; else g_bins[bin_of(b->size)] = l->next;
    if (l->next) L(l->next)->prev = l->prev;
}

/* b (out of the bins) cut to size: the rest a free block of its own */
static void split(block_t* b, uint32_t size) {
    if (b->size < size + HDR + MIN_SPLIT) return;
    block_t* n = (block_t*)((uint8_t*)b + HDR + size);
    n->size = b->size - size - HDR;
    n->used = 0;
    n->prev = b;
    n->next = b->next;
    if (b->next) b->next->prev = n;
    b->next = n;
    b->size = size;
    bin_insert(n);
}

/* b (out of the bins) and its following block, if that one is free */
static void merge_next(block_t* b) {
    block_t* n = b->next;
    if (!n || n->used) return;
    bin_remove(n);
    b->size += HDR + n->size;
    b->next = n->next;
    if (n->next) n->next->prev = b;
}

static void* take(block_t* b, uint32_t need) {
    bin_remove(b);
    split(b, need);
    b->used = HEAP_MAGIC;
    g_used += b->size;
    return (uint8_t*)b + HDR;
}

static void* find_fit(uint32_t need) {
    int i = bin_of(need);
    /* its own class: the blocks there may be smaller - a few looked at */
    int k = 0;
    for (block_t* b = g_bins[i]; b && k < 64; b = L(b)->next, k++)
        if (b->size >= need) return take(b, need);
    /* a bigger class: its first block will do */
    for (i++; i < NBINS; i++) if (g_bins[i]) return take(g_bins[i], need);
    /* (the class's other blocks, past the few looked at) */
    for (block_t* b = g_bins[bin_of(need)]; b; b = L(b)->next) if (b->size >= need) return take(b, need);
    return NULL;
}

/* what gives memory back when it runs out: files cached in RAM (fs.c) */
static uint32_t (*g_reclaim)(uint32_t need);
static int g_reclaiming;
void kheap_set_reclaim(uint32_t (*reclaim)(uint32_t need)) { g_reclaim = reclaim; }

/* only from ordinary kernel code on the boot core: not in an interrupt
 * handler (interrupts off), where the file tables may be half-changed */
static int may_reclaim(void) {
    uintptr_t fl;
    __asm__ volatile("pushf; pop %0" : "=r"(fl));
    return g_reclaim && !g_reclaiming && (fl & 0x200) && cpu_id() == 0;
}

void* kmalloc(size_t size) {
    if (!g_head || size == 0 || size > 0xF0000000u) return NULL;
    uint32_t need = align_up((uint32_t)size, ALIGN);
    uintptr_t f = spin_lock_irq(&g_lock);
    void* p = find_fit(need);
    spin_unlock_irq(&g_lock, f);
    if (!p && may_reclaim()) {
        /* (unlocked: dropping cached files frees heap blocks) */
        g_reclaiming = 1;
        g_reclaim(need + HDR);
        g_reclaiming = 0;
        f = spin_lock_irq(&g_lock);
        p = find_fit(need);
        spin_unlock_irq(&g_lock, f);
    }
    return p;
}

/* a free block holding `need` bytes that start at a multiple of `align`:
 * the part before that start stays a free block of its own */
static void* find_fit_aligned(uint32_t need, uint32_t align) {
    for (int i = bin_of(need); i < NBINS; i++) {
        for (block_t* b = g_bins[i]; b; b = L(b)->next) {
            if (b->size < need) continue;
            uintptr_t s = (uintptr_t)b + HDR, end = s + b->size;
            uintptr_t a = (s + align - 1) & ~(uintptr_t)(align - 1);
            /* room for the front block's header and a payload worth keeping */
            while (a != s && a - s < HDR + MIN_SPLIT) a += align;
            if (a + need > end) continue;
            bin_remove(b);
            if (a != s) {
                block_t* n = (block_t*)(a - HDR);
                n->size = (uint32_t)(end - a);
                n->used = 0;
                n->prev = b;
                n->next = b->next;
                if (b->next) b->next->prev = n;
                b->next = n;
                b->size = (uint32_t)((uintptr_t)n - s);
                bin_insert(b);                       /* (the front part: free) */
                b = n;
            }
            split(b, need);
            b->used = HEAP_MAGIC;
            g_used += b->size;
            return (uint8_t*)b + HDR;
        }
    }
    return NULL;
}

void* kmalloc_aligned(size_t size, size_t align) {
    if (align <= ALIGN) return kmalloc(size);
    if (!g_head || size == 0 || size > 0xF0000000u || (align & (align - 1))) return NULL;
    uint32_t need = align_up((uint32_t)size, ALIGN);
    uintptr_t f = spin_lock_irq(&g_lock);
    void* p = find_fit_aligned(need, (uint32_t)align);
    spin_unlock_irq(&g_lock, f);
    if (!p && may_reclaim()) {
        g_reclaiming = 1;
        g_reclaim(need + (uint32_t)align + HDR);
        g_reclaiming = 0;
        f = spin_lock_irq(&g_lock);
        p = find_fit_aligned(need, (uint32_t)align);
        spin_unlock_irq(&g_lock, f);
    }
    return p;
}

void* kzalloc(size_t size) {
    void* p = kmalloc(size);
    if (p) memset(p, 0, size);
    return p;
}

static block_t* hdr_of(void* p) {
    block_t* b = (block_t*)((uint8_t*)p - HDR);
    return (b->used == HEAP_MAGIC) ? b : NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    uintptr_t f = spin_lock_irq(&g_lock);
    block_t* b = hdr_of(ptr);
    if (b) {                    /* (else a double free or a garbage pointer: ignored) */
        g_used -= b->size;
        b->used = 0;
        merge_next(b);
        block_t* p = b->prev;
        if (p && !p->used) {                  /* (into the free block before it) */
            bin_remove(p);
            p->size += HDR + b->size;
            p->next = b->next;
            if (b->next) b->next->prev = p;
            b = p;
        }
        bin_insert(b);
    }
    spin_unlock_irq(&g_lock, f);
}

void* krealloc(void* ptr, size_t size) {
    if (!ptr) return kmalloc(size);
    if (size == 0) { kfree(ptr); return NULL; }
    uintptr_t f = spin_lock_irq(&g_lock);
    block_t* b = hdr_of(ptr);
    if (!b) { spin_unlock_irq(&g_lock, f); return NULL; }
    uint32_t need = align_up((uint32_t)size, ALIGN), old = b->size;
    if (need <= b->size) { spin_unlock_irq(&g_lock, f); return ptr; }

    /* grow in place by swallowing a free neighbour when possible: keeps
     * repeated appends (downloads, file writes) from copying every time */
    block_t* n = b->next;
    if (n && !n->used && b->size + HDR + n->size >= need) {
        g_used -= b->size;
        merge_next(b);
        split(b, need);
        g_used += b->size;
        spin_unlock_irq(&g_lock, f);
        return ptr;
    }
    spin_unlock_irq(&g_lock, f);

    void* np = kmalloc(size);
    if (!np) return NULL;
    memcpy(np, ptr, old);
    kfree(ptr);
    return np;
}

uint32_t kheap_total_bytes(void) { return g_total; }
uint32_t kheap_used_bytes(void)  { return g_used; }

uint32_t kheap_largest_free(void) {
    uint32_t best = 0;
    uintptr_t f = spin_lock_irq(&g_lock);
    for (int i = NBINS - 1; i >= 0 && !best; i--)           /* (the biggest class holding one) */
        for (block_t* b = g_bins[i]; b; b = L(b)->next) if (b->size > best) best = b->size;
    spin_unlock_irq(&g_lock, f);
    return best;
}

/* Memory handed to the heap later (the kernel's built-in packages, once
 * they are kept on the disk): a free block between two used markers of no
 * size - it is never merged with blocks that are not its neighbours - at
 * the end of the list. */
void kheap_add_region(uintptr_t lo, uintptr_t hi) {
    lo = (lo + ALIGN - 1) & ~(uintptr_t)(ALIGN - 1);
    hi &= ~(uintptr_t)(ALIGN - 1);
    if (!g_head || hi <= lo || hi - lo < 3u * HDR + MIN_SPLIT || hi - lo > 0xF0000000u) return;
    block_t* lead = (block_t*)lo;
    block_t* b = (block_t*)(lo + HDR);
    block_t* tail = (block_t*)(hi - HDR);
    lead->size = 0;
    lead->used = HEAP_MAGIC;
    b->size = (uint32_t)((uintptr_t)tail - (uintptr_t)b - HDR);
    b->used = 0;
    tail->size = 0;
    tail->used = HEAP_MAGIC;
    uintptr_t f = spin_lock_irq(&g_lock);
    block_t* t = g_head;
    while (t->next) t = t->next;
    t->next = lead;
    lead->prev = t;
    lead->next = b;
    b->prev = lead;
    b->next = tail;
    tail->prev = b;
    tail->next = NULL;
    bin_insert(b);
    g_total += b->size;
    spin_unlock_irq(&g_lock, f);
}
