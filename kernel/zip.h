/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef ZIP_H
#define ZIP_H

#include "types.h"
#include "fs.h"

/* ZIP archives (kernel/zip.c): list, extract and create them, on any
 * folder (the RAM disk, the installed disk, USB sticks). Entries are stored
 * or deflated (methods 0 and 8, what every ZIP tool writes); ZIP64 and
 * encrypted entries are not supported. Extracting never writes outside the
 * folder it extracts to ("../" and absolute names are refused). */

typedef struct {
    char     name[FS_PATH_LEN];  /* as in the archive: "folder/", "folder/file.txt" */
    uint32_t size, csize;        /* bytes, uncompressed and in the archive */
    uint32_t mtime;              /* seconds since 1970 (rtc_now's clock) */
    uint32_t crc;
    int      is_dir, method, encrypted;
} zip_entry_t;

/* what an extract or a create did, for messages */
typedef struct {
    int      files, dirs;        /* written / added */
    int      skipped;            /* not extracted: already there, unsafe name, encrypted, ZIP64 */
    int      failed;             /* damaged (bad CRC, bad data) or could not be written */
    uint32_t bytes;              /* uncompressed bytes written / added */
    uint32_t zbytes;             /* the archive's size (create) */
} zip_stats_t;

/* called for each entry as it is listed, extracted or added */
typedef void (*zip_cb_t)(const zip_entry_t* e, const char* note, void* arg);

/* the entries of an archive: their number, or -1 (err says why) */
int zip_list(const char* zip, zip_cb_t cb, void* arg, char* err, int cap);

#define ZIP_OVERWRITE 1          /* extract: replace files that are there (else they are skipped) */
#define ZIP_STORE     2          /* create: no compression */

/* every entry into folder dest (made if needed): 0, or -1 if the archive
 * could not be read at all (err says why; entries that fail are counted
 * in st->failed / skipped, and err has the first such problem) */
int zip_extract(const char* zip, const char* dest, int flags, zip_cb_t cb, void* arg,
                zip_stats_t* st, char* err, int cap);

/* a new archive zip of the files and folders paths[0..n) (folders with
 * all they hold), each under its own name: 0, or -1 (err) */
int zip_create(const char* zip, const char* const* paths, int n, int flags, zip_cb_t cb, void* arg,
               zip_stats_t* st, char* err, int cap);

/* 1 if name ends in .zip (any case) */
int zip_is_zip_name(const char* name);

#endif
