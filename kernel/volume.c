/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "volume.h"
#include "fs.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"
#include "fat32.h"
#include "exfat.h"

/* the filesystem drivers, in the order they are tried */
static const volfs_t* const g_fs[] = { &fat32_volfs, &exfat_volfs };
#define NFS ((int)(sizeof(g_fs) / sizeof(g_fs[0])))

static uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static const char* mbr_type_name(uint8_t t) {
    switch (t) {
    case 0x01: return "FAT12";
    case 0x04: case 0x06: case 0x0E: return "FAT16";
    case 0x0B: return "FAT32";
    case 0x0C: return "FAT32 (LBA)";
    case 0x07: return "NTFS / exFAT";
    case 0x0F: case 0x05: case 0x85: return "Extended";
    case 0x82: return "Linux swap";
    case 0x83: return "Linux";
    case 0x8E: return "Linux LVM";
    case 0xA5: return "FreeBSD";
    case 0xAF: return "HFS+";
    case 0xEE: return "GPT protective";
    case 0xEF: return "EFI System";
    case 0xFD: return "Linux RAID";
    case 0x00: return "empty";
    default:   return "other";
    }
}

/* the GPT type GUIDs one meets (the first 4 bytes, as stored) */
static const char* gpt_type_name(const uint8_t* g) {
    static const struct { uint32_t d1; const char* name; } T[] = {
        { 0xC12A7328u, "EFI System" },          { 0xEBD0A0A2u, "Microsoft basic data" },
        { 0xE3C9E316u, "Microsoft reserved" },  { 0xDE94BBA4u, "Windows recovery" },
        { 0x0FC63DAFu, "Linux filesystem" },    { 0x0657FD6Du, "Linux swap" },
        { 0xE6D6D379u, "Linux LVM" },           { 0x21686148u, "BIOS boot" },
        { 0x48465300u, "Apple HFS+" },          { 0x7C3457EFu, "Apple APFS" },
        { 0x4F68BCE3u, "Linux root (x86-64)" }, { 0x933AC7E1u, "Linux /home" },
    };
    uint32_t d1 = rd32(g);
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) if (T[i].d1 == d1) return T[i].name;
    return "other";
}

static void add_part(vol_part_t* out, int* n, int max, int number, uint32_t base, uint32_t sectors,
                     uint8_t mbr, const char* type) {
    if (*n >= max) return;
    vol_part_t* p = &out[(*n)++];
    memset(p, 0, sizeof(*p));
    p->number = number;
    p->base = base;
    p->sectors = sectors;
    p->mbr_type = mbr;
    kstrlcpy(p->type, type, sizeof(p->type));
}

int volume_partitions(blockdev_t* bd, vol_part_t* out, int max, int* is_gpt) {
    int n = 0;
    if (is_gpt) *is_gpt = 0;
    uint8_t* b = (uint8_t*)kmalloc(512);
    if (!b) return 0;
    if (!bd->present || bd->read(bd, 0, 1, b) != 0 || b[510] != 0x55 || b[511] != 0xAA) { kfree(b); return 0; }
    uint8_t mbr[64];
    memcpy(mbr, b + 446, 64);
    int gpt = 0;
    for (int i = 0; i < 4; i++) if (mbr[i * 16 + 4] == 0xEE) gpt = 1;
    if (gpt && bd->read(bd, 1, 1, b) == 0 && memcmp(b, "EFI PART", 8) == 0) {
        if (is_gpt) *is_gpt = 1;
        uint32_t ent_lba = rd32(b + 72), nent = rd32(b + 80), esz = rd32(b + 84);
        if (esz < 128 || esz > 512 || nent > 128) nent = 0;
        for (uint32_t i = 0; i < nent && n < max; i++) {
            uint32_t off = i * esz;
            if (bd->read(bd, ent_lba + off / 512, 1, b) != 0) break;
            const uint8_t* ge = b + off % 512;
            int empty = 1;
            for (int k = 0; k < 16; k++) if (ge[k]) empty = 0;
            if (empty) continue;
            uint32_t first = rd32(ge + 32), last = rd32(ge + 40);
            if (!first || first >= bd->sectors || last < first) continue;
            add_part(out, &n, max, (int)i + 1, first, last - first + 1, 0, gpt_type_name(ge));
            /* the name: UTF-16LE, kept as ASCII */
            char* l = out[n - 1].label;
            int k = 0;
            for (int c = 0; c < 36 && k < (int)sizeof(out[n - 1].label) - 1; c++) {
                uint16_t u = (uint16_t)(ge[56 + 2 * c] | ge[57 + 2 * c] << 8);
                if (!u) break;
                l[k++] = u < 128 ? (char)u : '?';
            }
            l[k] = 0;
        }
        kfree(b);
        return n;
    }
    /* MBR: the four primary entries, then the logical ones in the extended chain */
    uint32_t ext_base = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t* pe = mbr + i * 16;
        uint8_t type = pe[4];
        uint32_t lba = rd32(pe + 8), cnt = rd32(pe + 12);
        if (!type || !lba || lba >= bd->sectors) continue;
        if (type == 0x05 || type == 0x0F || type == 0x85) { if (!ext_base) ext_base = lba; continue; }
        add_part(out, &n, max, i + 1, lba, cnt, type, mbr_type_name(type));
    }
    uint32_t ebr = ext_base;
    for (int number = 5, guard = 0; ebr && guard < 64 && n < max; guard++) {
        if (ebr >= bd->sectors || bd->read(bd, ebr, 1, b) != 0 || b[510] != 0x55 || b[511] != 0xAA) break;
        const uint8_t* e0 = b + 446;
        const uint8_t* e1 = b + 462;
        if (e0[4] && rd32(e0 + 8)) {
            uint32_t lba = ebr + rd32(e0 + 8);
            if (lba < bd->sectors) add_part(out, &n, max, number++, lba, rd32(e0 + 12), e0[4], mbr_type_name(e0[4]));
        }
        uint32_t next = rd32(e1 + 8);
        ebr = (e1[4] && next) ? ext_base + next : 0;
    }
    kfree(b);
    return n;
}

/* the first free /mnt/<kind>, /mnt/<kind>2, ... */
static void free_point(blockdev_t* bd, char* where, int cap) {
    const char* kind = bd->kind[0] ? bd->kind : "usb";
    for (int i = 1; i <= FS_MAX_MOUNTS + 1; i++) {
        if (i == 1) ksnprintf(where, (size_t)cap, "/mnt/%s", kind);
        else ksnprintf(where, (size_t)cap, "/mnt/%s%d", kind, i);
        int d = fs_find_dir(where);
        if (d < 0) return;
        /* an empty folder left from before is fine too */
        int dirs[1], files[1];
        if (!fs_path_mount(where) && fs_list_dirs(where, dirs, 1) == 0 && fs_list_files(where, files, 1) == 0) return;
    }
}

int volume_mount_at(blockdev_t* bd, uint32_t base, const char* path, char* err, int errcap) {
    char where[FS_PATH_LEN];
    if (path) kstrlcpy(where, path, sizeof(where));
    else free_point(bd, where, sizeof(where));
    char first_err[96] = "";
    for (int f = 0; f < NFS; f++) {
        char e[96] = "";
        int mnt = g_fs[f]->mount(bd, base, where, e, sizeof(e));
        if (mnt > 0) {
            blockdev_add_volume(bd, mnt, base);
            return mnt;
        }
        if (mnt == -1 && !first_err[0]) ksnprintf(first_err, sizeof(first_err), "%s: %s", g_fs[f]->name, e);
    }
    kstrlcpy(err, first_err[0] ? first_err : "no FAT32 or exFAT volume there", (size_t)errcap);
    return -1;
}

int volume_mount_all(blockdev_t* bd, char* err, int errcap) {
    err[0] = 0;
    char e[96];
    /* the whole device first: a "superfloppy" has no partition table */
    if (volume_mount_at(bd, 0, NULL, e, sizeof(e)) > 0) return 1;
    vol_part_t* parts = (vol_part_t*)kmalloc(sizeof(vol_part_t) * VOL_MAX_PARTS);
    if (!parts) { kstrlcpy(err, "out of memory", (size_t)errcap); return 0; }
    int np = volume_partitions(bd, parts, VOL_MAX_PARTS, NULL), mounted = 0;
    char first_err[96] = "";
    for (int i = 0; i < np && bd->nvol < BD_MAX_VOLS; i++) {
        if (volume_mount_at(bd, parts[i].base, NULL, e, sizeof(e)) > 0) { mounted++; continue; }
        if (!first_err[0] && strncmp(e, "no FAT32", 8) != 0) ksnprintf(first_err, sizeof(first_err), "partition %d: %s", parts[i].number, e);
    }
    kfree(parts);
    if (!mounted) kstrlcpy(err, first_err[0] ? first_err : (np ? "no FAT32 or exFAT partition" : "no FAT32 or exFAT volume found"), (size_t)errcap);
    else if (mounted > 1) klog("volume: %s - %d volumes mounted\n", bd->name, mounted);
    return mounted;
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
