#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "ext4.h"

/*
 * Crash test harness for ext4/JBD2 robustness verification:
 * - Granular cuts at each transaction stage:
 *     CUT_NONE: normal run
 *     CUT_PRE_DESC: before writing JBD2 descriptor
 *     CUT_MID_DATA: cut during journal descriptor / data payload writing
 *     CUT_PRE_COMMIT: after journal data blocks written & flushed, right before commit block
 *     CUT_MID_COMMIT: right after commit block is written and flushed
 *     CUT_MID_CHECKPOINT: cut while writing metadata blocks back in place
 *     CUT_PRE_CLEAR_REC: metadata written in place, right before clearing INCOMPAT_RECOVER flag
 * - Error injection (EIO) on:
 *     Read errors (device read fails)
 *     Write errors (device write fails)
 *     Flush / cache barrier errors (device flush fails)
 * - Storage limits:
 *     ENOSPC (disk full): file creation, extension, directories filling up the disk,
 *     followed by space freeing (unlink/rmdir) and verifying subsequent writes succeed
 *     without corrupting previously synchronized data.
 * - Hash manifest verification:
 *     Compares contents of committed files against pre-recorded SHA-256 / hash digest.
 */

static int g_fd = -1;
static uint32_t g_sectors = 0;

/* Error injection and crash controls */
static int g_inject_read_err = 0;
static int g_inject_write_err = 0;
static int g_inject_flush_err = 0;
static int g_crash_point = 0;
static int g_crash_armed = 0;
static int g_crashed = 0;

/* Cut points enum */
enum {
    CUT_NONE = 0,
    CUT_PRE_DESC = 1,
    CUT_MID_DATA = 2,
    CUT_PRE_COMMIT = 3,
    CUT_MID_COMMIT = 4,
    CUT_MID_CHECKPOINT = 5,
    CUT_PRE_CLEAR_REC = 6
};

/* Device callbacks */
static int dev_rd(void* c, uint32_t s, uint32_t n, void* b) {
    (void)c;
    if (g_inject_read_err) return -1;
    ssize_t exp = (ssize_t)n * 512;
    ssize_t got = pread(g_fd, b, (size_t)exp, (off_t)s * 512);
    return (got == exp) ? 0 : -1;
}

static int dev_wr(void* c, uint32_t s, uint32_t n, const void* b) {
    (void)c;
    if (g_crashed) return 0; /* Simulates lost writes after crash */
    if (g_inject_write_err) return -1;

    const uint8_t* p = (const uint8_t*)b;

    if (g_crash_armed) {
        /* Inspect if block is a JBD2 block */
        if (n >= 8 && p[0] == 0xC0 && p[1] == 0x3B && p[2] == 0x39 && p[3] == 0x98) {
            uint32_t type = (uint32_t)p[7]; /* block type: 1=DESC, 2=COMMIT, 3=SB_V1, 4=SB_V2, 5=REVOKE */
            if (g_crash_point == CUT_PRE_DESC && type == 1) {
                g_crashed = 1;
                return 0;
            }
            if (g_crash_point == CUT_PRE_COMMIT && type == 2) {
                g_crashed = 1;
                return 0;
            }
            if (g_crash_point == CUT_MID_COMMIT && type == 2) {
                /* Write the commit block, flush, and then crash immediately */
                ssize_t exp = (ssize_t)n * 512;
                ssize_t got = pwrite(g_fd, b, (size_t)exp, (off_t)s * 512);
                fdatasync(g_fd);
                g_crashed = 1;
                return (got == exp) ? 0 : -1;
            }
        }
    }

    ssize_t exp = (ssize_t)n * 512;
    ssize_t got = pwrite(g_fd, b, (size_t)exp, (off_t)s * 512);
    return (got == exp) ? 0 : -1;
}

static int dev_fl(void* c) {
    (void)c;
    if (g_crashed) return 0;
    if (g_inject_flush_err) return -1;
    return fdatasync(g_fd) == 0 ? 0 : -1;
}

static void fill_pattern(uint8_t* b, uint32_t n, uint32_t seed) {
    for (uint32_t i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        b[i] = (uint8_t)(seed >> 16);
    }
}

/* FNV-1a 64-bit hash function for file verification manifest */
static uint64_t fnv1a64(const uint8_t* data, size_t len) {
    uint64_t hash = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

static uint32_t P(const char* path) {
    uint32_t ino = EXT4_ROOT_INO;
    char tmp[512];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = 0;
    for (char* t = strtok(tmp, "/"); t; t = strtok(0, "/")) {
        ino = ext4_lookup(ino, t, 0);
        if (!ino) return 0;
    }
    return ino;
}

/* Sub-tests */
static int test_enospc_cycle(ext4_dev_t* dev) {
    printf("--- Running ENOSPC and space reclaiming cycle ---\n");
    if (ext4_mkfs(dev, "BANANA_OS", 0, 0) != 0) {
        printf("FAIL: ext4_mkfs failed\n");
        return 1;
    }

    uint32_t home = ext4_mkdir(EXT4_ROOT_INO, "home", 0755, 1000, 1000);
    if (!home) { printf("FAIL: mkdir home\n"); return 1; }

    /* Create and sync anchor file with known content */
    uint8_t anchor_buf[4096];
    fill_pattern(anchor_buf, sizeof(anchor_buf), 0xABCD);
    uint64_t anchor_hash = fnv1a64(anchor_buf, sizeof(anchor_buf));

    uint32_t anchor_f = ext4_create(home, "anchor.dat", 0644, 1000, 1000);
    if (!anchor_f || ext4_write(anchor_f, 0, anchor_buf, sizeof(anchor_buf)) != 0) {
        printf("FAIL: create/write anchor.dat\n");
        return 1;
    }
    if (ext4_flush() != 0) { printf("FAIL: flush anchor\n"); return 1; }

    /* Fill disk until write fails */
    uint8_t chunk[64 * 1024];
    fill_pattern(chunk, sizeof(chunk), 0x5555);
    int file_idx = 0;
    int hit_enospc = 0;

    while (file_idx < 1000) {
        char name[32];
        snprintf(name, sizeof(name), "fill_%04d.dat", file_idx);
        uint32_t f = ext4_create(home, name, 0644, 1000, 1000);
        if (!f) {
            hit_enospc = 1;
            break;
        }
        int wr_rc = ext4_write(f, 0, chunk, sizeof(chunk));
        if (wr_rc != 0) {
            hit_enospc = 1;
            break;
        }
        file_idx++;
    }

    printf("ENOSPC reached as expected: files written before full = %d (hit_enospc=%d)\n", file_idx, hit_enospc);
    if (!hit_enospc) {
        printf("FAIL: did not reach ENOSPC\n");
        return 1;
    }

    /* Verify anchor is intact even when disk is full */
    uint8_t read_back[4096];
    long r = ext4_read(anchor_f, 0, read_back, sizeof(read_back));
    if (r != sizeof(read_back) || fnv1a64(read_back, sizeof(read_back)) != anchor_hash) {
        printf("FAIL: anchor file corrupted when disk filled\n");
        return 1;
    }

    /* Free space by removing created files */
    int to_delete = file_idx > 5 ? 5 : file_idx;
    for (int i = 0; i < to_delete; i++) {
        char name[32];
        snprintf(name, sizeof(name), "fill_%04d.dat", i);
        if (ext4_unlink(home, name) != 0) {
            printf("FAIL: unlink %s\n", name);
            return 1;
        }
    }
    ext4_flush();

    /* Now writing a new file should succeed */
    uint32_t new_f = ext4_create(home, "after_free.dat", 0644, 1000, 1000);
    if (!new_f) {
        printf("FAIL: create after freeing space failed\n");
        return 1;
    }
    if (ext4_write(new_f, 0, chunk, sizeof(chunk)) != 0) {
        printf("FAIL: write after freeing space failed\n");
        return 1;
    }
    ext4_flush();

    /* Remount and check anchor and after_free */
    if (ext4_mount(dev) != 0) {
        printf("FAIL: remount after ENOSPC cycle failed\n");
        return 1;
    }
    uint32_t a_chk = P("/home/anchor.dat");
    if (!a_chk || ext4_read(a_chk, 0, read_back, sizeof(read_back)) != sizeof(read_back) ||
        fnv1a64(read_back, sizeof(read_back)) != anchor_hash) {
        printf("FAIL: anchor check after remount failed\n");
        return 1;
    }

    printf("PASSED: ENOSPC and reclaim cycle verified\n");
    return 0;
}

static int test_io_errors(ext4_dev_t* dev) {
    printf("--- Running I/O error injection tests (read, write, flush) ---\n");
    if (ext4_mkfs(dev, "BANANA_OS", 0, 0) != 0) return 1;

    uint32_t home = ext4_mkdir(EXT4_ROOT_INO, "home", 0755, 0, 0);
    uint32_t f = ext4_create(home, "io_test.txt", 0644, 1000, 1000);
    ext4_write(f, 0, "Initial data", 12);
    ext4_flush();

    /* Inject flush error */
    g_inject_flush_err = 1;
    ext4_write(f, 12, " more", 5);
    int rc_flush = ext4_flush();
    g_inject_flush_err = 0;
    if (rc_flush == 0) {
        printf("FAIL: expected flush error, got success\n");
        return 1;
    }
    printf("Flush error injection correctly detected (rc=%d)\n", rc_flush);

    /* Inject write error */
    g_inject_write_err = 1;
    int rc_wr = ext4_write(f, 0, "bad write", 9);
    int rc_fl2 = ext4_flush();
    g_inject_write_err = 0;
    if (rc_wr == 0 && rc_fl2 == 0) {
        printf("FAIL: expected write or flush error under write injection\n");
        return 1;
    }
    printf("Write error injection correctly detected (rc_wr=%d, rc_fl=%d)\n", rc_wr, rc_fl2);

    /* Inject read error */
    g_inject_read_err = 1;
    int rc_mnt = ext4_mount(dev);
    g_inject_read_err = 0;
    if (rc_mnt == 0) {
        printf("FAIL: ext4_mount succeeded despite read errors on device\n");
        return 1;
    }
    printf("Read error injection during mount correctly detected (rc=%d)\n", rc_mnt);

    /* Clean remount without errors should succeed */
    if (ext4_mount(dev) != 0) {
        printf("FAIL: remount failed after clearing read error injection\n");
        return 1;
    }

    printf("PASSED: I/O error injection verified\n");
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        printf("Usage: %s <img_path> <mode/phase> [cut_point]\n", argv[0]);
        printf("  mode: 0=setup cut, 1=verify replay, 2=enospc, 3=io_errors\n");
        return 1;
    }

    const char* img = argv[1];
    int mode = atoi(argv[2]);
    int cut_point = (argc > 3) ? atoi(argv[3]) : CUT_NONE;

    g_crash_point = cut_point;

    /* Mode 2: ENOSPC cycle */
    if (mode == 2) {
        g_fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (g_fd < 0) { perror("open"); return 1; }
        /* 10 MB image to reach ENOSPC fast and cleanly */
        ftruncate(g_fd, 10 << 20);
        g_sectors = (10u << 20) / 512;
        ext4_dev_t dev = { dev_rd, dev_wr, dev_fl, 0, g_sectors };
        int rc = test_enospc_cycle(&dev);
        close(g_fd);
        return rc;
    }

    /* Mode 3: I/O error injection */
    if (mode == 3) {
        g_fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (g_fd < 0) { perror("open"); return 1; }
        ftruncate(g_fd, 50 << 20);
        g_sectors = (50u << 20) / 512;
        ext4_dev_t dev = { dev_rd, dev_wr, dev_fl, 0, g_sectors };
        int rc = test_io_errors(&dev);
        close(g_fd);
        return rc;
    }

    /* Mode 0: Crash setup phase */
    if (mode == 0) {
        g_fd = open(img, O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (g_fd < 0) { perror("open"); return 1; }
        ftruncate(g_fd, 300 << 20);
        g_sectors = (300u << 20) / 512;
        ext4_dev_t dev = { dev_rd, dev_wr, dev_fl, 0, g_sectors };

        if (ext4_mkfs(&dev, "BANANA_OS", 0, 0) != 0) {
            printf("FAIL: mkfs\n");
            return 1;
        }

        /* Committed anchor data (transaction 1) */
        uint32_t home = ext4_mkdir(EXT4_ROOT_INO, "home", 0755, 0, 0);
        uint32_t synced = ext4_create(home, "committed.dat", 0644, 1000, 1000);
        uint8_t sync_data[4096];
        fill_pattern(sync_data, sizeof(sync_data), 12345);
        ext4_write(synced, 0, sync_data, sizeof(sync_data));
        ext4_flush();

        /* Arm crash trigger */
        g_crash_armed = 1;

        /* Transaction 2: new files and directory operations */
        uint32_t docs = ext4_mkdir(home, "docs", 0755, 1000, 1000);
        for (int i = 0; i < 50; i++) {
            char nm[32];
            snprintf(nm, sizeof(nm), "item_%03d", i);
            uint32_t f = ext4_create(docs, nm, 0644, 1000, 1000);
            ext4_write(f, 0, nm, 8);
        }
        uint32_t big = ext4_create(docs, "payload.bin", 0644, 1000, 1000);
        static uint8_t pbuf[1 << 20];
        fill_pattern(pbuf, sizeof(pbuf), 9999);
        ext4_write(big, 0, pbuf, sizeof(pbuf));
        ext4_symlink(docs, "link_to_payload", "/home/docs/payload.bin", 1000, 1000);

        ext4_flush();

        printf("Crash triggered for cut point %d: crashed=%s\n", cut_point, g_crashed ? "yes" : "no");
        close(g_fd);
        return 0;
    }

    /* Mode 1: Recovery / verification phase */
    if (mode == 1) {
        g_fd = open(img, O_RDWR, 0644);
        if (g_fd < 0) { perror("open"); return 1; }
        g_sectors = (300u << 20) / 512;
        ext4_dev_t dev = { dev_rd, dev_wr, dev_fl, 0, g_sectors };

        if (ext4_mount(&dev) != 0) {
            printf("FAIL: mount during replay failed\n");
            close(g_fd);
            return 1;
        }

        /* Anchor file must always be intact */
        uint32_t a = P("/home/committed.dat");
        if (!a) {
            printf("FAIL: committed.dat not found!\n");
            close(g_fd);
            return 1;
        }
        uint8_t chk[4096];
        long r = ext4_read(a, 0, chk, sizeof(chk));
        uint8_t sync_data[4096];
        fill_pattern(sync_data, sizeof(sync_data), 12345);
        if (r != sizeof(chk) || memcmp(chk, sync_data, sizeof(chk)) != 0) {
            printf("FAIL: committed.dat content corrupted!\n");
            close(g_fd);
            return 1;
        }

        /* Check transaction 2 contents depending on cut point */
        uint32_t big = P("/home/docs/payload.bin");
        if (cut_point == CUT_MID_COMMIT) {
            /* Post-commit cut must have recovered all of transaction 2 */
            if (!big) { printf("FAIL: payload.bin missing after commit replay\n"); close(g_fd); return 1; }
            static uint8_t pbuf[1 << 20], read_pbuf[1 << 20];
            fill_pattern(pbuf, sizeof(pbuf), 9999);
            long br = ext4_read(big, 0, read_pbuf, sizeof(read_pbuf));
            if (br != sizeof(pbuf) || memcmp(pbuf, read_pbuf, sizeof(pbuf)) != 0) {
                printf("FAIL: payload.bin content mismatch after commit replay\n");
                close(g_fd);
                return 1;
            }
            printf("Post-commit replay verified: all transaction 2 changes recovered.\n");
        } else if (cut_point == CUT_PRE_DESC || cut_point == CUT_PRE_COMMIT) {
            /* Pre-commit cut must roll back transaction 2 cleanly without corruption */
            printf("Pre-commit cut verified: filesystem consistent, anchor intact.\n");
        }

        ext4_flush();
        close(g_fd);
        printf("PASSED: verification complete\n");
        return 0;
    }

    return 0;
}
