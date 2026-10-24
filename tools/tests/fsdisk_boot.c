/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Host regression tests of the real installer geometry and checksum code. */
#include "../../kernel/fsdisk.c"
extern int printf(const char*, ...);
extern void exit(int);
extern void* malloc(size_t);
extern void free(void*);
#define CHECK(x) do { if (!(x)) { printf("FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint8_t mbr[512], pvd[2048], table[128*1024], super[512];
static uint32_t table_lba, last_write, written;
static int fail_read;
void* kmalloc(size_t n) { return malloc(n); }
void* kzalloc(size_t n) { void* p = malloc(n); if (p) memset(p, 0, n); return p; }
void kfree(void* p) { free(p); }
uint32_t timer_ms(void) { return 0; }
void task_sleep_ms(uint32_t ms) { (void)ms; }
int disk_read(const ata_disk_t* d, uint32_t lba, uint32_t n, void* out) {
    (void)d;
    if (fail_read) return -1;
    if (!lba && n == 1) memcpy(out, mbr, 512);
    else if (lba == 64 && n == 4) memcpy(out, pvd, 2048);
    else if (lba == table_lba && n*512 <= sizeof(table)) memcpy(out, table, n*512);
    else if (lba == (mbr[466] == 0x83 ? boot_u32(mbr+470) : BOOT_LEGACY_LBA)+2 && n == 1) memcpy(out, super, 512);
    else return -1;
    return 0;
}
int disk_write(const ata_disk_t* d, uint32_t lba, uint32_t n, const void* in) {
    (void)d; last_write=lba; written++;
    if (lba != table_lba || n*512 > sizeof(table)) return -1;
    memcpy(table, in, n*512); return 0;
}
int disk_flush(const ata_disk_t* d) { (void)d; return 0; }
static void partition(uint32_t base, uint32_t n) {
    memset(mbr, 0, sizeof(mbr));
    memcpy(mbr+440, "BnOS", 4); mbr[510]=0x55; mbr[511]=0xaa;
    mbr[466]=0x83; boot_put32(mbr+470, base); boot_put32(mbr+474, n);
}
int main(void) {
    ata_disk_t d; memset(&d,0,sizeof(d)); d.sectors=8u*1024*1024;
    uint32_t capacity=boot_table_lba(BOOT_INSTALL_LBA)*512u;
    uint32_t threshold=(uint32_t)((uint64_t)capacity*2u/3u);
    uint32_t image_max=HTAB_MAX*HASH_BLOCKS*2048u;
    CHECK(boot_install_base(164177920u)==BOOT_INSTALL_LBA);
    CHECK(boot_install_base(0)==BOOT_INSTALL_LBA);
    CHECK(boot_install_base(256u*1024*1024)==BOOT_INSTALL_LBA);
    CHECK(boot_install_base(512u*1024*1024)==BOOT_INSTALL_LBA);
    CHECK(boot_install_base(threshold)==BOOT_INSTALL_LBA);
    CHECK(boot_install_base(threshold+1)==BOOT_MAX_LBA);
    CHECK(boot_install_base(image_max)==BOOT_MAX_LBA);
    CHECK(!boot_install_base(image_max+1));
    CHECK(!boot_install_base(~0u));
    CHECK(boot_image_fits(BOOT_LEGACY_LBA,134184960u));
    CHECK(!boot_image_fits(BOOT_LEGACY_LBA,134184961u));
    CHECK(boot_image_fits(BOOT_MIN_LBA,boot_table_lba(BOOT_MIN_LBA)*512u));
    CHECK(!boot_image_fits(BOOT_MIN_LBA,boot_table_lba(BOOT_MIN_LBA)*512u+1));
    CHECK(boot_image_fits(BOOT_MAX_LBA,image_max));
    CHECK(!boot_image_fits(BOOT_MAX_LBA,image_max+1));
    CHECK(!boot_image_fits(0,0));
    /* Sweep image sizes: every accepted installation has both margins,
     * including its checksum reservation, rather than just an exact fit. */
    for (uint64_t bytes=0;bytes<=image_max;bytes+=65536u) {
        uint32_t chosen=boot_install_base((uint32_t)bytes);
        uint64_t room=(uint64_t)boot_table_lba(chosen)*512u-bytes;
        CHECK(chosen>=BOOT_INSTALL_LBA && chosen<=BOOT_MAX_LBA);
        CHECK(room>=BOOT_HEADROOM && room>=(bytes+1u)/2u);
    }
    /* Read all previously emitted boundaries and the new larger boundary. */
    for (uint32_t b=BOOT_MIN_LBA;b<=BOOT_MAX_LBA;b*=2u) {
        partition(b,d.sectors-b-2048);
        CHECK(boot_partition(&d,0)==b);
        CHECK(boot_table_lba(b)+boot_table_sectors(b)<=b);
    }
    memset(mbr,0,sizeof(mbr));
    CHECK(boot_partition(&d,0)==BOOT_LEGACY_LBA);
    CHECK(boot_table_lba(BOOT_LEGACY_LBA)*512u==134184960u);
    CHECK(boot_table_sectors(BOOT_LEGACY_LBA)==17);
    uint8_t part[16]; partition(BOOT_MIN_LBA,d.sectors-BOOT_MIN_LBA-2048);
    CHECK(boot_partition(&d,part)==BOOT_MIN_LBA);
    CHECK(!memcmp(part,mbr+462,16));
    ext4_dev_t dev; uint32_t base; e4_geometry(&d,&dev,&base);
    CHECK(base==BOOT_MIN_LBA && dev.sectors==d.sectors-base-2048);
    super[0x38]=0x53; super[0x39]=0xef; memcpy(super+0x78,"BANANA_OS",9);
    CHECK(e4_detect(&d));
    table_lba=boot_table_lba(base);
    memset(&g_new,0,sizeof(g_new)); g_new.n=2506; /* image exceeds legacy 2048 chunks */
    for (uint32_t i=0;i<g_new.n;i++) g_new.tab[i]=i*177u;
    CHECK(!htab_save(&d,164177920u));
    CHECK(last_write==table_lba && last_write+boot_table_sectors(base)<=base);
    htab_load(&d); CHECK(g_old.valid && g_old.n==g_new.n);
    CHECK(!memcmp(g_old.tab,g_new.tab,g_new.n*4u));
    table[512+2300*4]^=1; htab_load(&d); CHECK(!g_old.valid);
    CHECK(!htab_drop(&d)); CHECK(!boot_u32(table));
    partition(BOOT_MAX_LBA,d.sectors-BOOT_MAX_LBA-2048);
    table_lba=boot_table_lba(BOOT_MAX_LBA);
    g_new.n=HTAB_MAX;
    for (uint32_t i=0;i<g_new.n;i++) g_new.tab[i]=i*177u;
    CHECK(!htab_save(&d,image_max));
    htab_load(&d); CHECK(g_old.valid && g_old.n==HTAB_MAX);
    CHECK(!memcmp(g_old.tab,g_new.tab,g_new.n*4u));
    partition(BOOT_MIN_LBA+2048,1024); CHECK(!boot_partition(&d,0));
    partition(BOOT_MIN_LBA,d.sectors); CHECK(!boot_partition(&d,0));
    fail_read=1; CHECK(!boot_partition(&d,0)); fail_read=0;
    memset(mbr,0,sizeof(mbr)); CHECK(e4_detect(&d)); table_lba=boot_table_lba(BOOT_LEGACY_LBA);
    g_new.n=1200; CHECK(!htab_save(&d,70u*1024*1024));
    htab_load(&d); CHECK(g_old.valid && g_old.n==1200);
    g_new.n=2506; uint32_t before=written; CHECK(htab_save(&d,164177920u)==-1 && written==before);
    printf("PASS: boot geometry, boundaries, partition validation, new/legacy checksum tables\n");
    return 0;
}
