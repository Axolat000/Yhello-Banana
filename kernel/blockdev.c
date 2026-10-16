#include "blockdev.h"
#include "volume.h"
#include "fs.h"
#include "task.h"
#include "kstring.h"
#include "serial.h"

static blockdev_t g_bd[BLOCKDEV_MAX];
static volatile int g_go;           /* the disks were looked at (the system disk known): mounting may start */

void blockdev_start_mounting(void) { g_go = 1; }

blockdev_t* blockdev_register(const char* model, uint32_t sectors, uint32_t sector_size,
                              int (*read)(blockdev_t*, uint32_t, uint32_t, void*),
                              int (*write)(blockdev_t*, uint32_t, uint32_t, const void*),
                              void* priv) {
    return blockdev_register_kind("usb", model, sectors, sector_size, read, write, priv);
}

blockdev_t* blockdev_register_kind(const char* kind, const char* model, uint32_t sectors, uint32_t sector_size,
                                   int (*read)(blockdev_t*, uint32_t, uint32_t, void*),
                                   int (*write)(blockdev_t*, uint32_t, uint32_t, const void*),
                                   void* priv) {
    int same = 0;
    for (int i = 0; i < BLOCKDEV_MAX; i++) if (g_bd[i].used && strcmp(g_bd[i].kind, kind) == 0) same++;
    for (int i = 0; i < BLOCKDEV_MAX; i++) {
        blockdev_t* bd = &g_bd[i];
        if (bd->used) continue;
        memset(bd, 0, sizeof(*bd));
        bd->used = 1;
        bd->present = 1;
        kstrlcpy(bd->kind, kind, sizeof(bd->kind));
        ksnprintf(bd->name, sizeof(bd->name), "%s%d", kind, same);
        kstrlcpy(bd->model, model, sizeof(bd->model));
        bd->sectors = sectors;
        bd->sector_size = sector_size;
        bd->read = read;
        bd->write = write;
        bd->priv = priv;
        return bd;
    }
    return NULL;
}

static void drop_volumes(blockdev_t* bd) {
    for (int v = 0; v < bd->nvol; v++) fs_unmount(bd->vol_mnt[v]);     /* (each volume's unmount() forgets it) */
    bd->nvol = 0;
    bd->mnt = 0;
}

void blockdev_unregister(blockdev_t* bd) {
    if (!bd || !bd->used) return;
    bd->present = 0;
    if (bd->nvol) {
        klog("blockdev: %s removed while mounted at %s\n", bd->name, fs_mount_point(bd->mnt));
        drop_volumes(bd);
    }
    bd->used = 0;
}

blockdev_t* blockdev_get(int i) {
    return (i >= 0 && i < BLOCKDEV_MAX && g_bd[i].used) ? &g_bd[i] : NULL;
}

blockdev_t* blockdev_find(const char* name) {
    for (int i = 0; i < BLOCKDEV_MAX; i++) if (g_bd[i].used && strcmp(g_bd[i].name, name) == 0) return &g_bd[i];
    return NULL;
}

void blockdev_add_volume(blockdev_t* bd, int mnt, uint32_t base) {
    if (bd->nvol >= BD_MAX_VOLS) return;
    bd->vol_mnt[bd->nvol] = mnt;
    bd->vol_base[bd->nvol] = base;
    bd->nvol++;
    bd->mnt = bd->vol_mnt[0];
    bd->mount_state = 1;
}

blockdev_t* blockdev_of_mount(int mnt, uint32_t* base) {
    for (int i = 0; i < BLOCKDEV_MAX; i++) {
        blockdev_t* bd = &g_bd[i];
        if (!bd->used) continue;
        for (int v = 0; v < bd->nvol; v++)
            if (bd->vol_mnt[v] == mnt) { if (base) *base = bd->vol_base[v]; return bd; }
    }
    return NULL;
}

/* Mounting reads the whole directory tree, which takes many transfers: that
 * happens here, in a task of its own, rather than inside a driver (the USB
 * stack's enumeration runs from usb_poll()). */
static void automount_task(void) {
    task_set_background();
    for (;;) {
        for (int i = 0; i < BLOCKDEV_MAX; i++) {
            blockdev_t* bd = &g_bd[i];
            if (!g_go || !fs_is_ready() || !bd->used || !bd->present || bd->system || bd->mount_state != 0) continue;
            char err[96];
            int n = volume_mount_all(bd, err, sizeof(err));
            if (n <= 0) {
                bd->mount_state = -1;
                klog("blockdev: %s not mounted: %s\n", bd->name, err);
            }
        }
        task_sleep_ms(250);
    }
}

void blockdev_init(void) {
    task_create("automount", automount_task);
}

int blockdev_eject(int mnt) {
    for (int i = 0; i < BLOCKDEV_MAX; i++) {
        blockdev_t* bd = &g_bd[i];
        if (!bd->used) continue;
        for (int v = 0; v < bd->nvol; v++) {
            if (bd->vol_mnt[v] != mnt) continue;
            fs_unmount(mnt);
            for (int k = v; k + 1 < bd->nvol; k++) { bd->vol_mnt[k] = bd->vol_mnt[k + 1]; bd->vol_base[k] = bd->vol_base[k + 1]; }
            bd->nvol--;
            bd->mnt = bd->nvol ? bd->vol_mnt[0] : 0;
            if (!bd->nvol) bd->mount_state = 2;
            return 1;
        }
    }
    return 0;
}

void blockdev_eject_all(blockdev_t* bd) {
    if (!bd || !bd->used) return;
    drop_volumes(bd);
    bd->mount_state = 2;
}

void blockdev_retry_all(void) {
    for (int i = 0; i < BLOCKDEV_MAX; i++)
        if (g_bd[i].used && g_bd[i].present && !g_bd[i].system && g_bd[i].nvol == 0) g_bd[i].mount_state = 0;
}

void blockdev_set_system(blockdev_t* bd) {
    if (!bd || !bd->used) return;
    if (bd->nvol) drop_volumes(bd);
    bd->system = 1;
    bd->mount_state = 2;
}
