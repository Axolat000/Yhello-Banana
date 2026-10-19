/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "ext4.h"
/* a cut right after the first commit block reaches the disk: nothing written after it */
static int fd, crash_armed, crashed;
static int rd(void* c, uint32_t s, uint32_t n, void* b) { (void)c; return pread(fd, b, (size_t)n * 512, (off_t)s * 512) == (ssize_t)n * 512 ? 0 : -1; }
static int wr(void* c, uint32_t s, uint32_t n, const void* b) {
    (void)c;
    if (crashed) return 0;                                /* (lost) */
    int rc = pwrite(fd, b, (size_t)n * 512, (off_t)s * 512) == (ssize_t)n * 512 ? 0 : -1;
    const uint8_t* p = b;
    if (crash_armed && p[0] == 0xC0 && p[1] == 0x3B && p[2] == 0x39 && p[3] == 0x98 && p[7] == (getenv("PRE") ? 1 : 2)) crashed = 1;
    return rc;
}
static int fl(void* c) { (void)c; return 0; }
static void fill(uint8_t* b, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; b[i] = (uint8_t)(seed >> 16); } }
static uint32_t P(const char* path) {
    uint32_t ino = EXT4_ROOT_INO; char tmp[512]; strcpy(tmp, path);
    for (char* t = strtok(tmp, "/"); t; t = strtok(0, "/")) { ino = ext4_lookup(ino, t, 0); if (!ino) return 0; }
    return ino;
}
int main(int argc, char** argv) {
    int phase = atoi(argv[2]);
    fd = open(argv[1], O_RDWR | (phase == 0 ? O_CREAT | O_TRUNC : 0), 0644);
    if (phase == 0) ftruncate(fd, 300 << 20);
    ext4_dev_t dev = { rd, wr, fl, 0, (300u << 20) / 512 };
    static uint8_t a[3 << 20], got[3 << 20];
    fill(a, sizeof(a), 42);
    if (phase == 0) {
        /* made, then many changes, then the cut in the middle of the flush */
        if (ext4_mkfs(&dev, "BANANA_OS", 0, 0)) return 1;
        uint32_t home = ext4_mkdir(EXT4_ROOT_INO, "home", 0755, 0, 0);
        ext4_flush();
        crash_armed = 1;
        uint32_t d = ext4_mkdir(home, "docs", 0755, 1000, 1000);
        for (int i = 0; i < 300; i++) { char nm[32]; sprintf(nm, "n%03d", i); uint32_t f = ext4_create(d, nm, 0644, 1000, 1000); ext4_write(f, 0, nm, 4); }
        uint32_t big = ext4_create(d, "big.bin", 0644, 1000, 1000);
        ext4_write(big, 0, a, sizeof(a));
        ext4_symlink(d, "ln", "/home/docs/big.bin", 1000, 1000);
        ext4_flush();
        printf("cut %s the commit block: %s\n", getenv("PRE") ? "at the descriptor, before" : "right after", crashed ? "yes" : "NO");
        return 0;
    }
    /* phase 1: mounted again (replays the journal), everything checked */
    if (ext4_mount(&dev)) { printf("FAIL mount\n"); return 1; }
    int bad = 0;
    uint32_t big = P("/home/docs/big.bin");
    if (!big || ext4_read(big, 0, got, sizeof(got)) != (long)sizeof(got) || memcmp(got, a, sizeof(a))) { printf("FAIL big.bin\n"); bad++; }
    for (int i = 0; i < 300; i++) { char nm[32], b[8]; sprintf(nm, "/home/docs/n%03d", i); uint32_t f = P(nm); if (!f || ext4_read(f, 0, b, 8) != 4 || memcmp(b, nm + 11, 4)) { printf("FAIL %s\n", nm); bad++; break; } }
    char t[64]; long n = ext4_read(P("/home/docs/ln"), 0, t, 63);
    if (n != 18) { printf("FAIL ln\n"); bad++; }
    ext4_flush();
    printf("%s after the replay: %d bad\n", bad ? "FAILED" : "PASSED", bad);
    return bad;
}
