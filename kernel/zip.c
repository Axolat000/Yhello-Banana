/*
 * ZIP archives: list, extract, create (APPNOTE.TXT's format, without ZIP64
 * or encryption). An archive is read through fs_read - its central
 * directory, then one entry at a time - so it never has to fit in RAM as a
 * whole; each entry does. Entries are inflated by stb_image's inflater and
 * deflated by kernel/deflate.c.
 */
#include "zip.h"
#include "deflate.h"
#include "kheap.h"
#include "kstring.h"
#include "rtc.h"

/* (stb_image's zlib inflater, third_party/stb: raw deflate in, olen bytes out) */
int stbi_zlib_decode_noheader_buffer(char* obuffer, int olen, const char* ibuffer, int ilen);

#define SIG_LOCAL   0x04034b50u
#define SIG_CENTRAL 0x02014b50u
#define SIG_END     0x06054b50u
#define FLAG_ENCRYPTED 0x0001u
#define FLAG_UTF8      0x0800u
#define MAX_DEPTH   32

static uint16_t get16(const uint8_t* p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void put16(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void seterr(char* err, int cap, const char* msg) { if (err && cap > 0 && !err[0]) kstrlcpy(err, msg, (size_t)cap); }

int zip_is_zip_name(const char* name) {
    int n = (int)strlen(name);
    return n > 4 && strcasecmp(name + n - 4, ".zip") == 0;
}

/* DOS time <-> seconds since 1970 */
static uint32_t from_dos(uint16_t time, uint16_t date) {
    int y = 1980 + (date >> 9), mo = (date >> 5) & 15, d = date & 31;
    if (mo < 1 || mo > 12 || d < 1) return 0;
    return rtc_make_time(y, mo, d, time >> 11, (time >> 5) & 63, (time & 31) * 2);
}

static void to_dos(uint32_t t, uint16_t* time, uint16_t* date) {
    rtc_datetime_t dt;
    rtc_split_time(t, &dt);
    if (dt.year < 1980) { *time = 0; *date = (1 << 5) | 1; return; }       /* 1980-01-01 */
    *time = (uint16_t)(dt.hour << 11 | dt.minute << 5 | dt.second / 2);
    *date = (uint16_t)((dt.year - 1980) << 9 | dt.month << 5 | dt.day);
}

/* len bytes at off of file idx, all of them: 0 / -1 */
static int read_at(int idx, uint32_t off, void* buf, uint32_t len) {
    uint8_t* p = (uint8_t*)buf;
    while (len) {
        int n = fs_read(idx, off, p, len);
        if (n <= 0) return -1;
        p += n;
        off += (uint32_t)n;
        len -= (uint32_t)n;
    }
    return 0;
}

/* ── reading: the central directory ───────────────────────────────── */

typedef struct {
    int idx;                     /* the archive's file */
    uint8_t* cd;                 /* its central directory */
    uint32_t cd_len;
    int count;
} zr_t;

static int zr_open(zr_t* z, const char* zip, char* err, int cap) {
    z->cd = NULL;
    z->idx = fs_find_file(zip);
    if (z->idx < 0) { seterr(err, cap, fs_find_dir(zip) >= 0 ? "that is a folder" : "no such file"); return -1; }
    fs_file_t* f = fs_file_info(z->idx);
    uint32_t size = f ? f->size : 0;
    if (size < 22) { seterr(err, cap, "not a ZIP archive (too small)"); return -1; }
    /* the end record: in the last 22 bytes + up to 64 KiB of comment */
    uint32_t tail = size < 22 + 65535 ? size : 22 + 65535;
    uint8_t* t = (uint8_t*)kmalloc(tail);
    if (!t) { seterr(err, cap, "out of memory"); return -1; }
    if (read_at(z->idx, size - tail, t, tail) < 0) { kfree(t); seterr(err, cap, "could not read the archive"); return -1; }
    int at = -1;
    for (int i = (int)tail - 22; i >= 0; i--)
        if (get32(t + i) == SIG_END && (uint32_t)i + 22 + get16(t + i + 20) <= tail) { at = i; break; }
    if (at < 0) { kfree(t); seterr(err, cap, "not a ZIP archive (no end of central directory)"); return -1; }
    uint32_t entries = get16(t + at + 10), cd_len = get32(t + at + 12), cd_off = get32(t + at + 16);
    kfree(t);
    if (entries == 0xFFFF || cd_len == 0xFFFFFFFFu || cd_off == 0xFFFFFFFFu) { seterr(err, cap, "ZIP64 archives are not supported"); return -1; }
    if ((uint64_t)cd_off + cd_len > size) { seterr(err, cap, "damaged archive (central directory out of the file)"); return -1; }
    z->cd = (uint8_t*)kmalloc(cd_len ? cd_len : 1);
    if (!z->cd) { seterr(err, cap, "out of memory"); return -1; }
    if (cd_len && read_at(z->idx, cd_off, z->cd, cd_len) < 0) { kfree(z->cd); z->cd = NULL; seterr(err, cap, "could not read the archive"); return -1; }
    z->cd_len = cd_len;
    z->count = (int)entries;
    return 0;
}

/* the entry at *pos of the central directory into e (and its local header
 * offset): 1, 0 at the end, -1 if damaged */
static int zr_next(zr_t* z, uint32_t* pos, zip_entry_t* e, uint32_t* local_off) {
    if (*pos + 46 > z->cd_len) return 0;
    const uint8_t* p = z->cd + *pos;
    if (get32(p) != SIG_CENTRAL) return -1;
    uint16_t flags = get16(p + 8), nlen = get16(p + 28), xlen = get16(p + 30), clen = get16(p + 32);
    if (*pos + 46u + nlen + xlen + clen > z->cd_len) return -1;
    memset(e, 0, sizeof(*e));
    e->method = get16(p + 10);
    e->mtime = from_dos(get16(p + 12), get16(p + 14));
    e->crc = get32(p + 16);
    e->csize = get32(p + 20);
    e->size = get32(p + 24);
    e->encrypted = (flags & FLAG_ENCRYPTED) != 0;
    *local_off = get32(p + 42);
    uint32_t n = nlen < FS_PATH_LEN - 1 ? nlen : FS_PATH_LEN - 1;
    memcpy(e->name, p + 46, n);
    e->name[n] = 0;
    for (char* c = e->name; *c; c++) if (*c == '\\') *c = '/';      /* (archives made on Windows) */
    e->is_dir = n > 0 && e->name[n - 1] == '/';
    *pos += 46u + nlen + xlen + clen;
    return 1;
}

int zip_list(const char* zip, zip_cb_t cb, void* arg, char* err, int cap) {
    if (err && cap) err[0] = 0;
    zr_t z;
    if (zr_open(&z, zip, err, cap) < 0) return -1;
    zip_entry_t* e = (zip_entry_t*)kmalloc(sizeof(zip_entry_t));
    if (!e) { kfree(z.cd); seterr(err, cap, "out of memory"); return -1; }
    uint32_t pos = 0, off;
    int n = 0, r;
    while ((r = zr_next(&z, &pos, e, &off)) > 0) { if (cb) cb(e, NULL, arg); n++; }
    kfree(e);
    kfree(z.cd);
    if (r < 0) { seterr(err, cap, "damaged archive (central directory)"); return -1; }
    return n;
}

/* ── extracting ────────────────────────────────────────────────────── */

/* name made safe under dest: "dest/a/b" - 0, or -1 (absolute, "..", empty) */
static int safe_path(const char* dest, const char* name, char* out, int cap) {
    if (name[0] == '/' || (name[0] && name[1] == ':')) return -1;    /* "/etc/x", "C:/x" */
    int n = ksnprintf(out, (size_t)cap, "%s", dest);
    while (n > 1 && out[n - 1] == '/') out[--n] = 0;
    int parts = 0;
    for (const char* p = name; *p;) {
        const char* s = p;
        while (*p && *p != '/') p++;
        int len = (int)(p - s);
        if (*p) p++;
        if (len == 0 || (len == 1 && s[0] == '.')) continue;
        if (len == 2 && s[0] == '.' && s[1] == '.') return -1;
        if (len >= FS_NAME_LEN || n + 1 + len >= cap) return -1;
        out[n++] = '/';
        memcpy(out + n, s, (size_t)len);
        n += len;
        out[n] = 0;
        parts++;
    }
    return parts ? 0 : -1;
}

/* the folder holding path (made if needed): 0 / -1 */
static int make_parent(const char* path) {
    char d[FS_PATH_LEN];
    kstrlcpy(d, path, sizeof(d));
    char* s = strrchr(d, '/');
    if (!s || s == d) return 0;
    *s = 0;
    return fs_find_dir(d) >= 0 || fs_mkdir_p(d) >= 0 ? 0 : -1;
}

/* one entry's data, uncompressed and checked: a kmalloc'd buffer of e->size bytes, or NULL (err) */
static uint8_t* entry_data(zr_t* z, const zip_entry_t* e, uint32_t local_off, char* why, int cap) {
    uint8_t h[30];
    if (read_at(z->idx, local_off, h, 30) < 0 || get32(h) != SIG_LOCAL) { kstrlcpy(why, "damaged (no local header)", (size_t)cap); return NULL; }
    uint32_t data_off = local_off + 30u + get16(h + 26) + get16(h + 28);
    uint8_t* in = (uint8_t*)kmalloc(e->csize ? e->csize : 1);
    uint8_t* out = e->method == 0 ? in : (uint8_t*)kmalloc(e->size ? e->size : 1);
    if (!in || !out) { kfree(in); if (out != in) kfree(out); kstrlcpy(why, "too big for the memory", (size_t)cap); return NULL; }
    if (e->csize && read_at(z->idx, data_off, in, e->csize) < 0) { kstrlcpy(why, "could not be read", (size_t)cap); goto bad; }
    if (e->method == 0) {
        if (e->csize != e->size) { kstrlcpy(why, "damaged (sizes)", (size_t)cap); goto bad; }
    } else if (e->size) {
        int got = stbi_zlib_decode_noheader_buffer((char*)out, (int)e->size, (const char*)in, (int)e->csize);
        if (got != (int)e->size) { kstrlcpy(why, "damaged (bad compressed data)", (size_t)cap); goto bad; }
    }
    if (out != in) kfree(in);
    if (crc32_update(0, out, e->size) != e->crc) { kstrlcpy(why, "damaged (CRC does not match)", (size_t)cap); kfree(out); return NULL; }
    return out;
bad:
    if (out != in) kfree(out);
    kfree(in);
    return NULL;
}

int zip_extract(const char* zip, const char* dest, int flags, zip_cb_t cb, void* arg,
                zip_stats_t* st, char* err, int cap) {
    zip_stats_t dummy;
    if (!st) st = &dummy;
    memset(st, 0, sizeof(*st));
    if (err && cap) err[0] = 0;
    zr_t z;
    if (zr_open(&z, zip, err, cap) < 0) return -1;
    if (fs_find_dir(dest) < 0 && fs_mkdir_p(dest) < 0) { kfree(z.cd); seterr(err, cap, "cannot make the destination folder"); return -1; }
    zip_entry_t* e = (zip_entry_t*)kmalloc(sizeof(zip_entry_t));
    char* path = (char*)kmalloc(FS_PATH_LEN);
    if (!e || !path) { kfree(e); kfree(path); kfree(z.cd); seterr(err, cap, "out of memory"); return -1; }
    uint32_t pos = 0, off;
    int r;
    char why[64], msg[FS_PATH_LEN + 80];
    while ((r = zr_next(&z, &pos, e, &off)) > 0) {
        const char* note = NULL;
        why[0] = 0;
        if (safe_path(dest, e->name, path, FS_PATH_LEN) < 0) { st->skipped++; note = "skipped: unsafe name"; }
        else if (e->encrypted) { st->skipped++; note = "skipped: encrypted"; }
        else if (e->is_dir) {
            if (fs_find_dir(path) < 0 && fs_mkdir_p(path) < 0) { st->failed++; note = "failed: cannot make the folder"; }
            else { st->dirs++; int d = fs_find_dir(path); if (d >= 0 && e->mtime) fs_set_times(1, d, e->mtime, e->mtime); }
        } else if (e->method != 0 && e->method != 8) { st->skipped++; note = "skipped: unsupported compression"; }
        else if (e->size == 0xFFFFFFFFu || e->csize == 0xFFFFFFFFu) { st->skipped++; note = "skipped: ZIP64 entry"; }
        else if (fs_find_dir(path) >= 0 || (fs_find_file(path) >= 0 && !(flags & ZIP_OVERWRITE))) { st->skipped++; note = "skipped: already there"; }
        else {
            uint8_t* data = entry_data(&z, e, off, why, sizeof(why));
            int fi = -1;
            if (data) {
                if (make_parent(path) == 0) fi = fs_write_path(path, data, e->size);
                if (fi < 0) kstrlcpy(why, "could not be written", sizeof(why));
                kfree(data);
            }
            if (fi < 0) { st->failed++; note = why; }
            else { st->files++; st->bytes += e->size; if (e->mtime) fs_set_times(0, fi, e->mtime, e->mtime); }
        }
        if (note) { ksnprintf(msg, sizeof(msg), "%s: %s", e->name, note); seterr(err, cap, msg); }
        if (cb) cb(e, note, arg);
    }
    kfree(e);
    kfree(path);
    kfree(z.cd);
    if (r < 0) { st->failed++; seterr(err, cap, "damaged archive (central directory)"); }
    return 0;
}

/* ── creating ──────────────────────────────────────────────────────── */

typedef struct {
    int zidx;                    /* the archive being written */
    uint8_t* cd;                 /* its central directory, written at the end */
    uint32_t cd_len, cd_cap;
    uint64_t off;                /* bytes written so far */
    int count, flags;
    zip_cb_t cb;
    void* arg;
    zip_stats_t* st;
    char* err;
    int cap;
} zw_t;

static int cd_add(zw_t* z, const uint8_t* rec, uint32_t len) {
    if (z->cd_len + len > z->cd_cap) {
        uint32_t c = z->cd_cap ? z->cd_cap * 2 : 4096;
        while (c < z->cd_len + len) c *= 2;
        uint8_t* n = (uint8_t*)krealloc(z->cd, c);
        if (!n) return -1;
        z->cd = n;
        z->cd_cap = c;
    }
    memcpy(z->cd + z->cd_len, rec, len);
    z->cd_len += len;
    return 0;
}

static int out(zw_t* z, const void* data, uint32_t len) {
    if (!len) return 0;
    if (z->off + len > 0xFFFFFFF0ull) { seterr(z->err, z->cap, "the archive would be over 4 GB (ZIP64 is not supported)"); return -1; }
    if (fs_append(z->zidx, data, len) < 0) { seterr(z->err, z->cap, "could not write the archive (disk full?)"); return -1; }
    z->off += len;
    return 0;
}

/* one entry: name (with "/" at the end for a folder), data (NULL for a folder) */
static int add_entry(zw_t* z, const char* name, const uint8_t* data, uint32_t size, uint32_t mtime, uint32_t mode, int is_dir) {
    if (z->count >= 65535) { seterr(z->err, z->cap, "more than 65535 entries (ZIP64 is not supported)"); return -1; }
    uint32_t nlen = (uint32_t)strlen(name);
    uint32_t crc = data ? crc32_update(0, data, size) : 0, csize = size;
    uint16_t method = 0, t, d;
    uint8_t* packed = NULL;
    if (data && size > 0 && !(z->flags & ZIP_STORE)) {
        uint32_t plen = 0;
        packed = deflate_compress(data, size, &plen);
        if (packed && plen < size) { method = 8; csize = plen; }
        else { kfree(packed); packed = NULL; }
    }
    to_dos(mtime, &t, &d);
    uint8_t h[46];
    memset(h, 0, sizeof(h));
    put32(h, SIG_LOCAL); put16(h + 4, 20); put16(h + 6, FLAG_UTF8); put16(h + 8, method);
    put16(h + 10, t); put16(h + 12, d); put32(h + 14, crc); put32(h + 18, csize); put32(h + 22, size);
    put16(h + 26, nlen); put16(h + 28, 0);
    uint32_t local_off = (uint32_t)z->off;
    int rc = out(z, h, 30) || out(z, name, nlen) || out(z, packed ? packed : data, data ? csize : 0) ? -1 : 0;
    kfree(packed);
    if (rc) return -1;
    memset(h, 0, sizeof(h));
    put32(h, SIG_CENTRAL); put16(h + 4, 3 << 8 | 20); put16(h + 6, 20); put16(h + 8, FLAG_UTF8); put16(h + 10, method);
    put16(h + 12, t); put16(h + 14, d); put32(h + 16, crc); put32(h + 20, csize); put32(h + 24, size);
    put16(h + 28, nlen);
    put32(h + 38, (mode & 0xFFFFu) << 16 | (is_dir ? 0x10u : 0));     /* Unix mode, and MS-DOS's folder bit */
    put32(h + 42, local_off);
    if (cd_add(z, h, 46) < 0 || cd_add(z, (const uint8_t*)name, nlen) < 0) { seterr(z->err, z->cap, "out of memory"); return -1; }
    z->count++;
    if (is_dir) z->st->dirs++;
    else { z->st->files++; z->st->bytes += size; }
    if (z->cb) {
        zip_entry_t* e = (zip_entry_t*)kmalloc(sizeof(zip_entry_t));
        if (e) {
            memset(e, 0, sizeof(*e));
            kstrlcpy(e->name, name, sizeof(e->name));
            e->size = size; e->csize = csize; e->mtime = mtime; e->crc = crc; e->is_dir = is_dir; e->method = method;
            z->cb(e, NULL, z->arg);
            kfree(e);
        }
    }
    return 0;
}

static int add_file(zw_t* z, const char* path, const char* name) {
    int fi = fs_find_file(path);
    if (fi < 0) { seterr(z->err, z->cap, "a file went away"); return -1; }
    if (fi == z->zidx) return 0;                   /* the archive itself */
    fs_stat_t s;
    if (fs_stat(path, 1, &s) < 0) memset(&s, 0, sizeof(s));
    uint32_t size = fs_file_info(fi)->size;
    uint8_t* data = (uint8_t*)kmalloc(size ? size : 1);
    if (!data) { char m[FS_PATH_LEN + 40]; ksnprintf(m, sizeof(m), "%s: too big for the memory", name); seterr(z->err, z->cap, m); return -1; }
    if (size && read_at(fi, 0, data, size) < 0) {
        kfree(data);
        char m[FS_PATH_LEN + 40];
        ksnprintf(m, sizeof(m), "%s: could not be read", name);
        seterr(z->err, z->cap, m);
        return -1;
    }
    int rc = add_entry(z, name, data, size, s.mtime ? s.mtime : rtc_now(), s.mode ? s.mode : (FS_S_IFREG | 0644u), 0);
    kfree(data);
    return rc;
}

static int add_dir(zw_t* z, const char* path, const char* name, int depth) {
    if (depth > MAX_DEPTH) { seterr(z->err, z->cap, "folders nested too deep"); return -1; }
    char* ename = (char*)kmalloc(FS_PATH_LEN);
    char* sub = (char*)kmalloc(FS_PATH_LEN);
    char* subname = (char*)kmalloc(FS_PATH_LEN);
    int* idx = (int*)kmalloc(FS_LIST_MAX * sizeof(int));
    int rc = 0;
    if (!ename || !sub || !subname || !idx) { seterr(z->err, z->cap, "out of memory"); rc = -1; goto done; }
    fs_stat_t s;
    if (fs_stat(path, 1, &s) < 0) memset(&s, 0, sizeof(s));
    ksnprintf(ename, FS_PATH_LEN, "%s/", name);
    if (add_entry(z, ename, NULL, 0, s.mtime ? s.mtime : rtc_now(), s.mode ? s.mode : (FS_S_IFDIR | 0755u), 1) < 0) { rc = -1; goto done; }

    int nf = fs_list_files(path, idx, FS_LIST_MAX);
    /* (names first: adding to the archive may change the file table) */
    char (*names)[FS_NAME_LEN] = nf > 0 ? kmalloc((size_t)nf * FS_NAME_LEN) : NULL;
    if (nf > 0 && !names) { seterr(z->err, z->cap, "out of memory"); rc = -1; goto done; }
    for (int i = 0; i < nf; i++) kstrlcpy(names[i], fs_file_info(idx[i])->name, FS_NAME_LEN);
    for (int i = 0; i < nf && rc == 0; i++) {
        ksnprintf(sub, FS_PATH_LEN, "%s/%s", path, names[i]);
        ksnprintf(subname, FS_PATH_LEN, "%s/%s", name, names[i]);
        rc = add_file(z, sub, subname);
    }
    kfree(names);
    if (rc) goto done;
    int nd = fs_list_dirs(path, idx, FS_LIST_MAX);
    names = nd > 0 ? kmalloc((size_t)nd * FS_NAME_LEN) : NULL;
    if (nd > 0 && !names) { seterr(z->err, z->cap, "out of memory"); rc = -1; goto done; }
    for (int i = 0; i < nd; i++) kstrlcpy(names[i], fs_get_dir(idx[i])->name, FS_NAME_LEN);
    kfree(idx);
    idx = NULL;
    for (int i = 0; i < nd && rc == 0; i++) {
        ksnprintf(sub, FS_PATH_LEN, "%s/%s", path, names[i]);
        ksnprintf(subname, FS_PATH_LEN, "%s/%s", name, names[i]);
        rc = add_dir(z, sub, subname, depth + 1);
    }
    kfree(names);
done:
    kfree(ename);
    kfree(sub);
    kfree(subname);
    kfree(idx);
    return rc;
}

int zip_create(const char* zip, const char* const* paths, int n, int flags, zip_cb_t cb, void* arg,
               zip_stats_t* st, char* err, int cap) {
    zip_stats_t dummy;
    if (!st) st = &dummy;
    memset(st, 0, sizeof(*st));
    if (err && cap) err[0] = 0;
    if (fs_find_dir(zip) >= 0) { seterr(err, cap, "a folder has that name"); return -1; }
    zw_t z = { 0 };
    z.flags = flags; z.cb = cb; z.arg = arg; z.st = st; z.err = err; z.cap = cap;
    for (int i = 0; i < n; i++)
        if (fs_find_file(paths[i]) < 0 && fs_find_dir(paths[i]) < 0) {
            char m[FS_PATH_LEN + 40];
            ksnprintf(m, sizeof(m), "%s: no such file or folder", paths[i]);
            seterr(err, cap, m);
            return -1;
        }
    z.zidx = fs_write_path(zip, "", 0);
    if (z.zidx < 0) { seterr(err, cap, "cannot create the archive"); return -1; }
    int rc = 0;
    for (int i = 0; i < n && rc == 0; i++) {
        /* each under its last name: "/home/banana/docs/" -> "docs" */
        char p[FS_PATH_LEN];
        kstrlcpy(p, paths[i], sizeof(p));
        int l = (int)strlen(p);
        while (l > 1 && p[l - 1] == '/') p[--l] = 0;
        const char* base = strrchr(p, '/');
        base = base ? base + 1 : p;
        if (!base[0] || !strcmp(base, ".") || !strcmp(base, "..")) {
            char full[FS_PATH_LEN];
            if (fs_find_dir(p) >= 0) {
                /* "." or "..": the folder's own name */
                int d = fs_find_dir(p);
                fs_dir_path(d, full, sizeof(full));
                const char* b = strrchr(full, '/');
                base = b && b[1] ? b + 1 : "root";
                char nm[FS_NAME_LEN];
                kstrlcpy(nm, base, sizeof(nm));
                rc = add_dir(&z, p, nm, 0);
                continue;
            }
        }
        if (fs_find_dir(p) >= 0) rc = add_dir(&z, p, base, 0);
        else rc = add_file(&z, p, base);
    }
    if (rc == 0) {
        uint8_t e[22];
        memset(e, 0, sizeof(e));
        put32(e, SIG_END);
        put16(e + 8, (uint32_t)z.count); put16(e + 10, (uint32_t)z.count);
        put32(e + 12, z.cd_len); put32(e + 16, (uint32_t)z.off);
        if (out(&z, z.cd, z.cd_len) < 0 || out(&z, e, 22) < 0) rc = -1;
    }
    kfree(z.cd);
    if (rc) { fs_delete(zip, 0); return -1; }
    st->zbytes = (uint32_t)z.off;
    return 0;
}
