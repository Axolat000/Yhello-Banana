#include "backup.h"
#include "kstring.h"
#include "kheap.h"
#include "rtc.h"
#include "deflate.h"
#include "blockdev.h"
#include "volume.h"
#include "../crypto/sha256.h"

/* Forward declare decompression helper */
int stbi_zlib_decode_noheader_buffer(char *obuffer, int olen, const char *ibuffer, int ilen);



static inline void put32_le(uint8_t* buf, uint32_t val) {
    buf[0] = (uint8_t)(val & 0xFF);
    buf[1] = (uint8_t)((val >> 8) & 0xFF);
    buf[2] = (uint8_t)((val >> 16) & 0xFF);
    buf[3] = (uint8_t)((val >> 24) & 0xFF);
}

static inline void put64_le(uint8_t* buf, uint64_t val) {
    put32_le(buf, (uint32_t)(val & 0xFFFFFFFFULL));
    put32_le(buf + 4, (uint32_t)((val >> 32) & 0xFFFFFFFFULL));
}

static void set_err(char* err, int cap, const char* msg) {
    if (err && cap > 0) kstrlcpy(err, msg, (size_t)cap);
}

static uint32_t calc_crc32(uint32_t init, const void* data, uint32_t len) {
    return crc32_update(init, data, len);
}

typedef struct {
    int idx;
    uint32_t pos;
} file_writer_t;

static int fw_write(file_writer_t* fw, const void* data, uint32_t len) {
    if (!fw || fw->idx < 0) return -1;
    if (len == 0) return 0;
    if (fs_append(fw->idx, data, len) != 0) return -1;
    fw->pos += len;
    return 0;
}

static void sanitize_rel_path(const char* in, char* out, int outlen) {
    while (*in == '/') in++;
    int pos = 0;
    for (int i = 0; in[i] && pos < outlen - 1; i++) {
        if (in[i] == '/' && pos > 0 && out[pos - 1] == '/') continue;
        out[pos++] = in[i];
    }
    while (pos > 0 && out[pos - 1] == '/') pos--;
    out[pos] = '\0';
}

typedef struct {
    backup_cb_t cb;
    void* ctx;
    backup_stats_t* st;
    uint32_t flags;
    file_writer_t* fw;
    uint32_t archive_crc;
    uint32_t entry_count;
    char* err;
    int errcap;
} backup_ctx_t;

static int add_one_entry(backup_ctx_t* bc, const char* abs_path, const char* rel_path) {
    fs_stat_t st;
    if (fs_stat(abs_path, 0, &st) < 0) {
        set_err(bc->err, bc->errcap, "failed to stat source path");
        return -1;
    }

    backup_entry_t e;
    memset(&e, 0, sizeof(e));
    kstrlcpy(e.path, rel_path, sizeof(e.path));
    e.mode = st.mode;
    e.uid = st.uid;
    e.gid = st.gid;
    e.mtime = st.mtime;
    e.ctime = st.ctime;

    uint8_t* payload = NULL;
    uint32_t payload_size = 0;
    uint32_t comp_size = 0;
    int is_deflated = 0;

    if (st.is_link) {
        e.type = BBA_ENTRY_SYMLINK;
        int llen = fs_readlink(abs_path, e.link_target, sizeof(e.link_target));
        if (llen < 0) {
            set_err(bc->err, bc->errcap, "failed to read symlink target");
            return -1;
        }
        e.size = 0;
        e.csize = 0;
        bc->st->symlink_count++;
    } else if (st.is_dir) {
        e.type = BBA_ENTRY_DIR;
        e.size = 0;
        e.csize = 0;
        bc->st->dir_count++;
    } else {
        e.type = BBA_ENTRY_FILE;
        e.size = st.size;
        bc->st->file_count++;
        bc->st->total_uncompressed_bytes += st.size;

        int fi = fs_find_file(abs_path);
        fs_file_t* fl = (fi >= 0) ? fs_get_file(fi) : NULL;
        const uint8_t* content = fl ? (const uint8_t*)fl->content : (const uint8_t*)"";
        uint32_t content_len = fl ? fl->size : 0;

        sha256(content, content_len, e.sha256);

        if ((bc->flags & BBA_FLAG_DEFLATE) && content_len > 64) {
            uint32_t def_len = 0;
            uint8_t* comp = deflate_compress(content, content_len, &def_len);
            if (comp && def_len < content_len) {
                payload = comp;
                payload_size = def_len;
                comp_size = def_len;
                is_deflated = 1;
            } else {
                if (comp) kfree(comp);
                payload = NULL;
                payload_size = content_len;
                comp_size = content_len;
            }
        } else {
            payload_size = content_len;
            comp_size = content_len;
        }

        e.flags = is_deflated ? BBA_FLAG_DEFLATE : 0;
        e.csize = comp_size;

        /* If not compressed buffer, point to content */
        if (!payload && content_len > 0) {
            payload = (uint8_t*)content;
        }
    }

    uint16_t plen = (uint16_t)strlen(e.path);
    uint16_t llen = (uint16_t)strlen(e.link_target);

    uint8_t record_hdr[80];
    uint32_t hpos = 0;
    record_hdr[hpos++] = (uint8_t)(e.type & 0xFF);
    record_hdr[hpos++] = (uint8_t)((e.type >> 8) & 0xFF);
    record_hdr[hpos++] = (uint8_t)(e.flags & 0xFF);
    record_hdr[hpos++] = (uint8_t)((e.flags >> 8) & 0xFF);



    put32_le(record_hdr + hpos, e.mode); hpos += 4;
    put32_le(record_hdr + hpos, e.uid);  hpos += 4;
    put32_le(record_hdr + hpos, e.gid);  hpos += 4;
    put32_le(record_hdr + hpos, e.mtime); hpos += 4;
    put32_le(record_hdr + hpos, e.ctime); hpos += 4;
    put64_le(record_hdr + hpos, e.size);  hpos += 8;
    put64_le(record_hdr + hpos, e.csize); hpos += 8;

    memcpy(record_hdr + hpos, e.sha256, 32); hpos += 32;

    record_hdr[hpos++] = (uint8_t)(plen & 0xFF);
    record_hdr[hpos++] = (uint8_t)((plen >> 8) & 0xFF);
    record_hdr[hpos++] = (uint8_t)(llen & 0xFF);
    record_hdr[hpos++] = (uint8_t)((llen >> 8) & 0xFF);

    if (fw_write(bc->fw, record_hdr, hpos) != 0) {
        set_err(bc->err, bc->errcap, "failed writing entry header");
        if (is_deflated && payload) kfree(payload);
        return -1;
    }
    bc->archive_crc = calc_crc32(bc->archive_crc, record_hdr, hpos);

    if (plen > 0) {
        if (fw_write(bc->fw, e.path, plen) != 0) {
            set_err(bc->err, bc->errcap, "failed writing entry path");
            if (is_deflated && payload) kfree(payload);
            return -1;
        }
        bc->archive_crc = calc_crc32(bc->archive_crc, e.path, plen);
    }

    if (llen > 0) {
        if (fw_write(bc->fw, e.link_target, llen) != 0) {
            set_err(bc->err, bc->errcap, "failed writing symlink target");
            if (is_deflated && payload) kfree(payload);
            return -1;
        }
        bc->archive_crc = calc_crc32(bc->archive_crc, e.link_target, llen);
    }

    if (payload_size > 0 && payload) {
        if (fw_write(bc->fw, payload, payload_size) != 0) {
            set_err(bc->err, bc->errcap, "failed writing entry data");
            if (is_deflated) kfree(payload);
            return -1;
        }
        bc->archive_crc = calc_crc32(bc->archive_crc, payload, payload_size);
    }

    if (is_deflated && payload) {
        kfree(payload);
    }

    bc->entry_count++;
    if (bc->cb) bc->cb(&e, bc->ctx);
    return 0;
}

static int add_tree_recursive(backup_ctx_t* bc, const char* abs_dir, const char* rel_prefix) {
    if (add_one_entry(bc, abs_dir, rel_prefix) != 0) return -1;

    int* idx = (int*)kmalloc(FS_LIST_MAX * sizeof(int));
    if (!idx) {
        set_err(bc->err, bc->errcap, "out of memory listing directories");
        return -1;
    }

    int nf = fs_list_files(abs_dir, idx, FS_LIST_MAX);
    char (*fnames)[FS_NAME_LEN] = (nf > 0) ? (char(*)[FS_NAME_LEN])kmalloc((size_t)nf * FS_NAME_LEN) : NULL;
    for (int i = 0; i < nf; i++) {
        fs_file_t* fi = fs_file_info(idx[i]);
        kstrlcpy(fnames[i], fi ? fi->name : "", FS_NAME_LEN);
    }
    for (int i = 0; i < nf; i++) {
        char sub_abs[FS_PATH_LEN], sub_rel[FS_PATH_LEN];
        ksnprintf(sub_abs, sizeof(sub_abs), "%s/%s", abs_dir, fnames[i]);
        ksnprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel_prefix, fnames[i]);
        if (add_one_entry(bc, sub_abs, sub_rel) != 0) {
            kfree(fnames);
            kfree(idx);
            return -1;
        }
    }
    if (fnames) kfree(fnames);

    int nd = fs_list_dirs(abs_dir, idx, FS_LIST_MAX);
    char (*dnames)[FS_NAME_LEN] = (nd > 0) ? (char(*)[FS_NAME_LEN])kmalloc((size_t)nd * FS_NAME_LEN) : NULL;
    for (int i = 0; i < nd; i++) {
        const fs_dir_t* di = fs_get_dir(idx[i]);
        kstrlcpy(dnames[i], di ? di->name : "", FS_NAME_LEN);
    }
    for (int i = 0; i < nd; i++) {
        char sub_abs[FS_PATH_LEN], sub_rel[FS_PATH_LEN];
        ksnprintf(sub_abs, sizeof(sub_abs), "%s/%s", abs_dir, dnames[i]);
        ksnprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel_prefix, dnames[i]);
        if (add_tree_recursive(bc, sub_abs, sub_rel) != 0) {
            kfree(dnames);
            kfree(idx);
            return -1;
        }
    }
    if (dnames) kfree(dnames);
    kfree(idx);
    return 0;
}

int backup_create(const char* archive_path, const char* const* sources, int num_sources,
                  uint32_t flags, backup_cb_t cb, void* ctx, backup_stats_t* st,
                  char* err, int errcap) {
    if (!archive_path || !sources || num_sources <= 0) {
        set_err(err, errcap, "invalid arguments to backup_create");
        return -1;
    }

    backup_stats_t local_st;
    memset(&local_st, 0, sizeof(local_st));
    if (!st) st = &local_st;
    else memset(st, 0, sizeof(*st));

    int fi = fs_write_path(archive_path, "", 0);
    if (fi < 0) {
        set_err(err, errcap, "failed to create archive file");
        return -1;
    }

    file_writer_t fw;
    fw.idx = fi;
    fw.pos = 0;

    backup_ctx_t bc;
    bc.cb = cb;
    bc.ctx = ctx;
    bc.st = st;
    bc.flags = flags;
    bc.fw = &fw;
    bc.archive_crc = 0;
    bc.entry_count = 0;
    bc.err = err;
    bc.errcap = errcap;

    /* Write 36-byte placeholder header:
     * 0..3: magic (4)
     * 4..7: version (4)
     * 8..11: flags (4)
     * 12..15: entry_count (4)
     * 16..23: created_time (8)
     * 24..31: total_bytes (8)
     * 32..35: header_crc32 (4)
     */
    uint8_t hdr[BBA_HDR_LEN];
    memset(hdr, 0, sizeof(hdr));
    memcpy(hdr, BBA_MAGIC, 4);
    put32_le(hdr + 4, BBA_VERSION_1);
    put32_le(hdr + 8, flags);
    put64_le(hdr + 16, (uint64_t)rtc_now());
    if (fw_write(&fw, hdr, BBA_HDR_LEN) != 0) {
        set_err(err, errcap, "failed writing initial header");
        fs_delete(archive_path, 0);
        return -1;
    }

    for (int s = 0; s < num_sources; s++) {
        const char* src = sources[s];
        fs_stat_t sst;
        if (fs_stat(src, 0, &sst) < 0) {
            set_err(err, errcap, "source path does not exist");
            fs_delete(archive_path, 0);
            return -1;
        }

        char base_name[FS_NAME_LEN];
        const char* slash = strrchr(src, '/');
        if (slash && slash[1]) kstrlcpy(base_name, slash + 1, sizeof(base_name));
        else if (slash && !slash[1]) kstrlcpy(base_name, "root", sizeof(base_name));
        else kstrlcpy(base_name, src, sizeof(base_name));

        char rel[FS_PATH_LEN];
        sanitize_rel_path(base_name, rel, sizeof(rel));

        if (sst.is_dir) {
            if (add_tree_recursive(&bc, src, rel) != 0) {
                fs_delete(archive_path, 0);
                return -1;
            }
        } else {
            if (add_one_entry(&bc, src, rel) != 0) {
                fs_delete(archive_path, 0);
                return -1;
            }
        }
    }

    /* Finalize header */
    put32_le(hdr + 12, bc.entry_count);
    put64_le(hdr + 24, st->total_uncompressed_bytes);
    uint32_t hcrc = calc_crc32(0, hdr, 32);
    put32_le(hdr + 32, hcrc);

    /* Write trailer: EBBA magic (4), archive_size (8), archive_crc32 (4) */
    uint8_t trailer[16];
    put32_le(trailer, BBA_TRAILER_MAGIC);
    uint64_t total_archive_len = (uint64_t)fw.pos + 16;
    put64_le(trailer + 4, total_archive_len);
    put32_le(trailer + 12, bc.archive_crc);

    if (fw_write(&fw, trailer, 16) != 0) {
        set_err(err, errcap, "failed writing archive trailer");
        fs_delete(archive_path, 0);
        return -1;
    }

    /* Seek back and write actual header with entry count, total bytes, header crc */
    fs_file_t* fl = fs_get_file(fi);
    if (fl && fl->content && fl->size >= BBA_HDR_LEN) {
        memcpy(fl->content, hdr, BBA_HDR_LEN);
    } else {
        set_err(err, errcap, "failed finalizing archive header");
        fs_delete(archive_path, 0);
        return -1;
    }

    st->archive_bytes = total_archive_len;
    return 0;
}

/* Binary layout on disk / archive:
 * Header:
 *   char     magic[4];       // "BBA1"
 *   uint32_t version;        // 1
 *   uint32_t flags;          // 0 = uncompressed, 1 = deflated entries
 *   uint32_t entry_count;    // Number of records
 *   uint64_t created_time;   // Unix timestamp (seconds)
 *   uint64_t total_bytes;    // Total uncompressed data bytes
 *   uint32_t header_crc32;   // CRC32 of header fields preceding header_crc32
 * Total header size = 36 bytes.
 */

/* Header offsets:
 * 0: magic (4)
 * 4: version (4)
 * 8: flags (4)
 * 12: entry_count (4)
 * 16: created_time (8)
 * 24: total_bytes (8)
 * 32: header_crc32 (4)
 */



static inline uint16_t get16_le(const uint8_t* buf) {
    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

static inline uint32_t get32_le(const uint8_t* buf) {
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static inline uint64_t get64_le(const uint8_t* buf) {
    return (uint64_t)get32_le(buf) | ((uint64_t)get32_le(buf + 4) << 32);
}

int backup_verify(const char* archive_path, backup_cb_t cb, void* ctx, backup_stats_t* st,
                  char* err, int errcap) {
    if (!archive_path) {
        set_err(err, errcap, "archive path is NULL");
        return -1;
    }

    int fi = fs_find_file(archive_path);
    if (fi < 0) {
        set_err(err, errcap, "archive file not found");
        return -1;
    }

    fs_file_t* fl = fs_get_file(fi);
    if (!fl || !fl->content || fl->size < 48) {
        set_err(err, errcap, "archive corrupted or too small (< 48 bytes)");
        return -1;
    }

    const uint8_t* buf = (const uint8_t*)fl->content;
    uint32_t len = fl->size;

    if (memcmp(buf, BBA_MAGIC, 4) != 0) {
        set_err(err, errcap, "invalid backup archive magic (expected BBA1)");
        return -1;
    }

    uint32_t version = get32_le(buf + 4);
    if (version != BBA_VERSION_1) {
        set_err(err, errcap, "unsupported backup version");
        return -1;
    }

    uint32_t expected_hcrc = get32_le(buf + 32);
    uint32_t act_hcrc = calc_crc32(0, buf, 32);
    if (expected_hcrc != act_hcrc) {
        set_err(err, errcap, "corrupted header CRC32 mismatch");
        return -1;
    }

    uint32_t entry_count = get32_le(buf + 12);
    uint64_t total_bytes = get64_le(buf + 24);

    /* Verify trailer */
    const uint8_t* tr = buf + len - 16;
    if (get32_le(tr) != BBA_TRAILER_MAGIC) {
        set_err(err, errcap, "missing or invalid EBBA trailer (truncated archive)");
        return -1;
    }
    uint64_t tr_size = get64_le(tr + 4);
    if (tr_size != (uint64_t)len) {
        set_err(err, errcap, "archive size mismatch in trailer");
        return -1;
    }
    uint32_t expected_arc_crc = get32_le(tr + 12);

    /* Verify stream CRC */
    uint32_t act_arc_crc = calc_crc32(0, buf + BBA_HDR_LEN, len - (BBA_HDR_LEN + 16));
    if (expected_arc_crc != act_arc_crc) {
        set_err(err, errcap, "archive data payload CRC32 mismatch");
        return -1;
    }

    backup_stats_t local_st;
    memset(&local_st, 0, sizeof(local_st));
    if (!st) st = &local_st;
    else memset(st, 0, sizeof(*st));

    uint32_t off = BBA_HDR_LEN;
    for (uint32_t i = 0; i < entry_count; i++) {
        if (off + 72 > len - 16) {
            set_err(err, errcap, "unexpected EOF reading entry record");
            return -1;
        }

        backup_entry_t e;
        memset(&e, 0, sizeof(e));
        e.type = get16_le(buf + off); off += 2;
        e.flags = get16_le(buf + off); off += 2;
        e.mode = get32_le(buf + off); off += 4;
        e.uid = get32_le(buf + off); off += 4;
        e.gid = get32_le(buf + off); off += 4;
        e.mtime = get32_le(buf + off); off += 4;
        e.ctime = get32_le(buf + off); off += 4;
        e.size = get64_le(buf + off); off += 8;
        e.csize = get64_le(buf + off); off += 8;
        memcpy(e.sha256, buf + off, 32); off += 32;

        uint16_t plen = get16_le(buf + off); off += 2;
        uint16_t llen = get16_le(buf + off); off += 2;

        if (off + plen + llen + e.csize > len - 16) {
            set_err(err, errcap, "entry payload overflows archive bounds");
            return -1;
        }

        if (plen >= sizeof(e.path) || llen >= sizeof(e.link_target)) {
            set_err(err, errcap, "entry path exceeds maximum supported length");
            return -1;
        }

        memcpy(e.path, buf + off, plen);
        e.path[plen] = '\0';
        off += plen;

        memcpy(e.link_target, buf + off, llen);
        e.link_target[llen] = '\0';
        off += llen;

        const uint8_t* payload = buf + off;
        off += (uint32_t)e.csize;

        if (e.type == BBA_ENTRY_FILE) {
            st->file_count++;
            st->total_uncompressed_bytes += e.size;

            /* Verify hash of payload */
            uint8_t act_sha[32];
            if (e.flags & BBA_FLAG_DEFLATE) {
                char* decomp = (char*)kmalloc((size_t)e.size + 1);
                if (!decomp && e.size > 0) {
                    set_err(err, errcap, "out of memory verifying deflated payload");
                    return -1;
                }
                int got = stbi_zlib_decode_noheader_buffer(decomp, (int)e.size, (const char*)payload, (int)e.csize);
                if (got < 0 || (uint64_t)got != e.size) {
                    if (decomp) kfree(decomp);
                    set_err(err, errcap, "corrupted deflated data payload");
                    return -1;
                }
                sha256(decomp, (uint32_t)e.size, act_sha);
                if (decomp) kfree(decomp);
            } else {
                sha256(payload, (uint32_t)e.size, act_sha);
            }

            if (memcmp(e.sha256, act_sha, 32) != 0) {
                set_err(err, errcap, "file SHA-256 integrity checksum mismatch");
                return -1;
            }
        } else if (e.type == BBA_ENTRY_DIR) {
            st->dir_count++;
        } else if (e.type == BBA_ENTRY_SYMLINK) {
            st->symlink_count++;
        }

        if (cb) cb(&e, ctx);
    }

    if (st->total_uncompressed_bytes != total_bytes) {
        char mmsg[128];
        ksnprintf(mmsg, sizeof(mmsg), "mismatch (hdr_hi=%u hdr_lo=%u vs st_hi=%u st_lo=%u)",
                  (uint32_t)(total_bytes >> 32), (uint32_t)total_bytes,
                  (uint32_t)(st->total_uncompressed_bytes >> 32), (uint32_t)st->total_uncompressed_bytes);
        set_err(err, errcap, mmsg);
        return -1;
    }

    st->archive_bytes = (uint64_t)len;
    return 0;
}

int backup_find_external_volume(char* dest_vol_path, int dest_cap, uint64_t required_bytes) {
    if (!dest_vol_path || dest_cap <= 0) return -1;
    dest_vol_path[0] = '\0';

    /* Scan /mnt for mounted external volumes */
    int dirs[16];
    int nd = fs_list_dirs("/mnt", dirs, 16);
    if (nd <= 0) return -1;

    for (int i = 0; i < nd; i++) {
        const fs_dir_t* d = fs_get_dir(dirs[i]);
        if (!d || !d->used) continue;

        char path[FS_PATH_LEN];
        ksnprintf(path, sizeof(path), "/mnt/%s", d->name);
        int mnt = fs_path_mount(path);
        if (mnt > 0) {
            uint64_t total = 0, free_b = 0;
            if (volume_space(mnt, &total, &free_b) && (required_bytes == 0 || free_b >= required_bytes)) {
                kstrlcpy(dest_vol_path, path, (size_t)dest_cap);
                return 0;
            }
            if (required_bytes == 0) {
                kstrlcpy(dest_vol_path, path, (size_t)dest_cap);
                return 0;
            }
        }
    }
    return -1;
}

int backup_to_volume(const char* external_mnt, const char* const* sources, int num_sources,
                     uint32_t flags, char* out_archive_path, int out_cap,
                     backup_stats_t* st, char* err, int errcap) {
    char target_mnt[FS_PATH_LEN];
    if (external_mnt && external_mnt[0]) {
        int mnt = fs_path_mount(external_mnt);
        if (mnt <= 0) {
            set_err(err, errcap, "specified path is not a mounted distinct volume");
            return -1;
        }
        kstrlcpy(target_mnt, external_mnt, sizeof(target_mnt));
    } else {
        if (backup_find_external_volume(target_mnt, sizeof(target_mnt), 0) != 0) {
            set_err(err, errcap, "no mounted external volume found under /mnt");
            return -1;
        }
    }

    uint32_t now = rtc_now();
    char arc_path[FS_PATH_LEN];
    ksnprintf(arc_path, sizeof(arc_path), "%s/banana_backup_%u.bba", target_mnt, now);

    int rc = backup_create(arc_path, sources, num_sources, flags, NULL, NULL, st, err, errcap);
    if (rc != 0) return -1;

    /* Verify stored archive on the distinct volume */
    rc = backup_verify(arc_path, NULL, NULL, NULL, err, errcap);
    if (rc != 0) {
        fs_delete(arc_path, 0);
        return -1;
    }

    if (out_archive_path && out_cap > 0) {
        kstrlcpy(out_archive_path, arc_path, (size_t)out_cap);
    }
    return 0;
}
