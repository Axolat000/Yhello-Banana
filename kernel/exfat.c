#include "exfat.h"
#include "fs.h"
#include "kheap.h"
#include "kstring.h"
#include "rtc.h"
#include "serial.h"

/*
 * exFAT, read and write (Microsoft's exFAT specification).
 *
 * As in kernel/fat32.c, the tree is read once at mount time and mirrored
 * into the filesystem tree; every node remembers where its entry set (File,
 * Stream Extension, File Name entries) starts in its parent folder's data,
 * so a change rewrites it in place with a fresh checksum. Free clusters are
 * the allocation bitmap's business (kept in RAM, written back as it
 * changes); the FAT only matters for files that are not one contiguous run
 * (NoFatChain clear). New data goes into one run of clusters when one is
 * free - no FAT entries to write - and a file that has to grow past its run
 * is turned into a FAT chain.
 */

#define MAX_DEPTH   16
#define FATC        8                   /* cached FAT sectors */
#define IO_MAX      256                 /* sectors per device request */

#define T_END       0x00
#define T_BITMAP    0x81
#define T_UPCASE    0x82
#define T_LABEL     0x83
#define T_FILE      0x85
#define T_STREAM    0xC0
#define T_NAME      0xC1
#define ATTR_DIR    0x10
#define F_ALLOC     0x01                /* GeneralSecondaryFlags: AllocationPossible */
#define F_NOFAT     0x02                /* NoFatChain: the data is one run of clusters */

#define EOC         0xFFFFFFFFu

typedef struct {
    int      used, is_dir;
    uint32_t parent;                    /* node of the folder holding its entries */
    uint32_t first;                     /* first cluster, 0 = no data */
    uint64_t size;                      /* DataLength */
    uint64_t valid;                     /* ValidDataLength (what was written) */
    int      nofat;
    uint32_t pos;                       /* byte offset of its File entry in the parent's data */
    uint8_t  nsec;                      /* secondary entries after it */
    uint8_t  namelen;                   /* its name in UTF-16 units, and the name's hash: */
    uint16_t hash;                      /* a new name is compared only with names of the same hash */
} xnode_t;

typedef struct {
    blockdev_t* bd;
    uint32_t base;                      /* first device sector of the volume */
    uint32_t bps, ratio;                /* bytes per (exFAT) sector, device sectors in one */
    uint32_t spc, clus_bytes;
    uint32_t fat_lba, fat_len;          /* in exFAT sectors, from base */
    uint32_t heap_lba;                  /* cluster 2 */
    uint32_t nclusters;                 /* valid numbers: 2 .. nclusters + 1 */
    uint32_t root;
    int      readonly;
    char     label[48];                 /* UTF-8 */
    /* the allocation bitmap, in RAM; its clusters on the volume */
    uint8_t* bmp;
    uint32_t bmp_bytes;
    uint32_t* bmp_clus;
    uint32_t bmp_nclus;
    uint32_t bmp_dirty_lo, bmp_dirty_hi; /* exFAT sectors of it to write back */
    uint32_t nfree;
    uint32_t hint;
    uint16_t* upcase;                   /* 65536 entries */
    uint32_t fc_idx[FATC];
    int      fc_ok[FATC], fc_dirty[FATC];
    uint8_t* fc;                        /* FATC sectors */
    uint8_t* sec;                       /* one-sector scratch */
    xnode_t* nodes;
    uint32_t nnodes, cap;
    int      mnt;
    int      skipped;
    int      io_err;
} xvol_t;

static xvol_t* g_vols[FS_MAX_MOUNTS + 1];

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }
static void wr16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t* p, uint32_t v) { wr16(p, (uint16_t)v); wr16(p + 2, (uint16_t)(v >> 16)); }
static void wr64(uint8_t* p, uint64_t v) { wr32(p, (uint32_t)v); wr32(p + 4, (uint32_t)(v >> 32)); }

/* ── sectors ───────────────────────────────────────────────────────── */

/* n exFAT sectors at volume sector s (in pieces the device takes) */
static int vread(xvol_t* v, uint32_t s, uint32_t n, void* buf) {
    uint8_t* b = (uint8_t*)buf;
    uint32_t lba = v->base + s * v->ratio, cnt = n * v->ratio;
    while (cnt) {
        uint32_t k = cnt < IO_MAX ? cnt : IO_MAX;
        if (!v->bd->present || v->bd->read(v->bd, lba, k, b) != 0) { v->io_err = 1; return -1; }
        lba += k; cnt -= k; b += k * 512u;
    }
    return 0;
}

static int vwrite(xvol_t* v, uint32_t s, uint32_t n, const void* buf) {
    if (v->readonly) { v->io_err = 1; return -1; }
    const uint8_t* b = (const uint8_t*)buf;
    uint32_t lba = v->base + s * v->ratio, cnt = n * v->ratio;
    while (cnt) {
        uint32_t k = cnt < IO_MAX ? cnt : IO_MAX;
        if (!v->bd->present || v->bd->write(v->bd, lba, k, b) != 0) { v->io_err = 1; return -1; }
        lba += k; cnt -= k; b += k * 512u;
    }
    return 0;
}

static int valid_clus(const xvol_t* v, uint32_t c) { return c >= 2 && c <= v->nclusters + 1; }
static uint32_t clus_sec(const xvol_t* v, uint32_t c) { return v->heap_lba + (c - 2) * v->spc; }

/* ── the FAT ───────────────────────────────────────────────────────── */

static int fc_flush_slot(xvol_t* v, int s) {
    if (!v->fc_ok[s] || !v->fc_dirty[s]) return 0;
    if (vwrite(v, v->fat_lba + v->fc_idx[s], 1, v->fc + (size_t)s * v->bps) != 0) return -1;
    v->fc_dirty[s] = 0;
    return 0;
}

static uint8_t* fat_sector(xvol_t* v, uint32_t idx, int* slot) {
    for (int s = 0; s < FATC; s++)
        if (v->fc_ok[s] && v->fc_idx[s] == idx) { *slot = s; return v->fc + (size_t)s * v->bps; }
    int s = (int)(idx % FATC);
    if (fc_flush_slot(v, s) != 0) return NULL;
    v->fc_ok[s] = 0;
    if (vread(v, v->fat_lba + idx, 1, v->fc + (size_t)s * v->bps) != 0) return NULL;
    v->fc_ok[s] = 1;
    v->fc_idx[s] = idx;
    *slot = s;
    return v->fc + (size_t)s * v->bps;
}

static uint32_t fat_get(xvol_t* v, uint32_t c) {
    int s;
    uint8_t* b = fat_sector(v, c * 4 / v->bps, &s);
    return b ? rd32(b + c * 4 % v->bps) : EOC;
}

static int fat_set(xvol_t* v, uint32_t c, uint32_t val) {
    int s;
    uint8_t* b = fat_sector(v, c * 4 / v->bps, &s);
    if (!b) return -1;
    wr32(b + c * 4 % v->bps, val);
    v->fc_dirty[s] = 1;
    return 0;
}

/* the cluster after c of a node's data */
static uint32_t next_clus(xvol_t* v, int nofat, uint32_t c) {
    if (nofat) return c + 1;
    uint32_t n = fat_get(v, c);
    return valid_clus(v, n) ? n : EOC;
}

/* ── the allocation bitmap ─────────────────────────────────────────── */

static int bmp_get(const xvol_t* v, uint32_t c) { uint32_t i = c - 2; return (v->bmp[i >> 3] >> (i & 7)) & 1; }

static void bmp_set(xvol_t* v, uint32_t c, int used) {
    uint32_t i = c - 2;
    int was = (v->bmp[i >> 3] >> (i & 7)) & 1;
    if (was == used) return;
    if (used) { v->bmp[i >> 3] |= (uint8_t)(1u << (i & 7)); v->nfree--; }
    else { v->bmp[i >> 3] &= (uint8_t)~(1u << (i & 7)); v->nfree++; }
    uint32_t sec = (i >> 3) / v->bps;
    if (sec < v->bmp_dirty_lo) v->bmp_dirty_lo = sec;
    if (sec + 1 > v->bmp_dirty_hi) v->bmp_dirty_hi = sec + 1;
}

/* everything changed goes to the volume: the FAT sectors, the bitmap */
static int flush(xvol_t* v) {
    int rc = 0;
    for (int s = 0; s < FATC; s++) if (fc_flush_slot(v, s) != 0) rc = -1;
    uint32_t per = v->spc;                                  /* bitmap sectors per cluster */
    for (uint32_t sec = v->bmp_dirty_lo; sec < v->bmp_dirty_hi; sec++) {
        uint32_t ci = sec / per;
        if (ci >= v->bmp_nclus) break;
        uint32_t off = sec * v->bps;
        if (off >= v->bmp_bytes) break;
        if (off + v->bps <= v->bmp_bytes) {
            if (vwrite(v, clus_sec(v, v->bmp_clus[ci]) + sec % per, 1, v->bmp + off) != 0) rc = -1;
        } else {                                            /* the last, partly used sector */
            memset(v->sec, 0, v->bps);
            memcpy(v->sec, v->bmp + off, v->bmp_bytes - off);
            if (vwrite(v, clus_sec(v, v->bmp_clus[ci]) + sec % per, 1, v->sec) != 0) rc = -1;
        }
    }
    v->bmp_dirty_lo = ~0u;
    v->bmp_dirty_hi = 0;
    return rc;
}

/* n free clusters in a row (marked used), from the hint on: the first, 0 if none */
static uint32_t alloc_run(xvol_t* v, uint32_t n) {
    if (!n || n > v->nfree) return 0;
    uint32_t last = v->nclusters + 1;
    for (int pass = 0; pass < 2; pass++) {
        uint32_t start = pass == 0 ? v->hint : 2;
        uint32_t len = 0, from = 0;
        for (uint32_t c = start; c <= last; c++) {
            if (bmp_get(v, c)) { len = 0; continue; }
            if (!len) from = c;
            if (++len == n) {
                for (uint32_t k = from; k < from + n; k++) bmp_set(v, k, 1);
                v->hint = from + n <= last ? from + n : 2;
                return from;
            }
        }
    }
    return 0;
}

/* the n clusters from `from` on, if every one is free (marked used): from, else 0 */
static uint32_t take_exact(xvol_t* v, uint32_t from, uint32_t n) {
    if (!valid_clus(v, from) || !valid_clus(v, from + n - 1)) return 0;
    for (uint32_t c = from; c < from + n; c++) if (bmp_get(v, c)) return 0;
    for (uint32_t c = from; c < from + n; c++) bmp_set(v, c, 1);
    return from;
}

/* the clusters of a node's data go back to the bitmap */
static void free_data(xvol_t* v, uint32_t first, int nofat, uint64_t size) {
    if (!valid_clus(v, first)) return;
    uint64_t n = (size + v->clus_bytes - 1) / v->clus_bytes;
    uint32_t c = first;
    for (uint64_t k = 0; k < n && valid_clus(v, c); k++) {
        uint32_t nx = next_clus(v, nofat, c);
        bmp_set(v, c, 0);
        c = nx;
    }
}

/* n clusters for new data: one run if there is one (*nofat = 1), else
 * scattered ones chained in the FAT (*nofat = 0); the first, 0 if full */
static uint32_t alloc_data(xvol_t* v, uint32_t n, int* nofat) {
    uint32_t r = alloc_run(v, n);
    if (r) { *nofat = 1; return r; }
    if (n > v->nfree) return 0;
    uint32_t first = 0, prev = 0;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t c = alloc_run(v, 1);
        if (!c) { if (first) free_data(v, first, 0, (uint64_t)k * v->clus_bytes); return 0; }
        if (prev) fat_set(v, prev, c);
        else first = c;
        prev = c;
    }
    fat_set(v, prev, EOC);
    *nofat = 0;
    return first;
}

/* a run that turns into a FAT chain (to grow where the run cannot) */
static void make_chain(xvol_t* v, uint32_t first, uint32_t n) {
    for (uint32_t k = 0; k + 1 < n; k++) fat_set(v, first + k, first + k + 1);
    if (n) fat_set(v, first + n - 1, EOC);
}

/* ── data ──────────────────────────────────────────────────────────── */

/* reads size bytes of a node's data (whole sectors through a scratch at the end) */
static int read_data(xvol_t* v, uint32_t c, int nofat, uint8_t* out, uint64_t size) {
    uint64_t done = 0;
    while (done < size) {
        if (!valid_clus(v, c)) return -1;
        uint32_t run = 1, last = c;
        while ((uint64_t)(run + 1) * v->clus_bytes <= size - done) {
            uint32_t nx = next_clus(v, nofat, last);
            if (nx != last + 1 || !valid_clus(v, nx)) break;
            last = nx;
            run++;
        }
        uint64_t want = size - done;
        uint64_t have = (uint64_t)run * v->clus_bytes;
        if (have > want) have = want;
        uint32_t whole = (uint32_t)(have / v->bps), part = (uint32_t)(have % v->bps);
        if (whole && vread(v, clus_sec(v, c), whole, out + done) != 0) return -1;
        if (part) {
            if (vread(v, clus_sec(v, c) + whole, 1, v->sec) != 0) return -1;
            memcpy(out + done + (uint64_t)whole * v->bps, v->sec, part);
        }
        done += have;
        if (done < size) c = next_clus(v, nofat, last);
    }
    return 0;
}

/* writes len bytes from byte offset off of the data starting at cluster c
 * (the clusters exist already) */
static int write_data(xvol_t* v, uint32_t c, int nofat, uint64_t off, const uint8_t* d, uint64_t len) {
    while (off >= v->clus_bytes) {                          /* to the cluster holding off */
        c = next_clus(v, nofat, c);
        off -= v->clus_bytes;
    }
    while (len) {
        if (!valid_clus(v, c)) return -1;
        uint32_t s = clus_sec(v, c) + (uint32_t)(off / v->bps);
        uint32_t in = (uint32_t)(off % v->bps);
        uint64_t room = v->clus_bytes - off;
        uint64_t take = len < room ? len : room;
        if (in || take < v->bps) {                          /* a partial sector */
            uint32_t k = v->bps - in < take ? v->bps - in : (uint32_t)take;
            if (vread(v, s, 1, v->sec) != 0) return -1;
            memcpy(v->sec + in, d, k);
            if (vwrite(v, s, 1, v->sec) != 0) return -1;
            d += k; len -= k; off += k;
        } else {                                            /* whole sectors, straight from the data */
            uint32_t n = (uint32_t)(take / v->bps);
            if (vwrite(v, s, n, d) != 0) return -1;
            d += (uint64_t)n * v->bps; len -= (uint64_t)n * v->bps; off += (uint64_t)n * v->bps;
        }
        if (off >= v->clus_bytes) { off = 0; c = next_clus(v, nofat, c); }
    }
    return 0;
}

/* zeroes n clusters of a run or chain (a folder's new clusters) */
static int zero_clusters(xvol_t* v, uint32_t c, int nofat, uint32_t n) {
    memset(v->sec, 0, v->bps);
    for (uint32_t k = 0; k < n; k++) {
        if (!valid_clus(v, c)) return -1;
        for (uint32_t s = 0; s < v->spc; s++)
            if (vwrite(v, clus_sec(v, c) + s, 1, v->sec) != 0) return -1;
        c = next_clus(v, nofat, c);
    }
    return 0;
}

/* ── names ─────────────────────────────────────────────────────────── */

static uint16_t up(const xvol_t* v, uint16_t c) {
    if (v->upcase) return v->upcase[c];
    return c >= 'a' && c <= 'z' ? (uint16_t)(c - 32) : c;
}

/* UTF-8 -> UTF-16 (up to cap units): how many, -1 if it does not fit or is bad */
static int to_utf16(const char* s, uint16_t* out, int cap) {
    int n = 0;
    const uint8_t* p = (const uint8_t*)s;
    while (*p) {
        uint32_t cp;
        if (*p < 0x80) cp = *p++;
        else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((uint32_t)(*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3;
        } else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) {
            cp = ((uint32_t)(*p & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            p += 4;
        } else return -1;
        if (cp >= 0x10000) {
            if (n + 2 > cap) return -1;
            cp -= 0x10000;
            out[n++] = (uint16_t)(0xD800 | (cp >> 10));
            out[n++] = (uint16_t)(0xDC00 | (cp & 0x3FF));
        } else {
            if (n + 1 > cap) return -1;
            out[n++] = (uint16_t)cp;
        }
    }
    return n;
}

/* UTF-16 -> UTF-8 into out (cap bytes; cut short at a whole character) */
static void to_utf8(const uint16_t* u, int n, char* out, int cap) {
    int o = 0;
    for (int i = 0; i < n; i++) {
        uint32_t cp = u[i];
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < n && u[i + 1] >= 0xDC00 && u[i + 1] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (u[i + 1] - 0xDC00);
            i++;
        }
        char b[4];
        int k;
        if (cp < 0x80) { b[0] = (char)cp; k = 1; }
        else if (cp < 0x800) { b[0] = (char)(0xC0 | (cp >> 6)); b[1] = (char)(0x80 | (cp & 0x3F)); k = 2; }
        else if (cp < 0x10000) { b[0] = (char)(0xE0 | (cp >> 12)); b[1] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[2] = (char)(0x80 | (cp & 0x3F)); k = 3; }
        else { b[0] = (char)(0xF0 | (cp >> 18)); b[1] = (char)(0x80 | ((cp >> 12) & 0x3F)); b[2] = (char)(0x80 | ((cp >> 6) & 0x3F)); b[3] = (char)(0x80 | (cp & 0x3F)); k = 4; }
        if (o + k > cap - 1) break;
        for (int j = 0; j < k; j++) out[o++] = b[j];
    }
    out[o] = 0;
}

static uint16_t name_hash(const xvol_t* v, const uint16_t* name, int n) {
    uint16_t h = 0;
    for (int i = 0; i < n; i++) {
        uint16_t c = up(v, name[i]);
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c & 0xFF));
        h = (uint16_t)(((h & 1) ? 0x8000 : 0) + (h >> 1) + (c >> 8));
    }
    return h;
}

static uint16_t set_checksum(const uint8_t* set, int entries) {
    uint16_t sum = 0;
    for (int i = 0; i < entries * 32; i++) {
        if (i == 2 || i == 3) continue;
        sum = (uint16_t)(((sum & 1) ? 0x8000 : 0) + (sum >> 1) + set[i]);
    }
    return sum;
}

/* an exFAT timestamp (local time, as FAT's) for t (rtc_now() seconds) */
static uint32_t stamp(uint32_t t) {
    rtc_datetime_t d;
    rtc_split_time(t, &d);
    if (d.year < 1980) return 0;
    return ((uint32_t)(d.year - 1980) << 25) | ((uint32_t)d.month << 21) | ((uint32_t)d.day << 16) |
           ((uint32_t)d.hour << 11) | ((uint32_t)d.minute << 5) | (uint32_t)(d.second / 2);
}

static uint32_t unstamp(uint32_t s) {
    if (!s) return 0;
    return rtc_make_time(1980 + (int)(s >> 25), (int)((s >> 21) & 15), (int)((s >> 16) & 31),
                         (int)((s >> 11) & 31), (int)((s >> 5) & 63), (int)(s & 31) * 2);
}

/* ── folders ───────────────────────────────────────────────────────── */

static int new_node(xvol_t* v) {
    if (v->nnodes == v->cap) {
        uint32_t cap = v->cap ? v->cap * 2 : 64;
        xnode_t* nn = (xnode_t*)krealloc(v->nodes, cap * sizeof(xnode_t));
        if (!nn) return -1;
        v->nodes = nn;
        v->cap = cap;
    }
    memset(&v->nodes[v->nnodes], 0, sizeof(xnode_t));
    return (int)v->nnodes++;
}

/* the volume sector holding byte pos of a folder's data (-1: past its end) */
static int64_t dir_sector(xvol_t* v, uint32_t dnode, uint32_t pos) {
    xnode_t* d = &v->nodes[dnode];
    uint32_t c = d->first;
    for (uint32_t k = pos / v->clus_bytes; k; k--) {
        c = next_clus(v, d->nofat, c);
        if (!valid_clus(v, c)) return -1;
    }
    if (!valid_clus(v, c)) return -1;
    return clus_sec(v, c) + (pos % v->clus_bytes) / v->bps;
}

static int dir_rw(xvol_t* v, uint32_t dnode, uint32_t pos, uint8_t* ent, int write) {
    int64_t s = dir_sector(v, dnode, pos);
    if (s < 0 || vread(v, (uint32_t)s, 1, v->sec) != 0) return -1;
    uint32_t in = pos % v->bps;
    if (!write) { memcpy(ent, v->sec + in, 32); return 0; }
    memcpy(v->sec + in, ent, 32);
    return vwrite(v, (uint32_t)s, 1, v->sec);
}

/* a folder's data in RAM (the root's: its whole chain) */
static uint8_t* dir_load(xvol_t* v, uint32_t dnode, uint32_t* len) {
    xnode_t* d = &v->nodes[dnode];
    uint64_t size = d->size;
    if (dnode == 0) {                                       /* the root has no length: count its chain */
        uint32_t n = 0;
        for (uint32_t c = d->first; valid_clus(v, c) && n < 65536; c = next_clus(v, 0, c)) n++;
        size = (uint64_t)n * v->clus_bytes;
        d->size = d->valid = size;
    }
    if (!size || size > (64u << 20)) return NULL;
    uint8_t* b = (uint8_t*)kmalloc((size_t)size);
    if (!b) return NULL;
    if (read_data(v, d->first, d->nofat, b, size) != 0) { kfree(b); return NULL; }
    *len = (uint32_t)size;
    return b;
}

/* rewrites node n's entry set from what n says (sizes, clusters), with a new checksum */
static int update_set(xvol_t* v, uint32_t node, int touch) {
    xnode_t* n = &v->nodes[node];
    if (node == 0) return 0;                                /* (the root has no entries) */
    int cnt = n->nsec + 1;
    uint8_t set[19 * 32];
    if (cnt > 19) return -1;
    for (int i = 0; i < cnt; i++)
        if (dir_rw(v, n->parent, n->pos + (uint32_t)i * 32, set + i * 32, 0) != 0) return -1;
    if (set[0] != T_FILE || set[32] != T_STREAM) return -1;
    uint8_t* st = set + 32;
    st[1] = (uint8_t)(F_ALLOC | (n->nofat && n->first ? F_NOFAT : 0));
    wr64(st + 8, n->valid);
    wr32(st + 20, n->first);
    wr64(st + 24, n->size);
    if (touch) {
        uint32_t now = stamp(rtc_now());
        wr32(set + 12, now);                                /* modified */
        wr32(set + 16, now);                                /* accessed */
        set[21] = 0;
        set[23] = 0;
    }
    wr16(set + 2, set_checksum(set, cnt));
    for (int i = 0; i < cnt; i++)
        if (dir_rw(v, n->parent, n->pos + (uint32_t)i * 32, set + i * 32, 1) != 0) return -1;
    return 0;
}

/* a folder grows by one cluster (zeroed); its length in its own entry set */
static int dir_grow(xvol_t* v, uint32_t dnode) {
    xnode_t* d = &v->nodes[dnode];
    uint32_t have = (uint32_t)((d->size + v->clus_bytes - 1) / v->clus_bytes);
    uint32_t last = d->first;
    for (uint32_t k = 1; k < have; k++) last = next_clus(v, d->nofat, last);
    uint32_t c = d->nofat ? take_exact(v, last + 1, 1) : 0;    /* right after the run: it stays a run */
    if (!c) {
        if (d->nofat) { make_chain(v, d->first, have); d->nofat = 0; }
        c = alloc_run(v, 1);
        if (!c) return -1;
        fat_set(v, last, c);
        fat_set(v, c, EOC);
    }
    d = &v->nodes[dnode];
    if (zero_clusters(v, c, 1, 1) != 0) return -1;
    d->size += v->clus_bytes;
    d->valid = d->size;
    return update_set(v, dnode, 0);
}

/* `need` free entries in a row in folder dnode (growing it if needed): their offset */
static int64_t dir_room(xvol_t* v, uint32_t dnode, int need) {
    for (int tries = 0; tries < 2; tries++) {
        uint32_t len;
        uint8_t* d = dir_load(v, dnode, &len);
        if (!d) return -1;
        int run = 0;
        for (uint32_t p = 0; p + 32 <= len; p += 32) {
            uint8_t t = d[p];
            if (t == T_END || !(t & 0x80)) {                /* unused or deleted */
                if (++run == need) { kfree(d); return (int64_t)(p + 32 - (uint32_t)need * 32); }
            } else run = 0;
        }
        kfree(d);
        if (dir_grow(v, dnode) != 0) return -1;
    }
    return -1;
}

/* is name already in folder dnode (exFAT compares names up-cased)? Only
 * names of the same length and hash are read from the volume - not every
 * name in the folder each time (300 new files took minutes so) */
static int name_taken(xvol_t* v, uint32_t dnode, const uint16_t* name, int n) {
    uint16_t h = name_hash(v, name, n);
    for (uint32_t i = 1; i < v->nnodes; i++) {
        xnode_t* x = &v->nodes[i];
        if (!x->used || x->parent != dnode || x->namelen != n || x->hash != h) continue;
        uint16_t other[255];
        int on = 0;
        int cnt = x->nsec + 1;
        for (int k = 2; k < cnt && on < 255; k++) {
            uint8_t e[32];
            if (dir_rw(v, dnode, x->pos + (uint32_t)k * 32, e, 0) != 0) return 1;
            for (int j = 0; j < 15 && on < 255; j++) other[on++] = rd16(e + 2 + j * 2);
        }
        uint8_t st[32];
        if (dir_rw(v, dnode, x->pos + 32, st, 0) != 0) return 1;
        on = st[3] < on ? st[3] : on;
        if (on != n) continue;
        int same = 1;
        for (int j = 0; j < n && same; j++) if (up(v, other[j]) != up(v, name[j])) same = 0;
        if (same) return 1;
    }
    return 0;
}

/* writes an entry set for name into folder parent (file + stream from
 * tmpl's first two entries): its offset in *pos, its secondaries in *nsec */
static int add_set(xvol_t* v, uint32_t parent, const char* name, const uint8_t* tmpl, uint32_t* pos, uint8_t* nsec,
                   uint8_t* namelen, uint16_t* hash) {
    uint16_t u[255];
    int n = to_utf16(name, u, 255);
    if (n <= 0) return -1;
    for (int i = 0; i < n; i++)
        if (u[i] < 0x20 || u[i] == '"' || u[i] == '*' || u[i] == '/' || u[i] == ':' || u[i] == '<' ||
            u[i] == '>' || u[i] == '?' || u[i] == '\\' || u[i] == '|') return -1;
    if (name_taken(v, parent, u, n)) return -1;
    int nn = (n + 14) / 15;
    int cnt = 2 + nn;
    uint8_t set[19 * 32];
    memset(set, 0, sizeof(set));
    memcpy(set, tmpl, 64);
    set[0] = T_FILE;
    set[1] = (uint8_t)(cnt - 1);
    set[32] = T_STREAM;
    set[32 + 3] = (uint8_t)n;
    wr16(set + 32 + 4, name_hash(v, u, n));
    for (int k = 0; k < nn; k++) {
        uint8_t* e = set + (2 + k) * 32;
        e[0] = T_NAME;
        for (int j = 0; j < 15 && k * 15 + j < n; j++) wr16(e + 2 + j * 2, u[k * 15 + j]);
    }
    wr16(set + 2, set_checksum(set, cnt));
    int64_t at = dir_room(v, parent, cnt);
    if (at < 0) return -1;
    for (int i = 0; i < cnt; i++)
        if (dir_rw(v, parent, (uint32_t)at + (uint32_t)i * 32, set + i * 32, 1) != 0) return -1;
    *pos = (uint32_t)at;
    *nsec = (uint8_t)(cnt - 1);
    *namelen = (uint8_t)n;
    *hash = rd16(set + 32 + 4);
    return 0;
}

/* the entries of a set are no longer in use */
static int del_set(xvol_t* v, uint32_t parent, uint32_t pos, int cnt) {
    for (int i = 0; i < cnt; i++) {
        uint8_t e[32];
        if (dir_rw(v, parent, pos + (uint32_t)i * 32, e, 0) != 0) return -1;
        e[0] &= 0x7F;
        if (dir_rw(v, parent, pos + (uint32_t)i * 32, e, 1) != 0) return -1;
    }
    return 0;
}

/* ── fs mount ops ──────────────────────────────────────────────────── */

static int node_ok(xvol_t* v, uint32_t node) { return node < v->nnodes && v->nodes[node].used; }

static int op_read(void* ctx, uint32_t node, uint8_t* buf, uint32_t size) {
    xvol_t* v = (xvol_t*)ctx;
    if (!node_ok(v, node)) return -1;
    v->io_err = 0;
    xnode_t* n = &v->nodes[node];
    uint64_t have = n->valid < size ? n->valid : size;
    if (have && read_data(v, n->first, n->nofat, buf, have) != 0) return -1;
    if (have < size) memset(buf + have, 0, size - (uint32_t)have);   /* (past ValidDataLength: zeros) */
    return 0;
}

static int op_write(void* ctx, uint32_t node, const void* data, uint32_t len) {
    xvol_t* v = (xvol_t*)ctx;
    if (node == 0 || !node_ok(v, node) || v->readonly) return -1;
    v->io_err = 0;
    xnode_t* n = &v->nodes[node];
    free_data(v, n->first, n->nofat, n->size);
    n->first = 0;
    n->nofat = 0;
    n->size = n->valid = 0;
    int rc = 0;
    if (len) {
        int nofat;
        uint32_t first = alloc_data(v, (len + v->clus_bytes - 1) / v->clus_bytes, &nofat);
        if (!first) rc = -1;
        else {
            n = &v->nodes[node];
            n->first = first;
            n->nofat = nofat;
            if (write_data(v, first, nofat, 0, (const uint8_t*)data, len) != 0) rc = -1;
            n->size = n->valid = len;
        }
    }
    if (rc != 0) {                                          /* (the file ends up empty, as in fat32.c) */
        n = &v->nodes[node];
        free_data(v, n->first, n->nofat, n->size);
        n->first = 0; n->nofat = 0; n->size = n->valid = 0;
    }
    if (update_set(v, node, 1) != 0) rc = -1;
    if (flush(v) != 0) rc = -1;
    return rc;
}

static int op_append(void* ctx, uint32_t node, uint32_t old_size, const void* data, uint32_t len) {
    xvol_t* v = (xvol_t*)ctx;
    if (node == 0 || !node_ok(v, node) || v->readonly) return -1;
    xnode_t* n = &v->nodes[node];
    if (!n->first || !old_size) return op_write(ctx, node, data, len);   /* (nothing there yet) */
    if (!len) return 0;
    v->io_err = 0;
    uint32_t have = (uint32_t)((n->size + v->clus_bytes - 1) / v->clus_bytes);
    uint64_t total = (uint64_t)old_size + len;
    uint32_t need = (uint32_t)((total + v->clus_bytes - 1) / v->clus_bytes);
    int rc = 0;
    if (need > have) {
        uint32_t more = need - have;
        uint32_t last = n->first;
        for (uint32_t k = 1; k < have; k++) last = next_clus(v, n->nofat, last);
        uint32_t c = n->nofat ? take_exact(v, last + 1, more) : 0;   /* right after the run: still a run */
        if (!c) {
            int nf;
            c = alloc_data(v, more, &nf);
            if (!c) rc = -1;
            else {
                n = &v->nodes[node];
                if (n->nofat) { make_chain(v, n->first, have); n->nofat = 0; }
                if (nf) make_chain(v, c, more);
                fat_set(v, last, c);
            }
        }
    }
    n = &v->nodes[node];
    if (rc == 0) {
        if (write_data(v, n->first, n->nofat, old_size, (const uint8_t*)data, len) != 0) rc = -1;
        else n->size = n->valid = total;
    }
    if (update_set(v, node, 1) != 0) rc = -1;
    if (flush(v) != 0) rc = -1;
    return rc;
}

static int op_create(void* ctx, uint32_t parent, const char* name, int is_dir, uint32_t* node) {
    xvol_t* v = (xvol_t*)ctx;
    if (!node_ok(v, parent) || !v->nodes[parent].is_dir || v->readonly) return -1;
    v->io_err = 0;
    int ni = new_node(v);
    if (ni < 0) return -1;
    uint8_t tmpl[64];
    memset(tmpl, 0, sizeof(tmpl));
    uint32_t now = stamp(rtc_now());
    wr16(tmpl + 4, is_dir ? ATTR_DIR : 0x20);               /* directory / archive */
    wr32(tmpl + 8, now);
    wr32(tmpl + 12, now);
    wr32(tmpl + 16, now);
    uint32_t first = 0;
    uint64_t size = 0;
    if (is_dir) {                                           /* a folder has one (zeroed) cluster */
        first = alloc_run(v, 1);
        if (!first || zero_clusters(v, first, 1, 1) != 0) { if (first) bmp_set(v, first, 0); flush(v); return -1; }
        size = v->clus_bytes;
    }
    uint8_t* st = tmpl + 32;
    st[1] = (uint8_t)(F_ALLOC | (first ? F_NOFAT : 0));
    wr64(st + 8, size);
    wr32(st + 20, first);
    wr64(st + 24, size);
    uint32_t pos;
    uint8_t nsec, nlen;
    uint16_t hash;
    if (add_set(v, parent, name, tmpl, &pos, &nsec, &nlen, &hash) != 0) {
        if (first) bmp_set(v, first, 0);
        flush(v);
        return -1;
    }
    xnode_t* n = &v->nodes[ni];
    n->used = 1;
    n->is_dir = is_dir;
    n->parent = parent;
    n->first = first;
    n->nofat = first != 0;
    n->size = n->valid = size;
    n->pos = pos;
    n->nsec = nsec;
    n->namelen = nlen;
    n->hash = hash;
    *node = (uint32_t)ni;
    return flush(v);
}

static int op_remove(void* ctx, uint32_t node, int is_dir) {
    xvol_t* v = (xvol_t*)ctx;
    (void)is_dir;
    if (node == 0 || !node_ok(v, node) || v->readonly) return -1;
    v->io_err = 0;
    xnode_t* n = &v->nodes[node];
    int rc = del_set(v, n->parent, n->pos, n->nsec + 1);
    n = &v->nodes[node];
    free_data(v, n->first, n->nofat, n->size);
    n->used = 0;
    if (flush(v) != 0) rc = -1;
    return rc || v->io_err ? -1 : 0;
}

static int op_rename(void* ctx, uint32_t node, int is_dir, uint32_t new_parent, const char* new_name) {
    xvol_t* v = (xvol_t*)ctx;
    (void)is_dir;
    if (node == 0 || !node_ok(v, node) || !node_ok(v, new_parent) || !v->nodes[new_parent].is_dir || v->readonly) return -1;
    v->io_err = 0;
    xnode_t old = v->nodes[node];
    uint8_t tmpl[64];
    if (dir_rw(v, old.parent, old.pos, tmpl, 0) != 0 || dir_rw(v, old.parent, old.pos + 32, tmpl + 32, 0) != 0) return -1;
    v->nodes[node].used = 0;                                /* (its own name does not count as taken) */
    uint32_t pos;
    uint8_t nsec, nlen;
    uint16_t hash;
    int rc = add_set(v, new_parent, new_name, tmpl, &pos, &nsec, &nlen, &hash);
    v->nodes[node].used = 1;
    if (rc != 0) { flush(v); return -1; }
    if (del_set(v, old.parent, old.pos, old.nsec + 1) != 0) rc = -1;
    xnode_t* n = &v->nodes[node];
    n->parent = new_parent;
    n->pos = pos;
    n->nsec = nsec;
    n->namelen = nlen;
    n->hash = hash;
    if (flush(v) != 0) rc = -1;
    return rc || v->io_err ? -1 : 0;
}

static void op_unmount(void* ctx) {
    xvol_t* v = (xvol_t*)ctx;
    flush(v);
    if (v->mnt > 0 && v->mnt <= FS_MAX_MOUNTS && g_vols[v->mnt] == v) g_vols[v->mnt] = NULL;
    kfree(v->nodes);
    kfree(v->bmp);
    kfree(v->bmp_clus);
    kfree(v->upcase);
    kfree(v->fc);
    kfree(v->sec);
    kfree(v);
}

static const fs_mount_ops_t g_ops = {
    op_read, op_write, op_append, op_create, op_remove, op_rename, op_unmount,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,                    /* (FAT: no Unix calls) */
};

/* ── mounting ──────────────────────────────────────────────────────── */

static void scan_dir(xvol_t* v, uint32_t dnode, int fsdir, int depth) {
    uint32_t len;
    uint8_t* d = dir_load(v, dnode, &len);
    if (!d) return;
    for (uint32_t p = 0; p + 32 <= len; p += 32) {
        const uint8_t* e = d + p;
        if (e[0] == T_END) break;
        if (dnode == 0 && e[0] == T_LABEL) {
            uint16_t u[11];
            int n = e[1] > 11 ? 11 : e[1];
            for (int i = 0; i < n; i++) u[i] = rd16(e + 2 + i * 2);
            to_utf8(u, n, v->label, sizeof(v->label));
            continue;
        }
        if (e[0] != T_FILE) continue;
        int nsec = e[1];
        if (nsec < 2 || nsec > 18 || p + (uint32_t)(nsec + 1) * 32 > len) continue;
        if (set_checksum(e, nsec + 1) != rd16(e + 2)) continue;          /* a damaged set: leave it alone */
        const uint8_t* st = e + 32;
        if (st[0] != T_STREAM) continue;
        uint16_t u[255];
        int nl = st[3], have = 0;
        for (int k = 2; k <= nsec && have < nl; k++) {
            const uint8_t* ne = e + k * 32;
            if (ne[0] != T_NAME) break;
            for (int j = 0; j < 15 && have < nl; j++) u[have++] = rd16(ne + 2 + j * 2);
        }
        char name[256];
        to_utf8(u, have, name, FS_NAME_LEN);                               /* (the tree's limit) */
        p += (uint32_t)nsec * 32;
        if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        int ni = new_node(v);
        if (ni < 0) break;
        xnode_t* n = &v->nodes[ni];
        n->used = 1;
        n->is_dir = (rd16(e + 4) & ATTR_DIR) != 0;
        n->parent = dnode;
        n->first = rd32(st + 20);
        n->size = rd64(st + 24);
        n->valid = rd64(st + 8);
        if (n->valid > n->size) n->valid = n->size;
        n->nofat = (st[1] & F_NOFAT) != 0;
        n->pos = p - (uint32_t)nsec * 32;
        n->nsec = (uint8_t)nsec;
        n->namelen = st[3];
        n->hash = rd16(st + 4);
        if (n->first && !valid_clus(v, n->first)) { n->used = 0; continue; }
        uint32_t mt = unstamp(rd32(e + 12)), ct = unstamp(rd32(e + 8));
        if (n->is_dir) {
            int fd = fs_mount_add_dir(v->mnt, fsdir, name, (uint32_t)ni);
            if (fd < 0) { v->nodes[ni].used = 0; v->skipped++; continue; }
            fs_set_times(1, fd, mt, ct);
            if (depth < MAX_DEPTH) scan_dir(v, (uint32_t)ni, fd, depth + 1);
        } else {
            int fi = n->size <= FS_MAX_FILE_SIZE ? fs_mount_add_file(v->mnt, fsdir, name, (uint32_t)ni, (uint32_t)n->size) : -1;
            if (fi < 0) { v->nodes[ni].used = 0; v->skipped++; continue; }
            fs_set_times(0, fi, mt, ct);
        }
    }
    kfree(d);
}

/* the up-case table (compressed: 0xFFFF n = the next n characters map to themselves) */
static void load_upcase(xvol_t* v, uint32_t first, uint64_t size) {
    if (!valid_clus(v, first) || !size || size > 256u * 1024u) return;
    uint8_t* raw = (uint8_t*)kmalloc((size_t)size);
    uint16_t* t = (uint16_t*)kmalloc(65536 * sizeof(uint16_t));
    if (!raw || !t || read_data(v, first, 0, raw, size) != 0) { kfree(raw); kfree(t); return; }
    uint32_t ch = 0;
    for (uint64_t i = 0; i + 1 < size && ch < 65536; i += 2) {
        uint16_t w = rd16(raw + i);
        if (w == 0xFFFF && i + 3 < size) {
            uint32_t n = rd16(raw + i + 2);
            for (uint32_t k = 0; k < n && ch < 65536; k++, ch++) t[ch] = (uint16_t)ch;
            i += 2;
        } else {
            t[ch++] = w;
        }
    }
    for (; ch < 65536; ch++) t[ch] = (uint16_t)ch;
    kfree(raw);
    v->upcase = t;
}

/* the allocation bitmap: into RAM, with where its clusters are */
static int load_bitmap(xvol_t* v, uint32_t first, uint64_t size) {
    if (!valid_clus(v, first) || size < (v->nclusters + 7) / 8 || size > (64u << 20)) return -1;
    v->bmp_bytes = (uint32_t)size;
    v->bmp_nclus = (uint32_t)((size + v->clus_bytes - 1) / v->clus_bytes);
    v->bmp = (uint8_t*)kmalloc(v->bmp_bytes);
    v->bmp_clus = (uint32_t*)kmalloc(v->bmp_nclus * sizeof(uint32_t));
    if (!v->bmp || !v->bmp_clus) return -1;
    uint32_t c = first;
    for (uint32_t k = 0; k < v->bmp_nclus; k++) {
        if (!valid_clus(v, c)) return -1;
        v->bmp_clus[k] = c;
        c = next_clus(v, 0, c);
    }
    if (read_data(v, first, 0, v->bmp, size) != 0) return -1;
    v->nfree = 0;
    for (uint32_t c2 = 2; c2 <= v->nclusters + 1; c2++) if (!bmp_get(v, c2)) v->nfree++;
    v->bmp_dirty_lo = ~0u;
    return 0;
}

static void discard(xvol_t* v) {
    kfree(v->nodes); kfree(v->bmp); kfree(v->bmp_clus); kfree(v->upcase); kfree(v->fc); kfree(v->sec);
    kfree(v);
}

static int mount_at(blockdev_t* bd, uint32_t base, const char* where, char* err, int errcap) {
    uint8_t* b = (uint8_t*)kmalloc(512);
    if (!b) { kstrlcpy(err, "out of memory", (size_t)errcap); return -1; }
    if (!bd->present || bd->read(bd, base, 1, b) != 0 || memcmp(b + 3, "EXFAT   ", 8) != 0 || b[510] != 0x55 || b[511] != 0xAA) {
        kfree(b);
        return -2;
    }
    xvol_t* v = (xvol_t*)kzalloc(sizeof(xvol_t));
    if (!v) { kfree(b); kstrlcpy(err, "out of memory", (size_t)errcap); return -1; }
    v->bd = bd;
    v->base = base;
    uint32_t bps_shift = b[108], spc_shift = b[109];
    v->fat_lba = rd32(b + 80);
    v->fat_len = rd32(b + 84);
    v->heap_lba = rd32(b + 88);
    v->nclusters = rd32(b + 92);
    v->root = rd32(b + 96);
    int nfats = b[110];
    uint16_t flags = rd16(b + 106);
    uint64_t vol_len = rd64(b + 72);
    kfree(b);
    if (bps_shift < 9 || bps_shift > 12 || spc_shift > 25 - bps_shift || !v->nclusters || !valid_clus(v, v->root) ||
        (nfats != 1 && nfats != 2) || (uint64_t)v->fat_len * (1u << bps_shift) / 4 < v->nclusters + 2) {
        kfree(v);
        kstrlcpy(err, "the exFAT boot sector does not make sense", (size_t)errcap);
        return -1;
    }
    v->bps = 1u << bps_shift;
    v->ratio = v->bps / 512u;
    v->spc = 1u << spc_shift;
    v->clus_bytes = v->bps * v->spc;
    if (base + vol_len * v->ratio > (uint64_t)bd->sectors + 1) {
        kfree(v);
        kstrlcpy(err, "the volume is bigger than the drive", (size_t)errcap);
        return -1;
    }
    /* two FATs (TexFAT): which is active is the boot sector's - read it, change nothing */
    if (nfats == 2) {
        if (flags & 1) v->fat_lba += v->fat_len;
        v->readonly = 1;
    }
    v->hint = 2;
    v->sec = (uint8_t*)kmalloc(v->bps);
    v->fc = (uint8_t*)kmalloc((size_t)FATC * v->bps);
    int root = new_node(v);
    if (!v->sec || !v->fc || root != 0) { discard(v); kstrlcpy(err, "out of memory", (size_t)errcap); return -1; }
    v->nodes[0].used = 1;
    v->nodes[0].is_dir = 1;
    v->nodes[0].first = v->root;

    /* the root's special entries: the bitmap and the up-case table */
    uint32_t len;
    uint8_t* d = dir_load(v, 0, &len);
    if (!d) { discard(v); kstrlcpy(err, "cannot read the root folder", (size_t)errcap); return -1; }
    int have_bmp = 0;
    for (uint32_t p = 0; p + 32 <= len; p += 32) {
        const uint8_t* e = d + p;
        if (e[0] == T_END) break;
        if (e[0] == T_BITMAP && !(e[1] & 1) && !have_bmp)   /* (the first bitmap: TexFAT has two) */
            have_bmp = load_bitmap(v, rd32(e + 20), rd64(e + 24)) == 0 ? 1 : -1;
        else if (e[0] == T_UPCASE && !v->upcase) load_upcase(v, rd32(e + 20), rd64(e + 24));
    }
    kfree(d);
    if (have_bmp != 1) { discard(v); kstrlcpy(err, "no usable allocation bitmap", (size_t)errcap); return -1; }

    int mnt = fs_mount(where, &g_ops, v, 0);
    if (mnt < 0) {
        discard(v);
        ksnprintf(err, (size_t)errcap, "cannot mount at %s", where);
        return -1;
    }
    v->mnt = mnt;
    g_vols[mnt] = v;
    scan_dir(v, 0, fs_mount_root(mnt), 0);
    klog("exfat: %s (%s) mounted at %s - %u clusters of %u bytes%s%s\n", bd->name, v->label[0] ? v->label : "no label",
         where, v->nclusters, v->clus_bytes, v->readonly ? ", read-only (two FATs)" : "",
         v->skipped ? " (some entries did not fit)" : "");
    return mnt;
}

static int exfat_space(int mnt, uint64_t* total, uint64_t* free_bytes) {
    if (mnt <= 0 || mnt > FS_MAX_MOUNTS || !g_vols[mnt]) return 0;
    xvol_t* v = g_vols[mnt];
    *total = (uint64_t)v->nclusters * v->clus_bytes;
    *free_bytes = (uint64_t)v->nfree * v->clus_bytes;
    return 1;
}

static int exfat_describe(int mnt, char* out, int cap) {
    if (mnt <= 0 || mnt > FS_MAX_MOUNTS || !g_vols[mnt]) return 0;
    xvol_t* v = g_vols[mnt];
    uint32_t mib = (uint32_t)(((uint64_t)v->nclusters * v->clus_bytes) >> 20);
    ksnprintf(out, (size_t)cap, "%-12s exFAT  %s%s%s on %s (%s), %u MiB%s%s", fs_mount_point(mnt),
              v->label[0] ? "\"" : "", v->label[0] ? v->label : "(no label)", v->label[0] ? "\"" : "",
              v->bd->name, v->bd->model, mib, v->readonly ? ", read-only" : "",
              v->skipped ? " - partly shown (too many files or too big)" : "");
    return 1;
}

const volfs_t exfat_volfs = { "exFAT", "exfat", mount_at, exfat_describe, exfat_space };
