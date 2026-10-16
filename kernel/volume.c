#include "volume.h"
#include "fs.h"
#include "kstring.h"
#include "kheap.h"
#include "fat32.h"
#include "exfat.h"

/* the filesystem drivers, in the order they are tried */
static const volfs_t* const g_fs[] = { &fat32_volfs, &exfat_volfs };
#define NFS ((int)(sizeof(g_fs) / sizeof(g_fs[0])))

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

/* where volumes may start: the whole device ("superfloppy"), then the
 * partitions of its MBR or GPT */
static int candidates(blockdev_t* bd, uint32_t* out, int max) {
    int n = 0;
    out[n++] = 0;
    uint8_t* b = (uint8_t*)kmalloc(512);
    if (!b) return n;
    if (!bd->present || bd->read(bd, 0, 1, b) != 0 || b[510] != 0x55 || b[511] != 0xAA) { kfree(b); return n; }
    int gpt = 0;
    for (int i = 0; i < 4 && n < max; i++) {
        const uint8_t* pe = b + 446 + i * 16;
        uint8_t type = pe[4];
        uint32_t lba = rd32(pe + 8);
        if (type == 0xEE) gpt = 1;
        else if (type && type != 0x05 && type != 0x0F && lba && lba < bd->sectors) out[n++] = lba;
    }
    if (gpt && bd->read(bd, 1, 1, b) == 0 && memcmp(b, "EFI PART", 8) == 0) {
        uint32_t ent_lba = rd32(b + 72), nent = rd32(b + 80), esz = rd32(b + 84);
        if (esz < 128 || esz > 512 || nent > 128) nent = 0;
        n = 1;                                  /* (the MBR's protective entry is no volume) */
        for (uint32_t i = 0; i < nent && n < max; i++) {
            uint32_t off = i * esz;
            if (bd->read(bd, ent_lba + off / 512, 1, b) != 0) break;
            const uint8_t* ge = b + off % 512;
            int empty = 1;
            for (int k = 0; k < 16; k++) if (ge[k]) empty = 0;
            uint32_t lba = rd32(ge + 32);
            if (!empty && lba && lba < bd->sectors) out[n++] = lba;
        }
    }
    kfree(b);
    return n;
}

int volume_mount(blockdev_t* bd, const char* path, char* err, int errcap) {
    err[0] = 0;
    char where[FS_PATH_LEN];
    if (path) kstrlcpy(where, path, sizeof(where));
    else {
        const char* kind = bd->kind[0] ? bd->kind : "usb";
        for (int i = 1; i <= FS_MAX_MOUNTS; i++) {
            if (i == 1) ksnprintf(where, sizeof(where), "/mnt/%s", kind);
            else ksnprintf(where, sizeof(where), "/mnt/%s%d", kind, i);
            if (fs_find_dir(where) < 0) break;
        }
    }
    uint32_t cand[17];
    int nc = candidates(bd, cand, 17);
    char first_err[96] = "";
    for (int c = 0; c < nc; c++) {
        for (int f = 0; f < NFS; f++) {
            char e[96] = "";
            int mnt = g_fs[f]->mount(bd, cand[c], where, e, sizeof(e));
            if (mnt > 0) return mnt;
            if (mnt == -1 && !first_err[0]) ksnprintf(first_err, sizeof(first_err), "%s: %s", g_fs[f]->name, e);
        }
    }
    kstrlcpy(err, first_err[0] ? first_err : "no FAT32 or exFAT volume found", (size_t)errcap);
    return -1;
}

int volume_describe(int mnt, char* out, int cap) {
    for (int f = 0; f < NFS; f++) if (g_fs[f]->describe(mnt, out, cap)) return 1;
    return 0;
}

int volume_space(int mnt, uint64_t* total, uint64_t* free_bytes) {
    for (int f = 0; f < NFS; f++) if (g_fs[f]->space(mnt, total, free_bytes)) return 1;
    return 0;
}

const char* volume_fs_name(int mnt) {
    char line[160];
    for (int f = 0; f < NFS; f++) if (g_fs[f]->describe(mnt, line, sizeof(line))) return g_fs[f]->type;
    return "";
}
