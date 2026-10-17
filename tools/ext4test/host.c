#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "ext4.h"

static int fd;
static int rd(void* c, uint32_t s, uint32_t n, void* b) { (void)c; return pread(fd, b, (size_t)n * 512, (off_t)s * 512) == (ssize_t)n * 512 ? 0 : -1; }
static int wr(void* c, uint32_t s, uint32_t n, const void* b) { (void)c; return pwrite(fd, b, (size_t)n * 512, (off_t)s * 512) == (ssize_t)n * 512 ? 0 : -1; }
static int fl(void* c) { (void)c; return 0; }
static int fails;
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); fails++; } } while (0)

static void fill(uint8_t* b, uint32_t n, uint32_t seed) { for (uint32_t i = 0; i < n; i++) { seed = seed * 1103515245u + 12345u; b[i] = (uint8_t)(seed >> 16); } }

static uint32_t P(const char* path) {        /* inode of an absolute path */
    uint32_t ino = EXT4_ROOT_INO;
    char tmp[512]; strcpy(tmp, path);
    for (char* t = strtok(tmp, "/"); t; t = strtok(0, "/")) { ino = ext4_lookup(ino, t, 0); if (!ino) return 0; }
    return ino;
}
static int verify(uint32_t ino, const uint8_t* want, uint32_t n) {
    uint8_t* got = malloc(n + 10); long r = ext4_read(ino, 0, got, n + 10);
    int ok = r == (long)n && !memcmp(got, want, n); free(got); return ok;
}

int main(int argc, char** argv) {
    uint64_t mb = argc > 2 ? strtoull(argv[2], 0, 10) : 300;
    fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0644);
    ftruncate(fd, (off_t)(mb << 20));
    ext4_dev_t dev = { rd, wr, fl, 0, (uint32_t)((mb << 20) / 512) };
    CHECK(ext4_mkfs(&dev, "BANANA_OS", 0, 0) == 0);
    uint32_t home = ext4_mkdir(EXT4_ROOT_INO, "home", 0755, 0, 0);
    uint32_t ban = ext4_mkdir(home, "banana", 0755, 1000, 1000);
    CHECK(home && ban);
    /* files of all sizes */
    static uint8_t big[24 << 20], b2[1 << 20];
    uint32_t sizes[] = { 0, 1, 10, 4095, 4096, 4097, 5000, 65536, 1 << 20, 24 << 20 };
    for (int i = 0; i < 10; i++) {
        char nm[32]; sprintf(nm, "f%d", i);
        uint32_t f = ext4_create(ban, nm, 0644, 1000, 1000);
        fill(big, sizes[i], i + 1);
        /* written in uneven pieces */
        uint32_t off = 0, step = 777 + i * 1000;
        while (off < sizes[i]) { uint32_t n = sizes[i] - off < step ? sizes[i] - off : step; CHECK(ext4_write(f, off, big + off, n) == 0); off += n; }
        CHECK(verify(f, big, sizes[i]));
    }
    /* a big one written whole, then changed in the middle, shrunk and grown */
    uint32_t f = ext4_create(ban, "movie.bin", 0644, 1000, 1000);
    fill(big, 24 << 20, 99);
    CHECK(ext4_write(f, 0, big, 24 << 20) == 0);
    fill(b2, 100000, 5); memcpy(big + 3000000, b2, 100000);
    CHECK(ext4_write(f, 3000000, b2, 100000) == 0);
    CHECK(verify(f, big, 24 << 20));
    CHECK(ext4_truncate(f, 10000000) == 0);
    CHECK(verify(f, big, 10000000));
    CHECK(ext4_truncate(f, 12000000) == 0);
    memset(big + 10000000, 0, 2000000);
    CHECK(verify(f, big, 12000000));
    /* two files written in turns: fragmented, more than 4 extents (a tree) */
    uint32_t a = ext4_create(ban, "frag-a", 0644, 1000, 1000), b = ext4_create(ban, "frag-b", 0644, 1000, 1000);
    static uint8_t fa[8 << 20], fb[8 << 20];
    fill(fa, 8 << 20, 7); fill(fb, 8 << 20, 8);
    for (uint32_t off = 0; off < (8u << 20); off += 8192) { ext4_write(a, off, fa + off, 8192); ext4_write(b, off, fb + off, 8192); }
    CHECK(verify(a, fa, 8 << 20)); CHECK(verify(b, fb, 8 << 20));
    /* links, folders, renames, deletes */
    CHECK(ext4_symlink(ban, "short", "/home/banana/f3", 1000, 1000));
    char longt[200]; memset(longt, 0, 200); for (int i = 0; i < 150; i++) longt[i] = (char)(97 + i % 26);
    CHECK(ext4_symlink(ban, "long", longt, 1000, 1000));
    uint32_t docs = ext4_mkdir(ban, "Documents", 0755, 1000, 1000), sub = ext4_mkdir(docs, "sub", 0755, 1000, 1000);
    CHECK(docs && sub);
    CHECK(ext4_rename(ban, "f5", docs, "renamed5") == 0);
    CHECK(ext4_rename(docs, "sub", ban, "moved-sub") == 0);
    CHECK(ext4_unlink(ban, "f2") == 0);
    CHECK(ext4_rmdir(ban, "moved-sub") == 0);
    CHECK(ext4_rmdir(home, "banana") != 0);           /* not empty */
    /* a folder of 2000 files (many blocks), half deleted */
    uint32_t many = ext4_mkdir(ban, "many", 0755, 1000, 1000);
    for (int i = 0; i < 2000; i++) { char nm[64]; sprintf(nm, "file-with-a-longer-name-%04d.txt", i); uint32_t x = ext4_create(many, nm, 0644, 1000, 1000); CHECK(x); ext4_write(x, 0, nm, (uint32_t)strlen(nm)); }
    for (int i = 0; i < 2000; i += 2) { char nm[64]; sprintf(nm, "file-with-a-longer-name-%04d.txt", i); CHECK(ext4_unlink(many, nm) == 0); }
    uint32_t sw = ext4_create(EXT4_ROOT_INO, "swapfile", 0600, 0, 0);
    CHECK(ext4_fallocate(sw, 64u << 20) == 0);
    uint32_t s0, sn; CHECK(ext4_first_run(sw, &s0, &sn) == 0); printf("swap run: sector %u, %u sectors\n", s0, sn);
    CHECK(ext4_flush() == 0);
    /* mounted again: everything still there */
    CHECK(ext4_mount(&dev) == 0);
    CHECK(verify(P("/home/banana/frag-a"), fa, 8 << 20));
    fill(big, 24 << 20, 99); memcpy(big + 3000000, b2, 100000); memset(big + 10000000, 0, 2000000);
    CHECK(verify(P("/home/banana/movie.bin"), big, 12000000));
    fill(big, 4097, 6); CHECK(verify(P("/home/banana/Documents/renamed5"), big, 4097));
    char t[256]; long n = ext4_read(P("/home/banana/long"), 0, t, 255); CHECK(n == 150 && !memcmp(t, longt, 150));
    n = ext4_read(P("/home/banana/short"), 0, t, 255); CHECK(n == 15 && !memcmp(t, "/home/banana/f3", 15));
    CHECK(P("/home/banana/f2") == 0);
    CHECK(P("/home/banana/many/file-with-a-longer-name-1999.txt") != 0);
    CHECK(P("/home/banana/many/file-with-a-longer-name-1998.txt") == 0);
    CHECK(ext4_flush() == 0);
    printf("%s: %d failed\n", fails ? "FAILED" : "PASSED", fails);
    return fails != 0;
}
