#ifndef ZSWAP_H
#define ZSWAP_H

#include "types.h"

/*
 * Compressed memory, a swap in RAM (64-bit kernel). When memory runs low,
 * pages of apps' mmap regions (a Java heap...) that were not used lately
 * are compressed into the kernel heap and their page goes back to the
 * system (kernel/upage.c); touching one again decompresses it in the page
 * fault (kernel/uspace.c), on any core. An all-zero page takes no room.
 * Windows 10 and Linux (zram, zswap) do the same before - or instead of -
 * writing pages to a disk.
 *
 * A compressed page is an entry of a table: its page table entry holds the
 * entry's number. While a page is being compressed the entry still holds
 * the page itself (PENDING): a touch then simply puts it back.
 */

#define ZS_FREE      0
#define ZS_PENDING   1      /* taken out of the app's tables, being compressed: frame is the page */
#define ZS_STORED    2      /* data / len hold it (len 0: a page of zeros) */
#define ZS_CANCELLED 3      /* the page went back (touched) or away (unmapped) while pending */

typedef struct {
    uint8_t*  data;
    uint32_t  len;
    uint32_t  state;
    uintptr_t frame;
} zent_t;

/* a new entry (PENDING, holding frame): its number (>= 1), or 0 */
uint32_t zswap_new(uintptr_t frame);
zent_t*  zswap_ent(uint32_t idx);
void     zswap_free(uint32_t idx);        /* the entry and its data */
/* a PENDING entry gets its compressed data (kmalloc'd, owned from now on;
 * NULL and 0 for a page of zeros): STORED */
void     zswap_store(uint32_t idx, uint8_t* data, uint32_t len);
/* a STORED entry back into page, and the entry freed: 0, or -1 (damaged) */
int      zswap_take(uint32_t idx, uint8_t* page);

/* 4096 bytes -> out (cap bytes): the compressed size, or 0 if it did not
 * fit (a page that does not compress); zero pages: *zero = 1, returns 0.
 * ht: 4096 uint16_t of work space */
uint32_t zswap_pack(const uint8_t* page, uint8_t* out, uint32_t cap, int* zero, uint16_t* ht);
/* back into a 4096-byte page: 0, or -1 if the data is damaged */
int      zswap_unpack(const uint8_t* in, uint32_t len, uint8_t* page);

/* the background task: compresses when RAM runs low */
void     zswap_start(void);
/* every app page not in use right now compressed (the `swap now` command,
 * selftest): how many were */
uint32_t zswap_compress_now(void);
/* on (1, the default) or off: when off, nothing new is compressed */
void     zswap_enable(int on);
int      zswap_enabled(void);

/* what is compressed: pages, the bytes they take, and how often a page was
 * brought back */
typedef struct {
    uint32_t pages, zero_pages;
    uint64_t bytes;                       /* the compressed data */
    uint64_t pages_out, pages_in;         /* since boot */
} zswap_stats_t;
void zswap_stats(zswap_stats_t* s);

#endif
