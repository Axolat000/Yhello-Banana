/* Banana OS SDK: malloc, free, realloc, calloc and aligned allocation.
 *
 * 64-bit: process-local first-fit split/coalesce arena allocator backed by
 * vm_map / vm_unmap (RW mappings only, no MAP_FIXED, no TLS caches).
 *  - A single static banana_mutex_t (g_lock) serializes all allocator state,
 *    including lazy capability detection.
 *  - Small allocations share 64 KiB arenas; free blocks are split on
 *    allocation and coalesced with physical neighbors on free.
 *  - Dedicated large mappings (> 1 arena) are unmapped as soon as freed (no
 *    large-mapping cache). While live, large arenas are marked is_large so
 *    unrelated small allocations cannot pin them.
 *  - Nonempty arenas are never unmapped. When an arena's live block count
 *    reaches zero, the whole arena is unmapped via vm_unmap. If vm_unmap
 *    fails, the coalesced arena header, free block metadata, and g_mapped
 *    accounting remain intact and reusable for future allocations.
 *  - Pointers below BANANA_VM_BASE belong to the kernel allocator and are
 *    forwarded to __banana->free / __banana->realloc (with __aligned_block).
 *
 * 32-bit, and systems without vm_map/vm_unmap: the kernel's allocator.
 */
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <banana.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

int __aligned_block(void* p, void** raw, size_t* size, int remove); /* posix.c */

/* Kept for ABI compatibility with pthread.c; unused (no TLS caches). */
void** (*__malloc_slot_fn)(void);
void __malloc_thread_end(void* cache) { (void)cache; }

#ifdef __x86_64__

#define PAGE_SIZE        4096ul
#define ARENA_SIZE       65536ul
#define MIN_ALIGN        16ul
#define MIN_SPLIT_SIZE   16ul
#define PROT_RW          (BANANA_PROT_READ | BANANA_PROT_WRITE)

#define ARENA_MAGIC      0x4172656E61426E31ull /* "1nBanerA" */
#define BLOCK_MAGIC_FREE 0x46726565426C6B31ull /* "1klBeerF" */
#define BLOCK_MAGIC_USED 0x55736564426C6B31ull /* "1klBdesU" */

struct block;

typedef struct arena {
    uint64_t      magic;
    struct arena* prev;
    struct arena* next;
    struct block* first;        /* first physical block at (uint8_t*)this + sizeof(arena_t) */
    size_t        map_size;     /* total bytes mapped via vm_map */
    size_t        used_blocks;  /* number of live allocated blocks in this arena */
    size_t        is_large;     /* 1 while a dedicated large allocation owns this arena */
    size_t        pad;          /* pad header to 64 bytes (16-byte aligned) */
} arena_t;

typedef struct block {
    uint64_t      magic;        /* BLOCK_MAGIC_FREE or BLOCK_MAGIC_USED */
    arena_t*      arena;        /* owning arena */
    struct block* prev;         /* previous physical block in arena (NULL if first) */
    struct block* next;         /* next physical block in arena (NULL if last) */
    size_t        size;         /* usable bytes from payload start to next block / arena end */
    size_t        used;         /* 1 if allocated, 0 if free */
    size_t        pad[2];       /* pad header to 64 bytes (16-byte aligned) */
} block_t;

typedef char __malloc_hdr_align_check[
    (sizeof(arena_t) == 64 && sizeof(block_t) == 64) ? 1 : -1
];

#define ARENA_OVERHEAD   (sizeof(arena_t) + sizeof(block_t))
#define ARENA_MAX_BLOCK  (ARENA_SIZE - ARENA_OVERHEAD)

static banana_mutex_t g_lock = BANANA_MUTEX_INIT;
static int            g_state;  /* 0: uninitialized, 1: local heap, -1: kernel fallback */
static arena_t*       g_arenas;
static size_t         g_mapped;
static size_t         g_inuse;

static int add_overflow(size_t a, size_t b, size_t* out) {
    if (a > SIZE_MAX - b) return 1;
    *out = a + b;
    return 0;
}

static int align_up_checked(size_t val, size_t align, size_t* out) {
    size_t tmp;
    if (add_overflow(val, align - 1, &tmp)) return 1;
    *out = tmp & ~(align - 1);
    return 0;
}

static int kernel_block(const void* p) {
    return (uintptr_t)p < BANANA_VM_BASE;
}

/* Must be called with g_lock held.
 * BANANA_HAS only checks struct size, so also verify non-NULL function pointers. */
static int heap_init_locked(void) {
    if (g_state == 0) {
        int ok = __banana != NULL &&
                 __banana->version >= 16 &&
                 BANANA_HAS(__banana, vm_map) &&
                 BANANA_HAS(__banana, vm_unmap) &&
                 __banana->vm_map != NULL &&
                 __banana->vm_unmap != NULL;
        g_state = ok ? 1 : -1;
    }
    return g_state > 0;
}

static void arena_link_locked(arena_t* a) {
    a->prev = NULL;
    a->next = g_arenas;
    if (g_arenas) g_arenas->prev = a;
    g_arenas = a;
}

static void arena_unlink_locked(arena_t* a) {
    if (a->prev) a->prev->next = a->next;
    else g_arenas = a->next;
    if (a->next) a->next->prev = a->prev;
    a->prev = NULL;
    a->next = NULL;
}

static arena_t* arena_create_locked(size_t map_size, size_t is_large) {
    void* mem = __banana->vm_map(NULL, map_size, PROT_RW, 0);
    if (!mem || kernel_block(mem) || ((uintptr_t)mem & (PAGE_SIZE - 1)) != 0) {
        if (mem) __banana->vm_unmap(mem, map_size);
        return NULL;
    }
    arena_t* a = (arena_t*)mem;
    a->magic = ARENA_MAGIC;
    a->prev = NULL;
    a->next = NULL;
    a->map_size = map_size;
    a->used_blocks = 0;
    a->is_large = is_large;
    a->pad = 0;

    block_t* b = (block_t*)((uint8_t*)a + sizeof(arena_t));
    b->magic = BLOCK_MAGIC_FREE;
    b->arena = a;
    b->prev = NULL;
    b->next = NULL;
    b->size = map_size - ARENA_OVERHEAD;
    b->used = 0;
    b->pad[0] = 0;
    b->pad[1] = 0;

    a->first = b;
    arena_link_locked(a);
    g_mapped += map_size;
    return a;
}

/* Attempt to allocate req bytes (already a multiple of 16) with power-of-two
 * alignment align (>= 16) out of free block b. Splits a leading free block if
 * alignment requires shifting the payload, and splits a trailing free block if
 * split_tail is non-zero and sufficient remainder exists. */
static void* carve_block_locked(block_t* b, size_t req, size_t align, int split_tail) {
    uintptr_t u0 = (uintptr_t)b + sizeof(block_t);

    if ((u0 & (align - 1)) != 0) {
        size_t min_lead = sizeof(block_t) + MIN_SPLIT_SIZE;
        size_t min_u_sz;
        if (add_overflow((size_t)u0, min_lead, &min_u_sz)) return NULL;
        size_t u_sz;
        if (align_up_checked(min_u_sz, align, &u_sz)) return NULL;

        size_t lead_total = u_sz - (size_t)u0;
        if (b->size < lead_total || b->size - lead_total < req) return NULL;

        block_t* aligned_b = (block_t*)((uintptr_t)u_sz - sizeof(block_t));
        aligned_b->magic = BLOCK_MAGIC_FREE;
        aligned_b->arena = b->arena;
        aligned_b->prev = b;
        aligned_b->next = b->next;
        aligned_b->size = b->size - lead_total;
        aligned_b->used = 0;
        aligned_b->pad[0] = 0;
        aligned_b->pad[1] = 0;

        if (b->next) b->next->prev = aligned_b;
        b->next = aligned_b;
        b->size = lead_total - sizeof(block_t);
        b = aligned_b;
    } else {
        if (b->size < req) return NULL;
    }

    if (split_tail && b->size - req >= sizeof(block_t) + MIN_SPLIT_SIZE) {
        block_t* tail = (block_t*)((uint8_t*)b + sizeof(block_t) + req);
        tail->magic = BLOCK_MAGIC_FREE;
        tail->arena = b->arena;
        tail->prev = b;
        tail->next = b->next;
        tail->size = b->size - req - sizeof(block_t);
        tail->used = 0;
        tail->pad[0] = 0;
        tail->pad[1] = 0;

        if (b->next) b->next->prev = tail;
        b->next = tail;
        b->size = req;
    }

    b->magic = BLOCK_MAGIC_USED;
    b->used = 1;
    b->arena->used_blocks++;
    g_inuse += b->size;
    return (void*)((uint8_t*)b + sizeof(block_t));
}

static void* heap_alloc_locked(size_t align, size_t n) {
    if (align < MIN_ALIGN) align = MIN_ALIGN;
    if ((align & (align - 1)) != 0) {
        errno = EINVAL;
        return NULL;
    }
    if (n == 0) n = 1;

    size_t req;
    if (align_up_checked(n, MIN_ALIGN, &req)) {
        errno = ENOMEM;
        return NULL;
    }

    /* Compute worst-case bytes needed in a freshly mapped arena. */
    size_t needed = ARENA_OVERHEAD;
    if (add_overflow(needed, req, &needed)) {
        errno = ENOMEM;
        return NULL;
    }
    if (align > MIN_ALIGN) {
        if (add_overflow(needed, sizeof(block_t) + MIN_SPLIT_SIZE, &needed) ||
            add_overflow(needed, align, &needed)) {
            errno = ENOMEM;
            return NULL;
        }
    }

    /* First-fit search across existing non-pinned arenas (includes standard
     * 64 KiB arenas and any arena retained after a failed vm_unmap). */
    for (arena_t* a = g_arenas; a != NULL; a = a->next) {
        if (a->is_large) continue;
        for (block_t* b = a->first; b != NULL; b = b->next) {
            if (!b->used && b->magic == BLOCK_MAGIC_FREE) {
                void* p = carve_block_locked(b, req, align, 1);
                if (p) return p;
            }
        }
    }

    size_t map_size = ARENA_SIZE;
    size_t is_large = 0;
    if (needed > ARENA_SIZE) {
        if (align_up_checked(needed, PAGE_SIZE, &map_size)) {
            errno = ENOMEM;
            return NULL;
        }
        is_large = 1;
    }

    arena_t* a = arena_create_locked(map_size, is_large);
    if (!a) {
        errno = ENOMEM;
        return NULL;
    }

    void* p = carve_block_locked(a->first, req, align, !is_large);
    if (!p) {
        /* Defensive cleanup if carving ever failed. */
        arena_unlink_locked(a);
        if (__banana->vm_unmap(a, map_size) == 0) g_mapped -= map_size;
        else { a->is_large = 0; arena_link_locked(a); }
        errno = ENOMEM;
        return NULL;
    }
    return p;
}

/* Locate and validate a live block without probing memory outside known arenas. */
static block_t* find_used_block_locked(const void* p) {
    uintptr_t addr = (uintptr_t)p;
    if ((addr & (MIN_ALIGN - 1)) != 0) return NULL;

    arena_t* a = NULL;
    for (arena_t* cur = g_arenas; cur != NULL; cur = cur->next) {
        uintptr_t base = (uintptr_t)cur;
        uintptr_t min_p = base + ARENA_OVERHEAD;
        uintptr_t end = base + cur->map_size;
        if (addr >= min_p && addr < end) {
            a = cur;
            break;
        }
    }
    if (!a || a->magic != ARENA_MAGIC) return NULL;

    block_t* b = (block_t*)(addr - sizeof(block_t));
    if (b->magic != BLOCK_MAGIC_USED || !b->used || b->arena != a) return NULL;

    uintptr_t end = (uintptr_t)a + a->map_size;
    if (b->size < MIN_SPLIT_SIZE || b->size > end - addr) return NULL;
    if (b->next && (uintptr_t)b->next != addr + b->size) return NULL;
    if (!b->next && addr + b->size != end) return NULL;
    if (b->prev && b->prev->next != b) return NULL;
    if (!b->prev && a->first != b) return NULL;
    return b;
}

static void free_block_locked(block_t* b) {
    arena_t* a = b->arena;
    g_inuse -= b->size;
    a->used_blocks--;
    b->used = 0;
    b->magic = BLOCK_MAGIC_FREE;

    /* Coalesce with physical successor if free. */
    if (b->next && !b->next->used && b->next->magic == BLOCK_MAGIC_FREE) {
        block_t* next = b->next;
        next->magic = 0;
        b->size += sizeof(block_t) + next->size;
        b->next = next->next;
        if (b->next) b->next->prev = b;
    }

    /* Coalesce with physical predecessor if free. */
    if (b->prev && !b->prev->used && b->prev->magic == BLOCK_MAGIC_FREE) {
        block_t* prev = b->prev;
        b->magic = 0;
        prev->size += sizeof(block_t) + b->size;
        prev->next = b->next;
        if (prev->next) prev->next->prev = prev;
        b = prev;
    }

    /* Never unmap a nonempty arena. */
    if (a->used_blocks != 0) return;

    /* Arena is now completely empty and coalesced into a->first.
     * Unlink and save map_size before vm_unmap so *a is never read after
     * a successful unmap; if vm_unmap fails, the kernel left the VMA and
     * pages intact, so re-link *a as a general reusable arena. */
    size_t map_size = a->map_size;
    a->is_large = 0;
    arena_unlink_locked(a);
    if (__banana->vm_unmap(a, map_size) == 0) {
        g_mapped -= map_size;
    } else {
        arena_link_locked(a);
    }
}

void* __malloc_aligned(size_t align, size_t n) {
    banana_lock(&g_lock);
    if (!heap_init_locked()) {
        banana_unlock(&g_lock);
        return NULL;
    }
    void* p = heap_alloc_locked(align, n);
    banana_unlock(&g_lock);
    return p;
}

void* malloc(size_t n) {
    banana_lock(&g_lock);
    if (heap_init_locked()) {
        void* p = heap_alloc_locked(MIN_ALIGN, n);
        banana_unlock(&g_lock);
        return p;
    }
    banana_unlock(&g_lock);

    void* p = __banana->malloc(n ? n : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void free(void* p) {
    if (!p) return;
    if (!kernel_block(p)) {
        banana_lock(&g_lock);
        if (heap_init_locked()) {
            block_t* b = find_used_block_locked(p);
            if (b) free_block_locked(b);
            banana_unlock(&g_lock);
            return;
        }
        banana_unlock(&g_lock);
    }
    void* raw;
    __banana->free(__aligned_block(p, &raw, NULL, 1) ? raw : p);
}

void* realloc(void* p, size_t n) {
    if (!p) return malloc(n);
    if (!n) {
        free(p);
        return NULL;
    }
    if (!kernel_block(p)) {
        banana_lock(&g_lock);
        if (heap_init_locked()) {
            block_t* b = find_used_block_locked(p);
            if (!b) {
                banana_unlock(&g_lock);
                errno = EINVAL;
                return NULL;
            }
            size_t have = b->size;
            if (n <= have && (n > have / 2 || have <= 64)) {
                banana_unlock(&g_lock);
                return p;
            }
            banana_unlock(&g_lock);

            void* q = malloc(n);
            if (!q) return NULL;
            memcpy(q, p, have < n ? have : n);
            free(p);
            return q;
        }
        banana_unlock(&g_lock);
    }
    void* raw;
    size_t old;
    if (__aligned_block(p, &raw, &old, 0)) {
        void* q = malloc(n);
        if (q) {
            memcpy(q, p, old < n ? old : n);
            __aligned_block(p, &raw, NULL, 1);
            __banana->free(raw);
        }
        return q;
    }
    void* q = __banana->realloc(p, n);
    if (!q) errno = ENOMEM;
    return q;
}

void* calloc(size_t n, size_t s) {
    if (s && n > SIZE_MAX / s) {
        errno = ENOMEM;
        return NULL;
    }
    size_t len = n * s;
    void* p = malloc(len);
    if (!p) return NULL;
    memset(p, 0, len);
    return p;
}

size_t malloc_usable_size(void* p) {
    if (!p) return 0;
    if (!kernel_block(p)) {
        banana_lock(&g_lock);
        if (heap_init_locked()) {
            block_t* b = find_used_block_locked(p);
            size_t sz = b ? b->size : 0;
            banana_unlock(&g_lock);
            return sz;
        }
        banana_unlock(&g_lock);
    }
    size_t old;
    void* raw;
    return __aligned_block(p, &raw, &old, 0) ? old : 0;
}

int __malloc_heap(void) {
    banana_lock(&g_lock);
    int ok = heap_init_locked();
    banana_unlock(&g_lock);
    return ok;
}

void __malloc_stats(size_t* mapped, size_t* inuse) {
    banana_lock(&g_lock);
    if (mapped) *mapped = g_mapped;
    if (inuse) *inuse = g_inuse;
    banana_unlock(&g_lock);
}

#else /* ── 32-bit: the kernel's allocator ───────────────────────── */

void* __malloc_aligned(size_t align, size_t n) { (void)align; (void)n; return NULL; }
int   __malloc_heap(void) { return 0; }
void  __malloc_stats(size_t* mapped, size_t* inuse) { if (mapped) *mapped = 0; if (inuse) *inuse = 0; }

void* malloc(size_t n) {
    void* p = __banana->malloc(n ? n : 1);
    if (!p) errno = ENOMEM;
    return p;
}

void free(void* p) {
    void* raw;
    if (!p) return;
    __banana->free(__aligned_block(p, &raw, NULL, 1) ? raw : p);
}

void* realloc(void* p, size_t n) {
    void* raw;
    size_t old;
    if (p && !n) { free(p); return NULL; }
    if (p && __aligned_block(p, &raw, &old, 0)) {
        void* q = malloc(n);
        if (q) {
            memcpy(q, p, old < n ? old : n);
            __aligned_block(p, &raw, NULL, 1);
            __banana->free(raw);
        }
        return q;
    }
    void* q = __banana->realloc(p, n);
    if (!q) errno = ENOMEM;
    return q;
}

void* calloc(size_t n, size_t s) {
    if (s && n > (size_t)-1 / s) { errno = ENOMEM; return NULL; }
    void* p = malloc(n * s);
    if (p) memset(p, 0, n * s);
    return p;
}

size_t malloc_usable_size(void* p) {
    size_t old;
    void* raw;
    return p && __aligned_block(p, &raw, &old, 0) ? old : 0;
}

#endif
