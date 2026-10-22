/* Real allocator, private internals visible only in this host translation unit.
 * No fault-injection entry point is exported by the kernel. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <kheap.c>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); exit(1); \
} } while (0)
#define MIB (1024u * 1024u)
static uint8_t* arena;
static uint32_t mb2;

static void healthy(void) {
    char err[128] = "stale error";
    int bad = kheap_check(err, sizeof(err));
    if (bad) fprintf(stderr, "heap: %s\n", err);
    CHECK(!bad && !err[0]);
}

static void corrupt(void) {
    char err[128] = "";
    CHECK(kheap_check(err, sizeof(err)) != 0 && err[0]);
    CHECK(kheap_check(NULL, 0) != 0);
    char tiny[2] = {'x', 'y'};
    CHECK(kheap_check(tiny, 0) != 0 && tiny[0] == 'x');
    CHECK(kheap_check(tiny, -1) != 0 && tiny[0] == 'x');
    CHECK(kheap_check(tiny, 1) != 0 && tiny[0] == 0 && tiny[1] == 'y');
}

static void reset_heap(void) { kheap_init(mb2); healthy(); }
static unsigned free_count(void) {
    unsigned n = 0;
    for (block_t* b = g_head; b; b = b->next) if (!b->used) n++;
    return n;
}
static void pattern(const uint8_t* p, size_t n, uint8_t byte) {
    for (size_t i = 0; i < n; i++) CHECK(p[i] == byte);
}

static void basic(void) {
    reset_heap();
    CHECK(!kmalloc(0) && !kmalloc_aligned(64, 48));
    kfree(NULL);
    uint8_t* a = kzalloc(257);
    CHECK(a && !((uintptr_t)a & 15));
    pattern(a, 257, 0);
    memset(a, 0x5a, 257);
    CHECK(krealloc(a, 128) == a);
    uint8_t* b = krealloc(a, 8192); /* In-place growth. */
    CHECK(b == a);
    pattern(b, 128, 0x5a);
    void* guard = kmalloc(64);
    CHECK(guard);
    a = krealloc(b, 32768); /* Forced relocation. */
    CHECK(a && a != b);
    pattern(a, 128, 0x5a);
    kfree(guard);
    CHECK(krealloc(a, 0) == NULL);
    a = krealloc(NULL, 64);
    CHECK(a);
    kfree(a);
    for (size_t align = 16; align <= 65536; align *= 2) {
        a = kmalloc_aligned(529, align);
        CHECK(a && !((uintptr_t)a & (align - 1)));
        memset(a, 0xa5, 529);
        healthy();
        pattern(a, 529, 0xa5);
        kfree(a);
    }
    healthy();
    CHECK(!g_used && free_count() == 1 && kheap_largest_free() == g_total);
    puts("PASS basic allocation, zeroing, realloc preservation and alignment");
}

static void exact_and_exhausted(void) {
    static const uint32_t sizes[] = {16, 48, 64, 112, 128, 240, 256, 496, 512, 528, 1008, 1024, 1040};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        reset_heap();
        uint8_t* p = kmalloc(sizes[i]);
        void* guard = kmalloc(64);
        void* rest = kmalloc(kheap_largest_free());
        CHECK(p && guard && rest && !kmalloc(1));
        memset(p, 0x36, sizes[i]);
        uint32_t used = g_used;
        CHECK(!krealloc(p, sizes[i] + 4096));
        CHECK(g_used == used);
        pattern(p, sizes[i], 0x36);
        healthy();
        kfree(p);
        CHECK(kmalloc(sizes[i]) == p); /* Only the exact-fit block is available. */
        CHECK(!kmalloc(1));
        kfree(rest);
        kfree(guard);
        kfree(p);
        healthy();
        CHECK(free_count() == 1 && !g_used);
    }
    puts("PASS exhausted exact fits near bin boundaries (including 528), failed realloc");
}

static void regions(void) {
    reset_heap();
    uint32_t initial = g_total;
    /* One mapping guarantees the advertised ordering, even with ASLR. The
     * added ranges touch the initial region, but must never coalesce with it. */
    CHECK((uintptr_t)arena + MIB < (uintptr_t)g_head);
    kheap_add_region((uintptr_t)arena + MIB, (uintptr_t)arena + 2 * MIB);
    healthy();
    CHECK((uintptr_t)arena + 6 * MIB > (uintptr_t)g_head);
    kheap_add_region((uintptr_t)arena + 6 * MIB, (uintptr_t)arena + 7 * MIB);
    healthy();
    CHECK(g_total == initial + 2 * (MIB - 3 * HDR) && free_count() == 3);
    void* all[3];
    for (int i = 0; i < 3; i++) {
        all[i] = kmalloc(kheap_largest_free());
        CHECK(all[i]);
        healthy();
    }
    CHECK(!kmalloc(1));
    for (int i = 2; i >= 0; i--) { kfree(all[i]); healthy(); }
    CHECK(free_count() == 3 && kheap_largest_free() == initial && !g_used);

    block_t* lead = g_head->next;
    block_t* body = lead->next;
    block_t* tail = body->next;
    CHECK(!lead->size && !tail->size);
    body->size -= ALIGN; corrupt(); body->size += ALIGN;
    lead->used = 0; corrupt(); lead->used = HEAP_MAGIC;
    block_t* saved = body->next;
    body->next = NULL; corrupt(); body->next = saved;
    tail->used = 0; corrupt(); tail->used = HEAP_MAGIC;
    healthy();
    puts("PASS below/above touching regions, sentinels and no cross-region coalescing");
}

static void diagnostics(void) {
    reset_heap();
    void* live[80];
    for (int i = 0; i < 80; i++) { live[i] = kmalloc(64); CHECK(live[i]); }
    for (int i = 0; i < 80; i += 2) kfree(live[i]);
    healthy(); /* Many nodes of one class, in reverse physical order. */
    block_t* b = g_bins[bin_of(64)];
    block_t* saved = L(b)->next;
    L(b)->next = b; corrupt(); L(b)->next = saved;
    block_t* other = saved;
    CHECK(other);
    saved = L(other)->next;
    L(other)->next = b; corrupt(); L(other)->next = saved;
    saved = L(b)->prev;
    L(b)->prev = b; corrupt(); L(b)->prev = saved;
    saved = L(b)->next;
    L(b)->next = (block_t*)(uintptr_t)0x10; corrupt(); L(b)->next = saved;
    g_bins[bin_of(64)] = (block_t*)((uint8_t*)b + HDR);
    corrupt(); g_bins[bin_of(64)] = b;
    g_bins[bin_of(64)] = NULL; corrupt(); g_bins[bin_of(64)] = b;
    CHECK(!g_bins[5]);
    g_bins[5] = b; g_bins[bin_of(64)] = NULL;
    corrupt(); g_bins[5] = NULL; g_bins[bin_of(64)] = b;
    g_bins[5] = b; corrupt(); g_bins[5] = NULL; /* Duplicate membership. */
    block_t* allocated = (block_t*)((uint8_t*)live[1] - HDR);
    g_bins[5] = allocated; corrupt(); g_bins[5] = NULL;
    g_used += ALIGN; corrupt(); g_used -= ALIGN;
    g_total += ALIGN; corrupt(); g_total -= ALIGN;
    g_head->used = 123; corrupt(); g_head->used = 0;
    g_head->size++; corrupt(); g_head->size--;
    g_head->size -= ALIGN; corrupt(); g_head->size += ALIGN;
    saved = g_head->prev;
    g_head->prev = b; corrupt(); g_head->prev = saved;
    saved = g_head->next;
    g_head->next = g_head; corrupt(); g_head->next = saved;
    healthy();
    for (int i = 1; i < 80; i += 2) kfree(live[i]);
    healthy();
    puts("PASS cycles, bad links/pointers, wrong classes, missing/duplicate entries and byte accounting");
}

static uint32_t random32(uint32_t* s) {
    *s ^= *s << 13; *s ^= *s >> 17; *s ^= *s << 5; return *s;
}

static void stress(uint32_t seed) {
    enum { SLOTS = 256, STEPS = 60000 };
    struct { uint8_t* p; size_t n; uint8_t byte; } slots[SLOTS] = {{0}};
    unsigned failures = 0, reallocs = 0;
    for (int step = 0; step < STEPS; step++) {
        unsigned i = random32(&seed) % SLOTS;
        if (slots[i].p) {
            pattern(slots[i].p, slots[i].n, slots[i].byte);
            if (random32(&seed) & 1) {
                size_t n = random32(&seed) % 65536 + 1;
                uint8_t* p = krealloc(slots[i].p, n);
                /* Check the surviving prefix BEFORE painting the new buffer. */
                pattern(p ? p : slots[i].p, p && n < slots[i].n ? n : slots[i].n, slots[i].byte);
                if (p) {
                    reallocs++;
                    slots[i].p = p; slots[i].n = n;
                    memset(p, slots[i].byte, n);
                } else failures++;
            } else { kfree(slots[i].p); slots[i].p = NULL; }
        } else {
            size_t n = random32(&seed) % 32768 + 1;
            size_t align = (size_t)16 << (random32(&seed) % 9);
            int aligned = random32(&seed) & 1;
            uint8_t* p = aligned ? kmalloc_aligned(n, align) : kmalloc(n);
            if (p) {
                CHECK(!((uintptr_t)p & ((aligned ? align : 16) - 1)));
                slots[i].p = p; slots[i].n = n; slots[i].byte = (uint8_t)random32(&seed);
                memset(p, slots[i].byte, n);
            } else failures++;
        }
        if (step % 257 == 0) {
            for (unsigned j = 0; j < SLOTS; j++)
                if (slots[j].p) pattern(slots[j].p, slots[j].n, slots[j].byte);
            healthy();
        }
    }
    for (unsigned i = 0; i < SLOTS; i++) if (slots[i].p) {
        pattern(slots[i].p, slots[i].n, slots[i].byte); kfree(slots[i].p);
    }
    healthy();
    CHECK(!g_used && free_count() == 3 && reallocs > 1000);
    printf("PASS 60000 deterministic mixed operations, %u reallocs, %u allocation failures\n", reallocs, failures);
}

int main(void) {
    corrupt(); /* Uninitialized heap. */
    int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#ifdef MAP_32BIT
    flags |= MAP_32BIT;
#endif
    arena = mmap((void*)(uintptr_t)0x20000000, 8 * MIB, PROT_READ | PROT_WRITE, flags, -1, 0);
    CHECK(arena != MAP_FAILED && (uintptr_t)arena >= HEAP_MIN_ADDR &&
          (uint64_t)(uintptr_t)arena + 8 * MIB < HEAP_MAX_ADDR);
    /* Multiboot2 header and a single memory-map entry, outside heap regions. */
    uint32_t* w = (uint32_t*)arena;
    w[0] = 56; w[1] = 0; w[2] = 6; w[3] = 40; w[4] = 24; w[5] = 0;
    uint64_t* e = (uint64_t*)(arena + 24);
    e[0] = (uintptr_t)arena + 2 * MIB; e[1] = 4 * MIB;
    w[10] = 1; w[11] = 0; w[12] = 0; w[13] = 8;
    mb2 = (uint32_t)(uintptr_t)arena;
    basic(); exact_and_exhausted(); diagnostics(); regions();
    stress(0x12345678); stress(0xdeadbeef);
    CHECK(munmap(arena, 8 * MIB) == 0);
    printf("PASS real kheap suite (%zu-bit pointers)\n", sizeof(void*) * 8);
    return 0;
}
