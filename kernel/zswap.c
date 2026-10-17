#include "zswap.h"
#include "uvm.h"
#include "kheap.h"
#include "hmem.h"
#include "kstring.h"
#include "spinlock.h"
#include "task.h"
#include "timer.h"
#include "serial.h"

/* ── the entries: blocks of 1024 that never move (pointers stay good) ── */

#define EBLK   1024u
#define NBLK   1024u                       /* up to 1M pages (4 GiB of app memory) */

static zent_t*    g_blk[NBLK];
static uint32_t   g_nblk;
static uint32_t   g_free_head;             /* a free entry (its frame holds the next), 0: none */
static spinlock_t g_zlock = SPINLOCK_INIT;
static zswap_stats_t g_st;
static int        g_on = 1;

zent_t* zswap_ent(uint32_t idx) {
    if (!idx) return NULL;
    uint32_t b = idx / EBLK, i = idx % EBLK;
    return b < g_nblk ? &g_blk[b][i] : NULL;
}

uint32_t zswap_new(uintptr_t frame) {
    uintptr_t f = spin_lock_irq(&g_zlock);
    if (!g_free_head) {
        /* a new block: its entries (but number 0) go on the free list */
        if (g_nblk == NBLK) { spin_unlock_irq(&g_zlock, f); return 0; }
        spin_unlock_irq(&g_zlock, f);
        zent_t* blk = (zent_t*)kzalloc(EBLK * sizeof(zent_t));
        if (!blk) return 0;
        f = spin_lock_irq(&g_zlock);
        if (g_nblk == NBLK) { spin_unlock_irq(&g_zlock, f); kfree(blk); return 0; }
        uint32_t b = g_nblk++;
        g_blk[b] = blk;
        for (uint32_t i = EBLK; i-- > 0; ) {
            uint32_t idx = b * EBLK + i;
            if (!idx) continue;
            blk[i].state = ZS_FREE;
            blk[i].frame = g_free_head;
            g_free_head = idx;
        }
    }
    uint32_t idx = g_free_head;
    zent_t* e = zswap_ent(idx);
    g_free_head = (uint32_t)e->frame;
    e->state = ZS_PENDING;
    e->frame = frame;
    e->data = NULL;
    e->len = 0;
    spin_unlock_irq(&g_zlock, f);
    return idx;
}

void zswap_free(uint32_t idx) {
    zent_t* e = zswap_ent(idx);
    if (!e || e->state == ZS_FREE) return;
    uintptr_t f = spin_lock_irq(&g_zlock);
    uint8_t* data = e->data;
    if (e->state == ZS_STORED) {
        g_st.pages--;
        if (!e->len) g_st.zero_pages--;
        g_st.bytes -= e->len;
    }
    e->state = ZS_FREE;
    e->data = NULL;
    e->len = 0;
    e->frame = g_free_head;
    g_free_head = idx;
    spin_unlock_irq(&g_zlock, f);
    kfree(data);
}

void zswap_store(uint32_t idx, uint8_t* data, uint32_t len) {
    zent_t* e = zswap_ent(idx);
    if (!e) { kfree(data); return; }
    uintptr_t f = spin_lock_irq(&g_zlock);
    e->data = data;
    e->len = data ? len : 0;
    e->state = ZS_STORED;
    e->frame = 0;
    g_st.pages++;
    if (!e->len) g_st.zero_pages++;
    g_st.bytes += e->len;
    g_st.pages_out++;
    spin_unlock_irq(&g_zlock, f);
}

int zswap_take(uint32_t idx, uint8_t* page) {
    zent_t* e = zswap_ent(idx);
    if (!e || e->state != ZS_STORED) return -1;
    int rc = 0;
    if (!e->len) memset(page, 0, 4096);
    else if (e->len == 4096) memcpy(page, e->data, 4096);       /* (kept as it was: did not compress) */
    else rc = zswap_unpack(e->data, e->len, page);
    __atomic_add_fetch(&g_st.pages_in, 1, __ATOMIC_RELAXED);
    zswap_free(idx);
    return rc;
}

void zswap_stats(zswap_stats_t* s) {
    uintptr_t f = spin_lock_irq(&g_zlock);
    *s = g_st;
    spin_unlock_irq(&g_zlock, f);
}

void zswap_enable(int on) { g_on = on != 0; }
int  zswap_enabled(void) { return g_on; }

/* ── the compressor: LZ77 in the LZ4 manner, for one 4 KiB page ────────
 * Sequences of: a token (literals in its high 4 bits, match length - 4 in
 * its low 4; 15 = more in the bytes that follow, 255 each), the literals,
 * a 2-byte offset back, more match length. The input ends after the last
 * sequence's literals. */

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }

static int put_len(uint8_t* out, uint32_t* o, uint32_t cap, uint32_t n) {
    while (n >= 255) { if (*o >= cap) return -1; out[(*o)++] = 255; n -= 255; }
    if (*o >= cap) return -1;
    out[(*o)++] = (uint8_t)n;
    return 0;
}

static int put_seq(uint8_t* out, uint32_t* o, uint32_t cap, const uint8_t* lit, uint32_t nlit, uint32_t off, uint32_t mlen) {
    uint32_t ml = mlen ? mlen - 4 : 0;
    if (*o >= cap) return -1;
    out[(*o)++] = (uint8_t)((nlit < 15 ? nlit : 15) << 4 | (ml < 15 ? ml : 15));
    if (nlit >= 15 && put_len(out, o, cap, nlit - 15) < 0) return -1;
    if (*o + nlit > cap) return -1;
    memcpy(out + *o, lit, nlit);
    *o += nlit;
    if (!mlen) return 0;
    if (*o + 2 > cap) return -1;
    out[(*o)++] = (uint8_t)off;
    out[(*o)++] = (uint8_t)(off >> 8);
    if (ml >= 15 && put_len(out, o, cap, ml - 15) < 0) return -1;
    return 0;
}

uint32_t zswap_pack(const uint8_t* page, uint8_t* out, uint32_t cap, int* zero, uint16_t* ht) {
    const uint64_t* w = (const uint64_t*)page;
    uint64_t any = 0;
    for (int i = 0; i < 512; i++) any |= w[i];
    *zero = any == 0;
    if (*zero) return 0;
    memset(ht, 0, 4096 * sizeof(uint16_t));   /* position + 1 of the last 4 bytes with each hash */
    uint32_t ip = 0, anchor = 0, o = 0;
    while (ip + 4 <= 4096) {
        uint32_t v = rd32(page + ip), h = (v * 2654435761u) >> 20;
        uint32_t ref = ht[h];
        ht[h] = (uint16_t)(ip + 1);
        if (ref && rd32(page + ref - 1) == v) {
            ref--;
            uint32_t m = 4;
            while (ip + m < 4096 && page[ref + m] == page[ip + m]) m++;
            if (put_seq(out, &o, cap, page + anchor, ip - anchor, ip - ref, m) < 0) return 0;
            ip += m;
            anchor = ip;
            continue;
        }
        ip++;
    }
    if (anchor < 4096 && put_seq(out, &o, cap, page + anchor, 4096 - anchor, 0, 0) < 0) return 0;
    return o;
}

static int get_len(const uint8_t* in, uint32_t len, uint32_t* i, uint32_t* n) {
    for (;;) {
        if (*i >= len) return -1;
        uint8_t b = in[(*i)++];
        *n += b;
        if (b != 255) return 0;
        if (*n > 8192) return -1;
    }
}

int zswap_unpack(const uint8_t* in, uint32_t len, uint8_t* page) {
    uint32_t i = 0, o = 0;
    while (i < len) {
        uint8_t t = in[i++];
        uint32_t nlit = t >> 4, ml = t & 15;
        if (nlit == 15 && get_len(in, len, &i, &nlit) < 0) return -1;
        if (nlit > len - i || nlit > 4096 - o) return -1;
        memcpy(page + o, in + i, nlit);
        i += nlit;
        o += nlit;
        if (i == len) break;                  /* (the last sequence: literals only) */
        if (len - i < 2) return -1;
        uint32_t off = (uint32_t)in[i] | (uint32_t)in[i + 1] << 8;
        i += 2;
        if (ml == 15 && get_len(in, len, &i, &ml) < 0) return -1;
        uint32_t m = ml + 4;
        if (!off || off > o || m > 4096 - o) return -1;
        for (uint32_t k = 0; k < m; k++, o++) page[o] = page[o - off];
    }
    return o == 4096 ? 0 : -1;
}

/* ── when to compress ──────────────────────────────────────────────────
 * The RAM the system can still give: the kernel heap's free bytes and,
 * 64-bit, the frames above 4 GiB. Below 1/8 of all of it free (after the
 * file cache had its turn: kernel/fs.c), apps' pages not used lately are
 * compressed until 1/5 is free again. */

#ifdef __x86_64__
static void ram(uint64_t* total, uint64_t* free) {
    *total = (uint64_t)kheap_total_bytes() + hmem_total_bytes();
    *free = (uint64_t)(kheap_total_bytes() - kheap_used_bytes()) + (hmem_total_bytes() - hmem_used_bytes());
}

static void zswap_task(void) {
    task_set_background();
    int busy = 0;
    for (;;) {
        task_sleep_ms(busy ? 10 : 100);                  /* (an app filling memory fast: kept up with) */
        busy = 0;
        if (!g_on) continue;
        uint64_t total, free;
        ram(&total, &free);
        if (free >= total / 8) continue;
        busy = 1;
        uint64_t want = total / 5 - free;
        uint32_t pages = (uint32_t)(want / 4096u), done = 0;
        for (int pass = 0; pass < 3 && done < pages; pass++) {
            uint32_t n = uvm_page_out_all(0, pages - done);
            done += n;
            if (!n && pass) break;                /* (first pass: the accessed bits were cleared) */
        }
        static uint32_t said, since_said;                /* (a line every 5 s at most) */
        since_said += done;
        if (since_said && timer_ms() - said > 5000u) {
            klog("zswap: memory low - %u KiB of app memory compressed\n", since_said * 4u);
            said = timer_ms();
            since_said = 0;
        }
    }
}
#endif

void zswap_start(void) {
#ifdef __x86_64__
    task_create("zswap", zswap_task);
#endif
}

uint32_t zswap_compress_now(void) {
    return uvm_page_out_all(1, 0xFFFFFFFFu);
}
