#ifndef BLOCKDEV_H
#define BLOCKDEV_H

#include "types.h"

/*
 * Block devices other than the installed disk: USB sticks, NVMe, virtio,
 * SD cards, and the extra IDE / SATA disks (kernel/disk.c). Each driver
 * registers its drives here; the automount task (kernel/volume.c) mounts
 * every volume it finds on them - the whole device, or each partition of
 * its MBR (primary and logical) or GPT - under /mnt/<kind>[N]. The disk
 * Banana OS is installed on is marked `system` and never mounted.
 */

#define BLOCKDEV_MAX 16
#define BD_MAX_VOLS  8                /* volumes mounted from one device */

typedef struct blockdev {
    int      used;
    int      present;            /* 0 once unplugged */
    char     name[16];           /* "usb0", "nvme0", "sata1" */
    char     kind[8];            /* "usb", "nvme", "sata", "ide": the mount point is /mnt/<kind>[N] */
    char     model[40];          /* "QEMU QEMU HARDDISK" */
    uint32_t sectors;            /* 512-byte sectors */
    uint32_t sector_size;
    /* count sectors at lba; 0 = ok */
    int    (*read)(struct blockdev* bd, uint32_t lba, uint32_t count, void* buf);
    int    (*write)(struct blockdev* bd, uint32_t lba, uint32_t count, const void* buf);
    void*    priv;
    int      mount_state;        /* 0 not tried yet, 1 mounted (a volume or more), -1 no usable volume,
                                    2 unmounted by hand (left alone until `mount -a`) */
    int      mnt;                /* the first volume's fs mount id while mounted */
    int      nvol;               /* its mounted volumes: mount id and first sector */
    int      vol_mnt[BD_MAX_VOLS];
    uint32_t vol_base[BD_MAX_VOLS];
    int      system;             /* holds Banana OS (installed): never mounted */
} blockdev_t;

/* a driver adds / removes a device (removing unmounts it first) */
blockdev_t* blockdev_register(const char* model, uint32_t sectors, uint32_t sector_size,
                              int (*read)(blockdev_t*, uint32_t, uint32_t, void*),
                              int (*write)(blockdev_t*, uint32_t, uint32_t, const void*),
                              void* priv);
/* the same for another kind of drive ("nvme") */
blockdev_t* blockdev_register_kind(const char* kind, const char* model, uint32_t sectors, uint32_t sector_size,
                                   int (*read)(blockdev_t*, uint32_t, uint32_t, void*),
                                   int (*write)(blockdev_t*, uint32_t, uint32_t, const void*),
                                   void* priv);
void        blockdev_unregister(blockdev_t* bd);
blockdev_t* blockdev_get(int i);          /* NULL if slot i is unused */
blockdev_t* blockdev_find(const char* name);   /* "sata1", or NULL */

/* a mounted volume of bd (kernel/volume.c calls these) */
void blockdev_add_volume(blockdev_t* bd, int mnt, uint32_t base);
/* the device and first sector behind mount mnt: the device, or NULL */
blockdev_t* blockdev_of_mount(int mnt, uint32_t* base);

/* umount: unmounts the volume with fs mount id mnt (the rest of its drive
 * stays); a drive left with none is not mounted again by itself; 1 if it
 * was one of ours */
int  blockdev_eject(int mnt);
/* every volume of bd */
void blockdev_eject_all(blockdev_t* bd);
/* `mount -a`: try every unmounted drive again */
void blockdev_retry_all(void);
/* marks bd as the installed system's disk: its volumes are unmounted and
 * it is never mounted again */
void blockdev_set_system(blockdev_t* bd);

/* once the boot knows which disk is the system's (kernel/disk.c): mounting starts */
void blockdev_start_mounting(void);
/* starts the task that mounts newly plugged sticks */
void blockdev_init(void);

#endif
