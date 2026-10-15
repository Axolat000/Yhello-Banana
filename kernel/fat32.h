#ifndef FAT32_H
#define FAT32_H

#include "types.h"
#include "blockdev.h"
#include "volume.h"

/*
 * FAT32 volumes (USB sticks): mounted into the filesystem tree (fs.h's
 * mount API) - folders and files appear under the mount point, contents
 * are read on first use, and every change (new files, writes, appends,
 * deletes, renames, new folders) is written straight to the stick, long
 * file names included.
 *
 * kernel/volume.c finds the volume: on the whole device (a "superfloppy")
 * or in a partition of an MBR or GPT partition table.
 */

/* its driver for kernel/volume.c */
extern const volfs_t fat32_volfs;

#endif
