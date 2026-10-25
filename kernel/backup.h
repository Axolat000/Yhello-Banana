#ifndef BACKUP_H
#define BACKUP_H

#include "types.h"
#include "fs.h"

/*
 * Banana Backup Archive (BBA) format v1:
 * Versioned backup format recording paths, sizes, hashes (SHA-256),
 * permissions/mode, uid, gid, symlink targets, and timestamps (mtime, ctime).
 *
 * Binary layout on disk / archive:
 * Header:
 *   char     magic[4];       // "BBA1"
 *   uint32_t version;        // 1
 *   uint32_t flags;          // 0 = uncompressed, 1 = deflated entries
 *   uint32_t entry_count;    // Number of records
 *   uint64_t created_time;   // Unix timestamp (seconds)
 *   uint64_t total_bytes;    // Total uncompressed data bytes
 *   uint32_t header_crc32;   // CRC32 of header fields preceding header_crc32
 *
 * Per entry:
 *   uint16_t type;           // 1 = file, 2 = dir, 3 = symlink
 *   uint16_t flags;          // 0 = stored, 1 = deflated
 *   uint32_t mode;           // Mode/permissions (ext4/Unix mode)
 *   uint32_t uid;            // Owner UID
 *   uint32_t gid;            // Owner GID
 *   uint32_t mtime;          // Modification time
 *   uint32_t ctime;          // Change/creation time
 *   uint64_t size;           // Uncompressed size
 *   uint64_t csize;          // Stored/compressed size in archive
 *   uint8_t  sha256[32];     // SHA-256 of uncompressed payload (zero for dir/symlink)
 *   uint16_t path_len;       // Length of path string (excluding NUL)
 *   uint16_t link_len;       // Length of symlink target (0 if not symlink)
 *   char     path[path_len]; // Relative path within backup root (UTF-8)
 *   char     link[link_len]; // Symlink target if type == 3
 *   uint8_t  payload[csize]; // File payload (deflated or raw stored)
 *
 * Trailer / Footer:
 *   uint32_t trailer_magic;  // 0x41424245 ("EBBA" - End of BBA)
 *   uint64_t archive_size;   // Total archive size in bytes
 *   uint32_t archive_crc32;  // CRC32 of everything preceding trailer
 */

#define BBA_MAGIC "BBA1"
#define BBA_VERSION_1 1u
#define BBA_HDR_LEN 36u
#define BBA_TRAILER_MAGIC 0x41424245u /* "EBBA" little-endian */

enum {
    BBA_ENTRY_FILE = 1,
    BBA_ENTRY_DIR = 2,
    BBA_ENTRY_SYMLINK = 3
};

#define BBA_FLAG_DEFLATE 1u

typedef struct {
    char     path[FS_PATH_LEN];
    char     link_target[FS_PATH_LEN];
    uint16_t type;
    uint16_t flags;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t mtime;
    uint32_t ctime;
    uint64_t size;
    uint64_t csize;
    uint8_t  sha256[32];
} backup_entry_t;

typedef struct {
    uint32_t file_count;
    uint32_t dir_count;
    uint32_t symlink_count;
    uint64_t total_uncompressed_bytes;
    uint64_t archive_bytes;
} backup_stats_t;

/* Progress / inspection callback */
typedef void (*backup_cb_t)(const backup_entry_t* e, void* ctx);

/* Create a versioned backup archive of sources into archive_path.
 * flags: 0 = uncompressed, BBA_FLAG_DEFLATE = compressed entries.
 * Returns 0 on success, or -1 with err message. */
int backup_create(const char* archive_path, const char* const* sources, int num_sources,
                  uint32_t flags, backup_cb_t cb, void* ctx, backup_stats_t* st,
                  char* err, int errcap);

/* Verify an archive's header, trailer, CRC32, and entry SHA-256 hashes without modifying filesystem.
 * Returns 0 on success, or -1 with err. */
int backup_verify(const char* archive_path, backup_cb_t cb, void* ctx, backup_stats_t* st,
                  char* err, int errcap);

/* Find a mounted distinct external volume (/mnt/...) with sufficient free space.
 * Out parameter dest_vol_path receives the mount point path (e.g. "/mnt/usb").
 * If required_bytes > 0, verifies available space.
 * Returns 0 on success, -1 if no distinct volume found. */
int backup_find_external_volume(char* dest_vol_path, int dest_cap, uint64_t required_bytes);

/* Store backup archive directly onto a distinct external volume.
 * If external_mnt is NULL, automatically locates an available mounted volume.
 * Archive is named <out_name> (default: "banana_backup_YYYYMMDD_HHMMSS.bba") under the volume.
 * Returns 0 on success, or -1 on error. */
int backup_to_volume(const char* external_mnt, const char* const* sources, int num_sources,
                     uint32_t flags, char* out_archive_path, int out_cap,
                     backup_stats_t* st, char* err, int errcap);

#endif /* BACKUP_H */
