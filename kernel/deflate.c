/*
 * DEFLATE compression (RFC 1951) for ZIP files (kernel/zip.c).
 *
 * The input is cut into blocks of up to BLOCK_TOKENS LZ77 tokens. Matches
 * are found with hash chains over the last 32 KiB (3-byte hashes, a chain
 * of up to CHAIN_MAX positions, one step of lazy matching), then each
 * block gets its own Huffman codes (lengths limited to 15 bits, 7 for the
 * code-length code) - or is stored as it is when that is smaller.
 */
#include "deflate.h"
#include "kheap.h"
#include "kstring.h"

#define WSIZE        32768u
#define WMASK        (WSIZE - 1)
#define HASH_BITS    15
#define HASH_SIZE    (1u << HASH_BITS)
#define MIN_MATCH    3
#define MAX_MATCH    258
#define CHAIN_MAX    64
#define GOOD_LAZY    32            /* a match this long is taken without looking one byte further */
#define BLOCK_TOKENS 32768

static const uint16_t LBASE[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59,
                                    67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const uint8_t LEXT[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const uint16_t DBASE[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769,
                                    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const uint8_t DEXT[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
/* the order the code-length code's lengths are sent in */
static const uint8_t CLORDER[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };

/* ── CRC-32 ────────────────────────────────────────────────────────── */

static uint32_t g_crc_table[256];

uint32_t crc32_update(uint32_t crc, const void* data, uint32_t len) {
    if (!g_crc_table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            g_crc_table[i] = c;
        }
    }
    const uint8_t* p = (const uint8_t*)data;
    crc = ~crc;
    while (len--) crc = g_crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

/* ── bit output ────────────────────────────────────────────────────── */

typedef struct {
    uint8_t* buf;
    uint32_t len, cap;
    uint32_t bits;
    int nbits, oom;
} bw_t;

static void bw_byte(bw_t* w, uint8_t b) {
    if (w->oom) return;
    if (w->len >= w->cap) {
        uint32_t cap = w->cap ? w->cap * 2 : 4096;
        uint8_t* nb = (uint8_t*)krealloc(w->buf, cap);
        if (!nb) { w->oom = 1; return; }
        w->buf = nb;
        w->cap = cap;
    }
    w->buf[w->len++] = b;
}

/* n (<= 16) bits of v, low bit first */
static void bw_put(bw_t* w, uint32_t v, int n) {
    w->bits |= v << w->nbits;
    w->nbits += n;
    while (w->nbits >= 8) { bw_byte(w, (uint8_t)w->bits); w->bits >>= 8; w->nbits -= 8; }
}

static void bw_align(bw_t* w) {
    if (w->nbits > 0) bw_byte(w, (uint8_t)w->bits);
    w->bits = 0;
    w->nbits = 0;
}

/* ── Huffman codes ─────────────────────────────────────────────────── */

/* one compression's scratch space (on the heap: two tasks may compress at once) */
typedef struct {
    uint32_t freq[288], w[576];
    int16_t parent[576];
    uint8_t alive[576];
    uint32_t lf[286], df[30], cf[19];
    uint8_t ll[286], dl[30], cl[19], lens[316];
    uint16_t lc[286], dc[30], cc[19];
    uint8_t rle[316], rle_extra[316];
} scratch_t;

/* code lengths (<= maxbits) for n symbols of the given frequencies */
static void huff_lengths(scratch_t* s, const uint32_t* freq_in, int n, int maxbits, uint8_t* len) {
    uint32_t* freq = s->freq, *w = s->w;
    int16_t* parent = s->parent;
    uint8_t* alive = s->alive;
    for (int i = 0; i < n; i++) freq[i] = freq_in[i];
    for (;;) {
        int used = 0, last = -1;
        for (int i = 0; i < n; i++) { len[i] = 0; if (freq[i]) { used++; last = i; } }
        if (used == 0) return;
        if (used == 1) {               /* one symbol: a complete code of two 1-bit codes */
            len[last] = 1;
            len[last == 0 ? 1 : 0] = 1;
            return;
        }
        int nodes = n;
        for (int i = 0; i < n; i++) { w[i] = freq[i]; alive[i] = freq[i] != 0; parent[i] = -1; }
        for (int left = used; left > 1; left--) {
            int a = -1, b = -1;
            for (int i = 0; i < nodes; i++) {
                if (!alive[i]) continue;
                if (a < 0 || w[i] < w[a]) { b = a; a = i; }
                else if (b < 0 || w[i] < w[b]) b = i;
            }
            w[nodes] = w[a] + w[b];
            alive[nodes] = 1;
            parent[nodes] = -1;
            alive[a] = alive[b] = 0;
            parent[a] = parent[b] = (int16_t)nodes;
            nodes++;
        }
        int too_long = 0;
        for (int i = 0; i < n; i++) {
            if (!freq[i]) continue;
            int d = 0;
            for (int p = i; parent[p] >= 0; p = parent[p]) d++;
            if (d > maxbits) too_long = 1;
            len[i] = (uint8_t)d;
        }
        if (!too_long) return;
        for (int i = 0; i < n; i++) if (freq[i]) freq[i] = (freq[i] >> 1) | 1;      /* flatter: shorter longest code */
    }
}

/* canonical codes from the lengths, bit-reversed (Huffman codes are sent
 * high bit first, bw_put sends low bit first) */
static void huff_codes(const uint8_t* len, int n, uint16_t* code) {
    uint16_t count[16] = { 0 }, next[16];
    for (int i = 0; i < n; i++) count[len[i]]++;
    count[0] = 0;
    uint16_t c = 0;
    for (int b = 1; b < 16; b++) { c = (uint16_t)((c + count[b - 1]) << 1); next[b] = c; }
    for (int i = 0; i < n; i++) {
        if (!len[i]) { code[i] = 0; continue; }
        uint16_t v = next[len[i]]++, r = 0;
        for (int k = 0; k < len[i]; k++) { r = (uint16_t)((r << 1) | (v & 1)); v >>= 1; }
        code[i] = r;
    }
}

/* ── LZ77 tokens and blocks ────────────────────────────────────────── */

typedef struct { uint16_t len; uint16_t dist; } tok_t;     /* dist 0: a literal byte (len) */

static int len_sym(int len) {           /* 0..28: index into LBASE */
    int s = 28;
    while (LBASE[s] > len) s--;
    return s;
}

static int dist_sym(int dist) {         /* 0..29: index into DBASE */
    int s = 29;
    while (DBASE[s] > dist) s--;
    return s;
}

static void stored_blocks(bw_t* w, const uint8_t* in, uint32_t n, int final) {
    do {
        uint32_t k = n > 65535 ? 65535 : n;
        int last = final && k == n;
        bw_put(w, (uint32_t)last, 1);
        bw_put(w, 0, 2);
        bw_align(w);
        bw_byte(w, (uint8_t)k); bw_byte(w, (uint8_t)(k >> 8));
        bw_byte(w, (uint8_t)~k); bw_byte(w, (uint8_t)(~k >> 8));
        for (uint32_t i = 0; i < k; i++) bw_byte(w, in[i]);
        in += k;
        n -= k;
    } while (n);
}

/* the tokens of in[start..end), as one block: dynamic Huffman or stored */
static void write_block(scratch_t* s, bw_t* w, const tok_t* t, int nt, const uint8_t* in, uint32_t start, uint32_t end, int final) {
    uint32_t* lf = s->lf, *df = s->df, *cf = s->cf;
    uint8_t* ll = s->ll, *dl = s->dl, *cl = s->cl, *lens = s->lens, *rle = s->rle, *rle_extra = s->rle_extra;
    uint16_t* lc = s->lc, *dc = s->dc, *cc = s->cc;
    memset(lf, 0, sizeof(s->lf)); memset(df, 0, sizeof(s->df)); memset(cf, 0, sizeof(s->cf));
    for (int i = 0; i < nt; i++) {
        if (!t[i].dist) lf[t[i].len]++;
        else { lf[257 + len_sym(t[i].len)]++; df[dist_sym(t[i].dist)]++; }
    }
    lf[256] = 1;
    huff_lengths(s, lf, 286, 15, ll);
    huff_lengths(s, df, 30, 15, dl);
    int hlit = 286, hdist = 30;
    while (hlit > 257 && !ll[hlit - 1]) hlit--;
    while (hdist > 1 && !dl[hdist - 1]) hdist--;

    /* the code lengths, run-length coded (16: repeat the last 3-6 times, 17 / 18: 3-10 / 11-138 zeros) */
    int nl = 0, nr = 0;
    for (int i = 0; i < hlit; i++) lens[nl++] = ll[i];
    for (int i = 0; i < hdist; i++) lens[nl++] = dl[i];
    for (int i = 0; i < nl;) {
        int v = lens[i], run = 1;
        while (i + run < nl && lens[i + run] == v) run++;
        if (v == 0 && run >= 3) {
            int k = run > 138 ? 138 : run;
            if (k >= 11) { rle[nr] = 18; rle_extra[nr++] = (uint8_t)(k - 11); }
            else { rle[nr] = 17; rle_extra[nr++] = (uint8_t)(k - 3); }
            i += k;
        } else if (v != 0 && run >= 4) {
            rle[nr] = (uint8_t)v; rle_extra[nr++] = 0;
            int k = run - 1 > 6 ? 6 : run - 1;
            rle[nr] = 16; rle_extra[nr++] = (uint8_t)(k - 3);
            i += 1 + k;
        } else {
            rle[nr] = (uint8_t)v; rle_extra[nr++] = 0;
            i++;
        }
    }
    for (int i = 0; i < nr; i++) cf[rle[i]]++;
    huff_lengths(s, cf, 19, 7, cl);
    int hclen = 19;
    while (hclen > 4 && !cl[CLORDER[hclen - 1]]) hclen--;

    /* its size in bits, against the stored block's */
    uint64_t bits = 3 + 5 + 5 + 4 + 3u * (uint32_t)hclen;
    for (int i = 0; i < nr; i++) bits += cl[rle[i]] + (rle[i] == 16 ? 2 : rle[i] == 17 ? 3 : rle[i] == 18 ? 7 : 0);
    for (int i = 0; i < 286; i++) bits += (uint64_t)lf[i] * ll[i];
    for (int i = 0; i < 29; i++) bits += (uint64_t)lf[257 + i] * LEXT[i];
    for (int i = 0; i < 30; i++) bits += (uint64_t)df[i] * (dl[i] + DEXT[i]);
    uint32_t raw = end - start;
    uint64_t stored_bits = (uint64_t)raw * 8 + ((raw + 65534) / 65535 + (raw == 0)) * 40 + 8;
    if (stored_bits <= bits) { stored_blocks(w, in + start, raw, final); return; }

    huff_codes(ll, 286, lc);
    huff_codes(dl, 30, dc);
    huff_codes(cl, 19, cc);
    bw_put(w, (uint32_t)final, 1);
    bw_put(w, 2, 2);
    bw_put(w, (uint32_t)(hlit - 257), 5);
    bw_put(w, (uint32_t)(hdist - 1), 5);
    bw_put(w, (uint32_t)(hclen - 4), 4);
    for (int i = 0; i < hclen; i++) bw_put(w, cl[CLORDER[i]], 3);
    for (int i = 0; i < nr; i++) {
        bw_put(w, cc[rle[i]], cl[rle[i]]);
        if (rle[i] == 16) bw_put(w, rle_extra[i], 2);
        else if (rle[i] == 17) bw_put(w, rle_extra[i], 3);
        else if (rle[i] == 18) bw_put(w, rle_extra[i], 7);
    }
    for (int i = 0; i < nt; i++) {
        if (!t[i].dist) { bw_put(w, lc[t[i].len], ll[t[i].len]); continue; }
        int ls = len_sym(t[i].len), ds = dist_sym(t[i].dist);
        bw_put(w, lc[257 + ls], ll[257 + ls]);
        if (LEXT[ls]) bw_put(w, (uint32_t)(t[i].len - LBASE[ls]), LEXT[ls]);
        bw_put(w, dc[ds], dl[ds]);
        if (DEXT[ds]) bw_put(w, (uint32_t)(t[i].dist - DBASE[ds]), DEXT[ds]);
    }
    bw_put(w, lc[256], ll[256]);
}

static inline uint32_t hash3(const uint8_t* p) {
    return ((uint32_t)p[0] << 10 ^ (uint32_t)p[1] << 5 ^ p[2]) & (HASH_SIZE - 1);
}

/* the longest match for in[pos..] in the window: its length (0: none), *dist */
static int longest(const uint8_t* in, uint32_t len, uint32_t pos, const int32_t* head, const int32_t* prev, int* dist) {
    int best = 0;
    uint32_t max = len - pos < MAX_MATCH ? len - pos : MAX_MATCH;
    if (max < MIN_MATCH) return 0;
    int32_t cand = head[hash3(in + pos)];
    for (int chain = 0; cand >= 0 && chain < CHAIN_MAX; chain++) {
        uint32_t c = (uint32_t)cand;
        if (c >= pos || pos - c > WSIZE) break;
        if (in[c + best] == in[pos + best] && in[c] == in[pos]) {
            uint32_t k = 0;
            while (k < max && in[c + k] == in[pos + k]) k++;
            if ((int)k > best) {
                best = (int)k;
                *dist = (int)(pos - c);
                if (k == max) break;
            }
        }
        cand = prev[c & WMASK];
    }
    return best >= MIN_MATCH ? best : 0;
}

uint8_t* deflate_compress(const uint8_t* in, uint32_t len, uint32_t* out_len) {
    bw_t w = { 0 };
    int32_t* head = (int32_t*)kmalloc(HASH_SIZE * sizeof(int32_t));
    int32_t* prev = (int32_t*)kmalloc(WSIZE * sizeof(int32_t));
    tok_t* toks = (tok_t*)kmalloc(BLOCK_TOKENS * sizeof(tok_t));
    scratch_t* sc = (scratch_t*)kmalloc(sizeof(scratch_t));
    if (!head || !prev || !toks || !sc) { kfree(head); kfree(prev); kfree(toks); kfree(sc); return NULL; }
    for (uint32_t i = 0; i < HASH_SIZE; i++) head[i] = -1;

    uint32_t pos = 0, block_start = 0;
    int nt = 0;
#define INSERT(p) do { if ((p) + MIN_MATCH <= len) { uint32_t h_ = hash3(in + (p)); prev[(p) & WMASK] = head[h_]; head[h_] = (int32_t)(p); } } while (0)
    while (pos < len) {
        int dist = 0, mlen = longest(in, len, pos, head, prev, &dist);
        if (mlen && mlen < GOOD_LAZY && pos + 1 < len) {       /* lazy: a longer match one byte on? */
            INSERT(pos);
            int d2 = 0, m2 = longest(in, len, pos + 1, head, prev, &d2);
            if (m2 > mlen) {
                toks[nt++] = (tok_t){ in[pos], 0 };
                pos++;
                mlen = m2;
                dist = d2;
            } else {
                for (int k = 1; k < mlen; k++) INSERT(pos + (uint32_t)k);
                toks[nt++] = (tok_t){ (uint16_t)mlen, (uint16_t)dist };
                pos += (uint32_t)mlen;
                goto next;
            }
        }
        if (mlen) {
            for (int k = 0; k < mlen; k++) INSERT(pos + (uint32_t)k);
            toks[nt++] = (tok_t){ (uint16_t)mlen, (uint16_t)dist };
            pos += (uint32_t)mlen;
        } else {
            INSERT(pos);
            toks[nt++] = (tok_t){ in[pos], 0 };
            pos++;
        }
    next:
        if (nt >= BLOCK_TOKENS - 2) {
            write_block(sc, &w, toks, nt, in, block_start, pos, pos >= len);
            block_start = pos;
            nt = 0;
        }
    }
#undef INSERT
    if (nt || len == 0 || block_start < len) write_block(sc, &w, toks, nt, in, block_start, pos, 1);
    bw_align(&w);
    kfree(head);
    kfree(prev);
    kfree(toks);
    kfree(sc);
    if (w.oom) { kfree(w.buf); return NULL; }
    if (!w.buf) w.buf = (uint8_t*)kmalloc(1);
    *out_len = w.len;
    return w.buf;
}
