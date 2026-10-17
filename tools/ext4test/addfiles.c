/* puts N files (in folders of 200) into an installed Banana OS disk image */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ext4.h"
#define BASE 262144u
static int fd;
static int rd(void* c, uint32_t s, uint32_t n, void* b) { (void)c; return pread(fd, b, (size_t)n * 512, (off_t)(BASE + s) * 512) == (ssize_t)n * 512 ? 0 : -1; }
static int wr(void* c, uint32_t s, uint32_t n, const void* b) { (void)c; return pwrite(fd, b, (size_t)n * 512, (off_t)(BASE + s) * 512) == (ssize_t)n * 512 ? 0 : -1; }
static int fl(void* c) { (void)c; return 0; }
int main(int argc, char** argv) {
    fd = open(argv[1], O_RDWR);
    struct stat st; fstat(fd, &st);
    uint32_t total = (uint32_t)(st.st_size / 512);
    ext4_dev_t dev = { rd, wr, fl, 0, ((total - 2048u) & ~7u) - BASE };
    if (ext4_mount(&dev)) { printf("not mounted\n"); return 1; }
    uint32_t home = ext4_lookup(EXT4_ROOT_INO, "home", 0), ban = ext4_lookup(home, "banana", 0);
    uint32_t top = ext4_mkdir(ban, "many", 0755, 1000, 1000);
    int n = atoi(argv[2]);
    uint32_t d = 0;
    for (int i = 0; i < n; i++) {
        char nm[64];
        if (i % 200 == 0) { sprintf(nm, "folder-%03d", i / 200); d = ext4_mkdir(top, nm, 0755, 1000, 1000); }
        sprintf(nm, "a-file-with-a-name-%05d.txt", i);
        uint32_t f = ext4_create(d, nm, 0644, 1000, 1000);
        ext4_write(f, 0, nm, (uint32_t)strlen(nm));
    }
    ext4_flush();
    printf("%d files in %d folders added\n", n, (n + 199) / 200);
    return 0;
}
