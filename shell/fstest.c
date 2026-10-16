#include "fstest.h"
#include "../kernel/fs.h"
#include "../kernel/kstring.h"
#include "../kernel/kheap.h"
#include "../kernel/terminal.h"
#include "../kernel/timer.h"
#include "../kernel/task.h"
#include "../kernel/blockdev.h"
#include "../kernel/volume.h"

/*
 * fstest <mount point> - checks a mounted FAT32 / exFAT drive end to end,
 * from inside Banana OS: it writes into a folder of its own there (files,
 * an append, a big file that grows, a rename, a folder, a move, hundreds of
 * files so a folder outgrows its first cluster, deletes), then unmounts
 * the drive, mounts it again - everything read back from the drive itself,
 * not from memory - and checks every byte. Then it removes its folder
 * (-k keeps it). Works on a real SD card or stick as well as in QEMU.
 */

#define NMANY    300                    /* enough for 16 KiB clusters to need more than one */
#define BIG      (300u * 1024u)
#define BIG_MORE (100u * 1024u)

static int g_fail, g_ok;

static void report(const char* what, int ok, const char* why) {
    char line[160];
    if (ok) {
        g_ok++;
        terminal_write_color("  ok    ", VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
        terminal_writeln(what);
    } else {
        g_fail++;
        terminal_write_color("  FAIL  ", VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
        ksnprintf(line, sizeof(line), "%s%s%s%s", what, why && why[0] ? " - " : "", why ? why : "",
                  fs_io_error() ? " (the drive reported an error)" : "");
        terminal_writeln(line);
    }
}

/* a long step is coming: say so (and let the screen show it) */
static void busy(const char* what) {
    terminal_write_color("  ...   ", VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    terminal_writeln(what);
    task_yield();
}

/* the same pseudo-random bytes every time (xorshift from seed) */
static void pattern(uint8_t* b, uint32_t n, uint32_t seed) {
    uint32_t x = seed * 2654435761u + 1;
    for (uint32_t i = 0; i < n; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        b[i] = (uint8_t)x;
    }
}

/* is the file at path exactly data[0..len)? (why gets what differs) */
static int same(const char* path, const void* data, uint32_t len, char* why, int cap) {
    int idx = fs_find_file(path);
    if (idx < 0) { kstrlcpy(why, "missing", (size_t)cap); return 0; }
    fs_file_t* f = fs_get_file(idx);
    if (!f) { kstrlcpy(why, "cannot read it", (size_t)cap); return 0; }
    if (f->size != len) { ksnprintf(why, (size_t)cap, "%u bytes, not %u", f->size, len); return 0; }
    if (len && memcmp(f->content, data, len) != 0) {
        uint32_t i = 0;
        while (i < len && ((const uint8_t*)f->content)[i] == ((const uint8_t*)data)[i]) i++;
        ksnprintf(why, (size_t)cap, "different from byte %u on", i);
        return 0;
    }
    return 1;
}

/* unmounts the drive behind mount mnt and lets the automount task mount it
 * again: the new mount id (its point may differ), or 0 */
static int remount(int mnt) {
    blockdev_t* bd = NULL;
    for (int i = 0; i < BLOCKDEV_MAX; i++) {
        blockdev_t* b = blockdev_get(i);
        if (b && b->mount_state == 1 && b->mnt == mnt) bd = b;
    }
    if (!bd || !blockdev_eject(mnt)) return 0;
    bd->mount_state = 0;                                    /* (what `mount -a` does) */
    uint32_t t0 = timer_ms();
    while (bd->mount_state == 0 && timer_ms() - t0 < 20000) task_sleep_ms(50);
    return bd->mount_state == 1 ? bd->mnt : 0;
}

int fstest_cmd(int argc, char** argv) {
    const char* where = NULL;
    int keep = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-k")) keep = 1;
        else where = argv[i];
    }
    if (!where) {
        terminal_writeln("usage: fstest [-k] <mount point>    e.g. fstest /mnt/usb   (-k: keep the test folder)");
        return 1;
    }
    int mnt = fs_path_mount(where);
    if (!mnt || strcmp(fs_mount_point(mnt), where) != 0) {
        terminal_writeln("fstest: give the mount point of a FAT32 or exFAT drive (see `mount`)");
        return 1;
    }
    char point[FS_PATH_LEN], d[FS_PATH_LEN], p[FS_PATH_LEN], q[FS_PATH_LEN], why[96];
    kstrlcpy(point, where, sizeof(point));
    const char* type = volume_fs_name(mnt);
    int unicode = !strcmp(type, "exfat");                   /* (FAT32's names here are ASCII only) */
    const char* renamed = unicode ? "héllo wörld.txt" : "hello-renamed.txt";
    g_fail = g_ok = 0;
    uint32_t t0 = timer_ms();
    char line[160];
    ksnprintf(line, sizeof(line), "fstest: %s (%s)", point, type[0] ? type : "?");
    terminal_writeln(line);

    uint8_t* big = (uint8_t*)kmalloc(BIG + BIG_MORE);
    if (!big) { terminal_writeln("fstest: out of memory"); return 1; }
    pattern(big, BIG + BIG_MORE, 7);

    /* ── writing ── */
    ksnprintf(d, sizeof(d), "%s/fstest", point);
    if (fs_find_dir(d) >= 0) fs_delete(d, 1);               /* (left from a run with -k) */
    report("make a folder", fs_mkdir(d) >= 0 && fs_find_dir(d) >= 0, "");

    ksnprintf(p, sizeof(p), "%s/hello.txt", d);
    int idx = fs_write_path(p, "hello", 5);
    report("write a small file", idx >= 0 && same(p, "hello", 5, why, sizeof(why)), why);
    why[0] = 0;
    idx = fs_find_file(p);
    report("append to it", idx >= 0 && fs_append(idx, " world", 6) == 0 && same(p, "hello world", 11, why, sizeof(why)), why);

    ksnprintf(p, sizeof(p), "%s/big.bin", d);
    why[0] = 0;
    report("write 300 KiB", fs_write_path(p, big, BIG) >= 0 && same(p, big, BIG, why, sizeof(why)), why);
    why[0] = 0;
    idx = fs_find_file(p);
    report("append 100 KiB to it", idx >= 0 && fs_append(idx, big + BIG, BIG_MORE) == 0 &&
                                   same(p, big, BIG + BIG_MORE, why, sizeof(why)), why);

    ksnprintf(p, sizeof(p), "%s/hello.txt", d);
    ksnprintf(q, sizeof(q), "%s/%s", d, renamed);
    report(unicode ? "rename (to a Unicode name)" : "rename", fs_move(p, q) >= 0 && fs_find_file(q) >= 0 && fs_find_file(p) < 0, "");

    ksnprintf(p, sizeof(p), "%s/sub", d);
    report("make a folder in it", fs_mkdir(p) >= 0, "");
    ksnprintf(p, sizeof(p), "%s/big.bin", d);
    ksnprintf(q, sizeof(q), "%s/sub/big.bin", d);
    report("move a file into it", fs_move(p, q) >= 0 && fs_find_file(q) >= 0 && fs_find_file(p) < 0, "");

    ksnprintf(line, sizeof(line), "writing %d files...", NMANY);
    busy(line);
    ksnprintf(p, sizeof(p), "%s/many", d);
    int ok = fs_mkdir(p) >= 0;
    for (int i = 0; i < NMANY && ok; i++) {
        char text[32];
        ksnprintf(q, sizeof(q), "%s/many/f%03d.txt", d, i);
        ksnprintf(text, sizeof(text), "file %d\n", i);
        if (fs_write_path(q, text, (uint32_t)strlen(text)) < 0) ok = 0;
        if (i % 50 == 49) task_maybe_yield();
    }
    ksnprintf(line, sizeof(line), "write %d files in one folder", NMANY);
    report(line, ok, "");
    int del = 1;
    for (int i = 0; i < NMANY && del; i += 2) {
        ksnprintf(q, sizeof(q), "%s/many/f%03d.txt", d, i);
        fs_delete(q, 0);
        if (fs_find_file(q) >= 0) del = 0;
    }
    ksnprintf(line, sizeof(line), "delete %d of them", NMANY / 2);
    report(line, del, "");

    /* ── everything again, from the drive ── */
    busy("unmounting and mounting again...");
    int m2 = remount(mnt);
    report("unmount and mount again", m2 > 0, "");
    if (m2 > 0) {
        kstrlcpy(point, fs_mount_point(m2), sizeof(point));
        ksnprintf(d, sizeof(d), "%s/fstest", point);
        ksnprintf(p, sizeof(p), "%s/%s", d, renamed);
        why[0] = 0;
        report("read back: the renamed file", same(p, "hello world", 11, why, sizeof(why)), why);
        ksnprintf(p, sizeof(p), "%s/sub/big.bin", d);
        why[0] = 0;
        report("read back: 400 KiB, every byte", same(p, big, BIG + BIG_MORE, why, sizeof(why)), why);
        int bad = -1;
        for (int i = 0; i < NMANY && bad < 0; i++) {
            char text[32];
            ksnprintf(q, sizeof(q), "%s/many/f%03d.txt", d, i);
            ksnprintf(text, sizeof(text), "file %d\n", i);
            if (i % 2 == 0 ? fs_find_file(q) >= 0 : !same(q, text, (uint32_t)strlen(text), why, sizeof(why))) bad = i;
        }
        ksnprintf(line, sizeof(line), "read back: the %d files left, none of the deleted", NMANY / 2);
        if (bad >= 0) ksnprintf(why, sizeof(why), "f%03d.txt", bad);
        report(line, bad < 0, why);
        if (!keep) {
            fs_delete(d, 1);
            report("remove the test folder", fs_find_dir(d) < 0, "");
        }
    }
    kfree(big);

    ksnprintf(line, sizeof(line), "fstest: %d passed, %d failed (%u ms)", g_ok, g_fail, timer_ms() - t0);
    terminal_write_color(line, g_fail ? VGA_COLOR_LIGHT_RED : VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_putchar('\n');
    return 1;
}
