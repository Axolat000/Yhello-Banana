#include "sdhci.h"
#include "pci.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"
#include "task.h"
#include "blockdev.h"

/*
 * SD card readers: the standard SD Host Controller (PCI class 08:05 -
 * Ricoh, O2 Micro, Intel, Texas Instruments readers and QEMU's sdhci-pci).
 *
 * Polled, with programmed I/O through the buffer data port (no DMA: every
 * controller does it the same way). A task looks at each slot twice a
 * second: a card put in is brought up (SD 1.x, SDHC and SDXC: CMD0, CMD8,
 * ACMD41, CMD2, CMD3, CMD9 for the size, CMD7, 4-bit bus at 25 MHz) and
 * given to the automounter like a USB stick (/mnt/sd); a card taken out is
 * unmounted. MMC cards and UHS speeds are not handled.
 */

#define R_BLKSIZE   0x04
#define R_BLKCOUNT  0x06
#define R_ARG       0x08
#define R_XFERMODE  0x0C
#define R_CMD       0x0E
#define R_RESP      0x10
#define R_DATA      0x20
#define R_PRESENT   0x24
#define R_HOSTCTL   0x28
#define R_POWER     0x29
#define R_CLOCK     0x2C
#define R_TIMEOUT   0x2E
#define R_RESET     0x2F
#define R_INT       0x30
#define R_ERRINT    0x32
#define R_INT_EN    0x34
#define R_ERR_EN    0x36
#define R_SIG_EN    0x38
#define R_ERRSIG_EN 0x3A
#define R_CAPS      0x40
#define R_VERSION   0xFE

#define P_CMD_INHIBIT (1u << 0)
#define P_DAT_INHIBIT (1u << 1)
#define P_CARD        (1u << 16)

#define I_CMD_DONE  (1u << 0)
#define I_XFER_DONE (1u << 1)
#define I_WRITE_RDY (1u << 4)
#define I_READ_RDY  (1u << 5)
#define I_ERROR     (1u << 15)

/* the command register: response kinds and checks */
#define C_NONE    0x00
#define C_R2      (0x01 | 0x08)            /* 136 bits, CRC */
#define C_R3      0x02                     /* 48 bits, no checks */
#define C_R1      (0x02 | 0x08 | 0x10)     /* 48 bits, CRC and index */
#define C_R1B     (0x03 | 0x08 | 0x10)     /* the same, busy after */
#define C_DATA    0x20

#define X_COUNT   (1u << 1)
#define X_ACMD12  (1u << 2)
#define X_READ    (1u << 4)
#define X_MULTI   (1u << 5)

#define MAX_BLOCKS 128                     /* per command (64 KiB) */

typedef struct {
    volatile uint8_t* r;
    uint8_t  version;                      /* 0 = 1.0, 1 = 2.0, 2 = 3.0... */
    uint32_t base_khz;
    int      card;                         /* a working card in the slot */
    int      hc;                           /* SDHC / SDXC: block addresses */
    uint32_t rca;
    uint32_t sectors;
    char     model[40];
    blockdev_t* bd;
    int      failed;                       /* the card in the slot would not start */
} slot_t;

static slot_t g_slot[SDHCI_MAX];
static int    g_count;

static inline uint8_t  r8(slot_t* s, int o)  { return s->r[o]; }
static inline uint16_t r16(slot_t* s, int o) { return *(volatile uint16_t*)(s->r + o); }
static inline uint32_t r32(slot_t* s, int o) { return *(volatile uint32_t*)(s->r + o); }
static inline void w8(slot_t* s, int o, uint8_t v)   { s->r[o] = v; }
static inline void w16(slot_t* s, int o, uint16_t v) { *(volatile uint16_t*)(s->r + o) = v; }
static inline void w32(slot_t* s, int o, uint32_t v) { *(volatile uint32_t*)(s->r + o) = v; }

static void spin_ms(uint32_t ms) {
    uint32_t t0 = timer_ms();
    while (timer_ms() - t0 < ms) __asm__ volatile("pause");
}

static int reset(slot_t* s, uint8_t what) {
    w8(s, R_RESET, what);
    uint32_t t0 = timer_ms();
    while (r8(s, R_RESET) & what) if (timer_ms() - t0 > 200) return -1;
    return 0;
}

/* the SD clock: khz or the nearest below it */
static int set_clock(slot_t* s, uint32_t khz) {
    w16(s, R_CLOCK, 0);
    uint32_t div = 0;                      /* clock = base / (2 * div), 0 = base */
    if (s->base_khz > khz) {
        div = (s->base_khz + 2 * khz - 1) / (2 * khz);
        if (s->version < 2) {              /* 2.0: a power of two, 8 bits */
            uint32_t p = 1;
            while (p < div) p <<= 1;
            div = p > 128 ? 128 : p;
        } else if (div > 1023) div = 1023;
    }
    uint16_t v = (uint16_t)(((div & 0xFF) << 8) | ((div >> 8) & 3) << 6 | 1);
    w16(s, R_CLOCK, v);
    uint32_t t0 = timer_ms();
    while (!(r16(s, R_CLOCK) & 2)) if (timer_ms() - t0 > 100) return -1;   /* internal clock stable */
    w16(s, R_CLOCK, (uint16_t)(v | 4));
    spin_ms(2);
    return 0;
}

/* one command; resp gets the response (4 words for R2); 0 = ok */
static int command(slot_t* s, uint8_t idx, uint32_t arg, uint16_t kind, uint16_t xfer, uint32_t* resp) {
    uint32_t inhibit = P_CMD_INHIBIT | ((kind & C_DATA) || (kind & 3) == 3 ? P_DAT_INHIBIT : 0);
    uint32_t t0 = timer_ms();
    while (r32(s, R_PRESENT) & inhibit) if (timer_ms() - t0 > 500) { reset(s, 6); return -1; }
    w16(s, R_INT, 0xFFFF);
    w16(s, R_ERRINT, 0xFFFF);
    w32(s, R_ARG, arg);
    w16(s, R_XFERMODE, xfer);
    w16(s, R_CMD, (uint16_t)((idx << 8) | kind));
    t0 = timer_ms();
    for (;;) {
        uint16_t st = r16(s, R_INT);
        if (st & I_ERROR) {
            w16(s, R_ERRINT, 0xFFFF);
            w16(s, R_INT, 0xFFFF);
            reset(s, 6);                   /* the command and data lines */
            return -1;
        }
        if (st & I_CMD_DONE) break;
        if (timer_ms() - t0 > 1000) { reset(s, 6); return -1; }
    }
    w16(s, R_INT, I_CMD_DONE);
    if (resp) {
        resp[0] = r32(s, R_RESP);
        if ((kind & 3) == 1) {
            resp[1] = r32(s, R_RESP + 4);
            resp[2] = r32(s, R_RESP + 8);
            resp[3] = r32(s, R_RESP + 12);
        }
    }
    if ((kind & 3) == 3 && !(kind & C_DATA)) {                 /* R1b: the card busy */
        t0 = timer_ms();
        while (!(r16(s, R_INT) & (I_XFER_DONE | I_ERROR)) && (r32(s, R_PRESENT) & P_DAT_INHIBIT))
            if (timer_ms() - t0 > 1000) break;
        w16(s, R_INT, I_XFER_DONE);
    }
    return 0;
}

static int wait_int(slot_t* s, uint16_t bit, uint32_t ms) {
    uint32_t t0 = timer_ms();
    for (;;) {
        uint16_t st = r16(s, R_INT);
        if (st & I_ERROR) { w16(s, R_ERRINT, 0xFFFF); w16(s, R_INT, 0xFFFF); reset(s, 6); return -1; }
        if (st & bit) { w16(s, R_INT, bit); return 0; }
        if (timer_ms() - t0 > ms) { reset(s, 6); return -1; }
    }
}

/* count blocks at lba (count <= MAX_BLOCKS) */
static int transfer(slot_t* s, uint32_t lba, uint32_t count, uint8_t* buf, int write) {
    w16(s, R_BLKSIZE, 512);
    w16(s, R_BLKCOUNT, (uint16_t)count);
    uint16_t xfer = (uint16_t)(X_COUNT | (write ? 0 : X_READ) | (count > 1 ? X_MULTI | X_ACMD12 : 0));
    uint8_t idx = write ? (count > 1 ? 25 : 24) : (count > 1 ? 18 : 17);
    uint32_t addr = s->hc ? lba : lba * 512;
    uint32_t resp[4];
    if (command(s, idx, addr, C_R1 | C_DATA, xfer, resp) != 0) return -1;
    for (uint32_t b = 0; b < count; b++) {
        if (wait_int(s, write ? I_WRITE_RDY : I_READ_RDY, 1000) != 0) return -1;
        uint32_t* p = (uint32_t*)(buf + b * 512);
        if (write) for (int i = 0; i < 128; i++) w32(s, R_DATA, p[i]);
        else       for (int i = 0; i < 128; i++) p[i] = r32(s, R_DATA);
    }
    return wait_int(s, I_XFER_DONE, write ? 3000 : 1000);
}

static int sd_rw(slot_t* s, uint32_t lba, uint32_t count, uint8_t* buf, int write) {
    if (!s->card) return -1;
    if (lba >= s->sectors || count > s->sectors - lba) return -1;
    while (count) {
        uint32_t c = count > MAX_BLOCKS ? MAX_BLOCKS : count;
        if (transfer(s, lba, c, buf, write) != 0) {
            klog("sdhci: %s error at %u\n", write ? "write" : "read", lba);
            return -1;
        }
        buf += c * 512;
        lba += c;
        count -= c;
    }
    return 0;
}

static int bd_read(blockdev_t* bd, uint32_t lba, uint32_t count, void* buf) {
    return sd_rw((slot_t*)bd->priv, lba, count, (uint8_t*)buf, 0);
}
static int bd_write(blockdev_t* bd, uint32_t lba, uint32_t count, const void* buf) {
    return sd_rw((slot_t*)bd->priv, lba, count, (uint8_t*)buf, 1);
}

/* bits hi..lo of the CSD, from an R2 response (which lacks the CRC byte) */
static uint32_t csd_bits(const uint32_t* r, int hi, int lo) {
    uint32_t v = 0;
    for (int b = hi; b >= lo; b--) {
        int n = b - 8;
        v = (v << 1) | ((r[n / 32] >> (n % 32)) & 1);
    }
    return v;
}

/* a card was put in: power, clock, and the SD start-up */
static int card_start(slot_t* s) {
    uint32_t r[4];
    if (reset(s, 1) != 0) return -1;
    uint32_t caps = r32(s, R_CAPS);
    uint8_t volt = (caps & (1u << 24)) ? 7 : (caps & (1u << 25)) ? 6 : 5;   /* 3.3 / 3.0 / 1.8 V */
    w8(s, R_POWER, 0);
    spin_ms(2);
    w8(s, R_POWER, (uint8_t)(volt << 1 | 1));
    spin_ms(10);
    w16(s, R_INT_EN, 0xFFFF);              /* status bits on, interrupts off: polled */
    w16(s, R_ERR_EN, 0xFFFF);
    w16(s, R_SIG_EN, 0);
    w16(s, R_ERRSIG_EN, 0);
    w8(s, R_TIMEOUT, 0x0E);
    w8(s, R_HOSTCTL, 0);
    if (set_clock(s, 400) != 0) return -1;
    spin_ms(2);

    command(s, 0, 0, C_NONE, 0, NULL);     /* idle */
    int v2 = command(s, 8, 0x1AA, C_R1, 0, r) == 0 && (r[0] & 0xFFF) == 0x1AA;
    uint32_t ocr = 0;
    uint32_t t0 = timer_ms();
    for (;;) {
        if (command(s, 55, 0, C_R1, 0, r) != 0) return -1;          /* (an MMC card stops here) */
        if (command(s, 41, (v2 ? 0x40000000u : 0) | 0x00FF8000u, C_R3, 0, r) != 0) return -1;
        ocr = r[0];
        if (ocr & 0x80000000u) break;      /* powered up */
        if (timer_ms() - t0 > 1500) return -1;
        spin_ms(10);
    }
    s->hc = (ocr & 0x40000000u) != 0;
    if (command(s, 2, 0, C_R2, 0, r) != 0) return -1;              /* CID */
    char name[6];
    name[0] = (char)(r[2] >> 24); name[1] = (char)(r[2] >> 16); name[2] = (char)(r[2] >> 8);
    name[3] = (char)r[2]; name[4] = (char)(r[1] >> 24); name[5] = 0;
    for (int i = 0; i < 5; i++) if (name[i] < 32 || name[i] > 126) name[i] = '?';
    if (command(s, 3, 0, C_R1, 0, r) != 0) return -1;              /* the card's address */
    s->rca = r[0] & 0xFFFF0000u;
    if (command(s, 9, s->rca, C_R2, 0, r) != 0) return -1;         /* CSD: the size */
    uint64_t sectors;
    if (csd_bits(r, 127, 126) == 1) {
        sectors = ((uint64_t)csd_bits(r, 69, 48) + 1) * 1024;
    } else {
        uint32_t c_size = csd_bits(r, 73, 62), mult = csd_bits(r, 49, 47), bl = csd_bits(r, 83, 80);
        sectors = ((uint64_t)(c_size + 1) << (mult + 2)) << bl >> 9;
    }
    s->sectors = sectors > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)sectors;
    if (command(s, 7, s->rca, C_R1B, 0, r) != 0) return -1;        /* selected */
    if (!s->hc) command(s, 16, 512, C_R1, 0, r);                   /* 512-byte blocks */
    if (command(s, 55, s->rca, C_R1, 0, r) == 0 && command(s, 6, 2, C_R1, 0, r) == 0)
        w8(s, R_HOSTCTL, (uint8_t)(r8(s, R_HOSTCTL) | 2));         /* 4-bit bus */
    set_clock(s, 25000);
    uint32_t mb = s->sectors / 2048;
    ksnprintf(s->model, sizeof(s->model), "SD card %s, %u %s", name,
              mb >= 10240 ? mb / 1024 : mb, mb >= 10240 ? "GiB" : "MiB");
    s->card = 1;
    klog("sdhci: %s (%s, %u sectors)\n", s->model, s->hc ? "SDHC/SDXC" : "SDSC", s->sectors);
    return 0;
}

static void card_gone(slot_t* s) {
    s->card = 0;
    if (s->bd) { blockdev_unregister(s->bd); s->bd = NULL; }
    w16(s, R_CLOCK, 0);
    w8(s, R_POWER, 0);
    klog("sdhci: card removed\n");
}

static void slots_task(void) {
    task_set_background();
    for (;;) {
        for (int i = 0; i < g_count; i++) {
            slot_t* s = &g_slot[i];
            int in = (r32(s, R_PRESENT) & P_CARD) != 0;
            if (in && !s->card && !s->failed) {
                if (card_start(s) == 0)
                    s->bd = blockdev_register_kind("sd", s->model, s->sectors, 512, bd_read, bd_write, s);
                else {
                    s->failed = 1;         /* (not again until it is taken out) */
                    w8(s, R_POWER, 0);
                    klog("sdhci: the card in slot %d did not start (not an SD card?)\n", i);
                }
            } else if (!in && (s->card || s->failed)) {
                if (s->card) card_gone(s);
                s->failed = 0;
            }
        }
        task_sleep_ms(500);
    }
}

static int scan_cb(const pci_dev_t* d, void* ctx) {
    (void)ctx;
    if (d->class_code != 0x08 || d->subclass != 0x05) return 0;
    /* the slot information register: how many slots, from which BAR */
    uint8_t si = (uint8_t)pci_read32(d->bus, d->dev, d->fn, 0x40);
    int slots = ((si >> 4) & 7) + 1, first = si & 7;
    if (first + slots > 6) { first = 0; slots = 1; }
    pci_enable(d);
    for (int k = 0; k < slots && g_count < SDHCI_MAX; k++) {
        int io = 0;
        uintptr_t bar = pci_bar(d, first + k, &io);
        if (!bar || io) continue;
        slot_t* s = &g_slot[g_count];
        memset(s, 0, sizeof(*s));
        s->r = (volatile uint8_t*)bar;
        s->version = (uint8_t)(r16(s, R_VERSION) & 0xFF);
        uint32_t caps = r32(s, R_CAPS);
        uint32_t mhz = s->version >= 2 ? (caps >> 8) & 0xFF : (caps >> 8) & 0x3F;
        s->base_khz = (mhz ? mhz : 50) * 1000;
        if (reset(s, 1) != 0) continue;
        klog("sdhci: %02x:%02x.%x slot %d, SDHCI %u.00, base clock %u MHz\n",
             d->bus, d->dev, d->fn, k, s->version + 1, s->base_khz / 1000);
        g_count++;
    }
    return 0;
}

void sdhci_init(void) {
    pci_scan(scan_cb, NULL);
    if (g_count) task_create("sdhci", slots_task);
}
