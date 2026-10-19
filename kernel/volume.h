/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef VOLUME_H
#define VOLUME_H

#include "types.h"
#include "blockdev.h"

/*
 * The volumes on removable and extra drives (USB sticks, SD cards, NVMe,
 * virtio, extra IDE / SATA disks): kernel/blockdev.c's automount task hands
 * each new drive to volume_mount_all, which mounts the whole device if it is
 * a volume itself ("superfloppy"), else every partition of its MBR (primary
 * and logical, through the extended partition's chain) or GPT that one of
 * the filesystem drivers knows (FAT32, exFAT, ...) - each under the first
 * free /mnt/<kind>, /mnt/<kind>2...
 */

/* what a filesystem driver gives: mount returns the fs mount id, -2 when
 * the volume at base is not its format (err untouched), -1 with err when it
 * is but cannot be mounted */
typedef struct {
    const char* name;                                       /* "FAT32" */
    const char* type;                                       /* "fat32": what df shows */
    int (*mount)(blockdev_t* bd, uint32_t base, const char* where, char* err, int errcap);
    int (*describe)(int mnt, char* out, int cap);           /* 0 if mnt is not its */
    int (*space)(int mnt, uint64_t* total, uint64_t* free_bytes);
} volfs_t;

/* one partition (or the whole device) of a drive */
#define VOL_MAX_PARTS 32
typedef struct {
    int      number;             /* 1, 2... (MBR: 1-4 primary, 5... logical; GPT: its entry + 1); 0 = whole device */
    uint32_t base, sectors;
    uint8_t  mbr_type;           /* MBR type byte (0 for GPT) */
    char     type[24];           /* "FAT32 (LBA)", "Linux", "EFI System", "Microsoft basic data"... */
    char     label[40];          /* GPT partition name ("" if none) */
} vol_part_t;

/* the drive's partition table: how many (0: none - maybe a superfloppy) */
int  volume_partitions(blockdev_t* bd, vol_part_t* out, int max, int* is_gpt);
/* mounts every volume on bd (see above): how many, or 0 with err */
int  volume_mount_all(blockdev_t* bd, char* err, int errcap);
/* the volume at sector base of bd at path (NULL: the first free /mnt/<kind>[N]):
 * the mount id, or -1 with err */
int  volume_mount_at(blockdev_t* bd, uint32_t base, const char* path, char* err, int errcap);
/* `mount`: one line about mount mnt (0 if it is no volume's) */
int  volume_describe(int mnt, char* out, int cap);
/* `df`: its size and free space (0 if it is no volume's) */
int  volume_space(int mnt, uint64_t* total, uint64_t* free_bytes);
/* its filesystem for `df` ("fat32", "exfat"...; "" if it is no volume's) */
const char* volume_fs_name(int mnt);

#endif
