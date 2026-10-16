#ifndef EXFAT_H
#define EXFAT_H

#include "types.h"
#include "volume.h"

/*
 * exFAT volumes - SD cards over 32 GB (SDXC) and many USB sticks come
 * formatted so. Like FAT32 (kernel/fat32.c): mirrored into the filesystem
 * tree at mount time, contents read on first use, every change written
 * straight to the volume - Unicode names (through the volume's up-case
 * table), files stored in one run of clusters where there is room (no FAT
 * chain), a FAT chain where not. Volumes with two FATs (TexFAT) are
 * mounted read-only.
 */

/* its driver for kernel/volume.c */
extern const volfs_t exfat_volfs;

#endif
