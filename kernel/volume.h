#ifndef VOLUME_H
#define VOLUME_H

#include "types.h"
#include "blockdev.h"

/*
 * The volumes on removable and extra drives (USB sticks, SD cards, NVMe,
 * virtio disks): kernel/blockdev.c's automount task hands each new drive
 * to volume_mount, which looks at the whole device and at every partition
 * of its MBR or GPT and mounts the first volume one of the filesystem
 * drivers knows (FAT32, exFAT, ...) under /mnt/<kind>[N].
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

/* mounts the first volume found on bd at path (NULL: the first free
 * /mnt/<kind>, /mnt/<kind>2...): the mount id, or -1 with err */
int volume_mount(blockdev_t* bd, const char* path, char* err, int errcap);
/* `mount`: one line about mount mnt (0 if it is no volume's) */
int volume_describe(int mnt, char* out, int cap);
/* `df`: its size and free space (0 if it is no volume's) */
int volume_space(int mnt, uint64_t* total, uint64_t* free_bytes);
/* its filesystem for `df` ("fat32", "exfat"...; "" if it is no volume's) */
const char* volume_fs_name(int mnt);

#endif
