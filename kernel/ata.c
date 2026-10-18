#include "ata.h"
#include "iowait.h"
#include "timer.h"

/* ── I/O helpers (each kernel .c file keeps its own copy - see usb.c) ── */
static inline uint8_t  inb(uint16_t p) { uint8_t  v; __asm__ volatile("inb  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outb(uint16_t p, uint8_t v) { __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint16_t inw(uint16_t p) { uint16_t v; __asm__ volatile("inw  %1,%0":"=a"(v):"Nd"(p)); return v; }
static inline void     outw(uint16_t p, uint16_t v) { __asm__ volatile("outw %0,%1"::"a"(v),"Nd"(p)); }
/* n words in one string instruction: a virtual machine handles it in one
 * go, where an inw / outw each traps into the hypervisor */
static inline void insw_n(uint16_t p, void* buf, uint32_t n) {
    __asm__ volatile("cld; rep insw" : "+D"(buf), "+c"(n) : "d"(p) : "memory");
}
static inline void outsw_n(uint16_t p, const void* buf, uint32_t n) {
    __asm__ volatile("cld; rep outsw" : "+S"(buf), "+c"(n) : "d"(p) : "memory");
}

#define ATA_REG_DATA       0
#define ATA_REG_FEATURES   1
#define ATA_REG_SECCOUNT0  2
#define ATA_REG_LBA0       3
#define ATA_REG_LBA1       4
#define ATA_REG_LBA2       5
#define ATA_REG_HDDEVSEL   6
#define ATA_REG_STATUS     7
#define ATA_REG_COMMAND    7

#define ATA_SR_ERR  0x01
#define ATA_SR_DF   0x20
#define ATA_SR_DRQ  0x08
#define ATA_SR_BSY  0x80

#define ATA_CMD_READ_SECTORS  0x20
#define ATA_CMD_READ_EXT      0x24
#define ATA_CMD_WRITE_SECTORS 0x30
#define ATA_CMD_WRITE_EXT     0x34
#define ATA_CMD_CACHE_FLUSH   0xE7
#define ATA_CMD_FLUSH_EXT     0xEA
#define ATA_CMD_IDENTIFY      0xEC

static uint8_t g_lba48[2][2];      /* [bus][slave]: the drive is past 28-bit LBAs (uses 48-bit commands there) */
static uint8_t g_has48[2][2];      /* ... and the drive has 48-bit commands at all (DMA EXT: 65535 sectors) */

static uint16_t bus_io(int bus)   { return (bus == ATA_BUS_PRIMARY) ? 0x1F0 : 0x170; }
static uint16_t bus_ctrl(int bus) { return (bus == ATA_BUS_PRIMARY) ? 0x3F6 : 0x376; }

/* Reading the alternate status register costs ~100ns on real hardware;
 * four reads is the ATA spec's standard "400ns" settle delay after a
 * drive select or reset, before status bits can be trusted. */
static void settle_400ns(uint16_t ctrl) {
    inb(ctrl); inb(ctrl); inb(ctrl); inb(ctrl);
}

/* Polls STATUS until BSY clears (bounded), returns the last status byte
 * read, or -1 if it never cleared within `spins` iterations. */
static int wait_not_busy(uint16_t io, uint32_t spins) {
    uint8_t st = 0;
    while (spins--) {
        st = inb(io + ATA_REG_STATUS);
        if (!(st & ATA_SR_BSY)) return st;
    }
    return -1;
}

/* Polls until BSY clears and DRQ sets (ready to transfer a sector), or
 * until ERR/DF sets. Returns 0 if ready, -1 on error/timeout. */
static int wait_drq(uint16_t io, uint32_t spins) {
    while (spins--) {
        uint8_t st = inb(io + ATA_REG_STATUS);
        if (st & (ATA_SR_ERR | ATA_SR_DF)) return -1;
        if (!(st & ATA_SR_BSY) && (st & ATA_SR_DRQ)) return 0;
    }
    return -1;
}

static void select_drive(int bus, int is_slave) {
    uint16_t io = bus_io(bus), ctrl = bus_ctrl(bus);
    outb(io + ATA_REG_HDDEVSEL, (uint8_t)(0xA0 | (is_slave ? 0x10 : 0x00)));
    settle_400ns(ctrl);
}

/* Sends IDENTIFY DEVICE and fills *out. Returns 1 if a drive answered at
 * all (ATA or ATAPI - caller must check out->is_atapi), 0 if nothing is
 * wired up at this bus/position. ATAPI (CD/DVD) drives answer the initial
 * select with a "0x14,0xEB" signature in LBA1/LBA2 instead of returning
 * IDENTIFY data - we detect that and bail out without reading further, so
 * the GRUB boot CD is never touched by anything below. */
static int identify_one(int bus, int is_slave, ata_disk_t* out) {
    out->present  = 0;
    out->is_atapi = 0;
    out->bus      = bus;
    out->is_slave = is_slave;
    out->sectors  = 0;
    out->model[0] = '\0';
    out->ahci_port = -1;
    out->nvme = 0;
    out->vblk = 0;

    uint16_t io = bus_io(bus);

    select_drive(bus, is_slave);
    outb(io + ATA_REG_SECCOUNT0, 0);
    outb(io + ATA_REG_LBA0, 0);
    outb(io + ATA_REG_LBA1, 0);
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    uint8_t status = inb(io + ATA_REG_STATUS);
    if (status == 0x00 || status == 0xFF) return 0; /* floating bus - nothing here */

    int st = wait_not_busy(io, 100000);
    if (st < 0) return 0; /* timed out - treat as absent rather than hang forever */

    uint8_t lba1 = inb(io + ATA_REG_LBA1);
    uint8_t lba2 = inb(io + ATA_REG_LBA2);
    if (lba1 == 0x14 && lba2 == 0xEB) {
        out->present  = 1;
        out->is_atapi = 1;
        return 1; /* ATAPI (CD/DVD) - report present, but never a write target */
    }
    if (lba1 != 0 || lba2 != 0) {
        return 0; /* neither plain ATA nor ATAPI signature - don't guess */
    }
    if ((uint8_t)st & ATA_SR_ERR) return 0; /* IDENTIFY aborted */

    if (!((uint8_t)st & ATA_SR_DRQ) && wait_drq(io, 100000) < 0) return 0;

    uint16_t ident[256];
    for (int w = 0; w < 256; w++) ident[w] = inw(io + ATA_REG_DATA);

    out->present  = 1;
    out->is_atapi = 0;
    out->sectors  = ((uint32_t)ident[61] << 16) | ident[60]; /* LBA28 total sectors */
    /* 48-bit addressing (word 83 bit 10): disks past 128 GB, whole (up to 2 TB here) */
    g_lba48[bus & 1][is_slave & 1] = 0;
    g_has48[bus & 1][is_slave & 1] = 0;
    if ((ident[83] & (1u << 10)) && (ident[83] & 0xC000) == 0x4000) {
        g_has48[bus & 1][is_slave & 1] = 1;
        uint64_t s48 = (uint64_t)ident[100] | (uint64_t)ident[101] << 16 | (uint64_t)ident[102] << 32 | (uint64_t)ident[103] << 48;
        if (s48 > out->sectors) {
            out->sectors = s48 > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)s48;
            g_lba48[bus & 1][is_slave & 1] = 1;
        }
    }

    for (int i = 0; i < 20; i++) {
        uint16_t w = ident[27 + i];
        out->model[i * 2]     = (char)(w >> 8);
        out->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    out->model[40] = '\0';
    for (int i = 39; i >= 0 && out->model[i] == ' '; i--) out->model[i] = '\0';

    return 1;
}

static void dma_probe(void);
static int  g_dma_probed;

int ata_probe_disks(ata_disk_t* out) {
    if (!g_dma_probed) dma_probe();                 /* (at boot, with the disks: not inside the first command) */
    static const int buses[2] = { ATA_BUS_PRIMARY, ATA_BUS_SECONDARY };
    int n = 0;
    int found = 0;
    for (int b = 0; b < 2; b++) {
        for (int slave = 0; slave < 2; slave++) {
            ata_disk_t* d = &out[n++];
            if (identify_one(buses[b], slave, d)) found++;
        }
    }
    return found;
}

static void setup_lba28(int bus, int is_slave, uint32_t lba, uint8_t count) {
    uint16_t io = bus_io(bus);
    select_drive(bus, is_slave);
    outb(io + ATA_REG_HDDEVSEL, (uint8_t)(0xE0 | (is_slave ? 0x10 : 0x00) | ((lba >> 24) & 0x0F)));
    outb(io + ATA_REG_FEATURES, 0);
    outb(io + ATA_REG_SECCOUNT0, count);
    outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
}

/* LBA48: the high bytes go first into the same registers (they are FIFOs) */
static void setup_lba48(int bus, int is_slave, uint32_t lba, uint8_t count) {
    uint16_t io = bus_io(bus);
    select_drive(bus, is_slave);
    outb(io + ATA_REG_HDDEVSEL, (uint8_t)(0x40 | (is_slave ? 0x10 : 0x00)));
    outb(io + ATA_REG_SECCOUNT0, 0);                         /* count bits 8-15 */
    outb(io + ATA_REG_LBA0, (uint8_t)(lba >> 24));
    outb(io + ATA_REG_LBA1, 0);                              /* LBA bits 32-47 */
    outb(io + ATA_REG_LBA2, 0);
    outb(io + ATA_REG_SECCOUNT0, count);
    outb(io + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(io + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(io + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
}

/* past the 28-bit range (128 GB) with a drive that has 48-bit commands */
static int use48(int bus, int is_slave, uint32_t lba, uint8_t count) {
    return g_lba48[bus & 1][is_slave & 1] && (uint64_t)lba + count > 0x0FFFFFFFu;
}

int ata_read_sectors(int bus, int is_slave, uint32_t lba, uint8_t count, void* buf) {
    if (count == 0 || !buf) return -1;
    uint16_t io = bus_io(bus);
    if (use48(bus, is_slave, lba, count)) {
        setup_lba48(bus, is_slave, lba, count);
        outb(io + ATA_REG_COMMAND, ATA_CMD_READ_EXT);
    } else {
        setup_lba28(bus, is_slave, lba, count);
        outb(io + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    }

    uint16_t* dst = (uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (wait_drq(io, 1000000) < 0) return -1;
        insw_n(io + ATA_REG_DATA, dst + s * 256, 256);
    }
    return 0;
}

int ata_write_sectors(int bus, int is_slave, uint32_t lba, uint8_t count, const void* buf) {
    if (count == 0 || !buf) return -1;
    uint16_t io = bus_io(bus);
    int ext = use48(bus, is_slave, lba, count);
    if (ext) {
        setup_lba48(bus, is_slave, lba, count);
        outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_EXT);
    } else {
        setup_lba28(bus, is_slave, lba, count);
        outb(io + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    }

    const uint16_t* src = (const uint16_t*)buf;
    for (int s = 0; s < count; s++) {
        if (wait_drq(io, 1000000) < 0) return -1;
        outsw_n(io + ATA_REG_DATA, src + s * 256, 256);
    }
    /* the drive takes it into its cache; ata_flush (disk_flush) makes it stick */
    int st = wait_not_busy(io, 1000000);
    if (st < 0 || (st & (ATA_SR_ERR | ATA_SR_DF))) return -1;
    return 0;
}

int ata_flush(int bus, int is_slave) {
    uint16_t io = bus_io(bus);
    select_drive(bus, is_slave);
    if (wait_not_busy(io, 1000000) < 0) return -1;
    outb(io + ATA_REG_COMMAND, g_lba48[bus & 1][is_slave & 1] ? ATA_CMD_FLUSH_EXT : ATA_CMD_CACHE_FLUSH);
    /* the drive writes its cache out - often a long time (a virtual
     * machine's host syncs its file): the others run meanwhile */
    uint32_t until = timer_ms() + 30000;
    int st;
    while ((st = inb(io + ATA_REG_STATUS)) & ATA_SR_BSY) {
        if ((int32_t)(timer_ms() - until) > 0) return -1;
        io_wait_step();
    }
    return st & (ATA_SR_ERR | ATA_SR_DF) ? -1 : 0;
}

/* ── bus-master DMA (the PCI IDE controller's, as on the PIIX and every
 * chipset since) ─────────────────────────────────────────────────────
 * The controller moves the sectors itself, by a table of regions (PRDs:
 * address, byte count, the last one marked), while the processor does other
 * work; the drive's interrupt (IRQ 14 / 15) says it is done. The PRDs hold
 * 32-bit addresses and a region may not cross a 64 KiB boundary: kernel/
 * disk.c hands over only buffers below 4 GiB (others it copies through
 * one). Commands READ / WRITE DMA (EXT): 256 sectors a command, or with
 * 48-bit addressing up to 65535. */

#include "pci.h"
#include "idt.h"

#define BM_CMD     0
#define BM_STATUS  2
#define BM_PRDT    4
#define BMS_ACTIVE 0x01
#define BMS_ERR    0x02
#define BMS_IRQ    0x04
#define ATA_CMD_READ_DMA      0xC8
#define ATA_CMD_READ_DMA_EXT  0x25
#define ATA_CMD_WRITE_DMA     0xCA
#define ATA_CMD_WRITE_DMA_EXT 0x35
#define PRD_N 32

static inline void outl(uint16_t p, uint32_t v) { __asm__ volatile("outl %0,%1"::"a"(v),"Nd"(p)); }

static int      g_dma_off;
static uint16_t g_bm;                               /* the bus-master registers (BAR4), 0: none */
static iowait_t g_chan_lock[2] = { IOWAIT_INIT, IOWAIT_INIT };
static volatile int g_irq_done[2];
static uint32_t g_prd[2][PRD_N * 2] __attribute__((aligned(256)));   /* (aligned: never across 64 KiB) */

static uint16_t bm_of(int bus) { return (uint16_t)(g_bm + (bus == ATA_BUS_PRIMARY ? 0 : 8)); }

static void ide_irq(void) {
    if (!g_bm) return;
    for (int ch = 0; ch < 2; ch++) {
        uint16_t bm = bm_of(ch ? ATA_BUS_SECONDARY : ATA_BUS_PRIMARY);
        uint8_t st = inb(bm + BM_STATUS);
        if (!(st & BMS_IRQ)) continue;
        outb(bm + BM_STATUS, (uint8_t)(st | BMS_IRQ));          /* (write 1: cleared) */
        inb(bus_io(ch ? ATA_BUS_SECONDARY : ATA_BUS_PRIMARY) + ATA_REG_STATUS);   /* the drive's INTRQ down */
        g_irq_done[ch] = 1;
    }
}

static int find_ide(const pci_dev_t* d, void* ctx) {
    (void)ctx;
    if (d->class_code != 0x01 || d->subclass != 0x01 || !(d->prog_if & 0x80)) return 0;
    int io;
    uintptr_t bar = pci_bar(d, 4, &io);
    if (!bar || !io) return 0;
    pci_enable(d);                                             /* (bus mastering on) */
    g_bm = (uint16_t)bar;
    return 1;
}

static void dma_probe(void) {
    g_dma_probed = 1;
    pci_scan(find_ide, NULL);
    if (!g_bm) return;
    irq_install(14, ide_irq);
    irq_install(15, ide_irq);
}

int ata_dma_available(void) {
    if (!g_dma_probed) dma_probe();
    return g_bm && !g_dma_off;
}

/* count sectors at lba by DMA: 0, -1 (the drive failed), -2 (no DMA: the
 * caller uses PIO). buf: below 4 GiB, an even address, identity-mapped */
int ata_dma_rw(int bus, int is_slave, uint32_t lba, uint32_t count, void* buf, int write) {
    if (!ata_dma_available() || !count || ((uintptr_t)buf & 1)) return -2;
    int ext = g_has48[bus & 1][is_slave & 1];
    if (count > (ext ? 65535u : 256u)) return -2;
    uint32_t ch = bus == ATA_BUS_PRIMARY ? 0 : 1;
    /* the regions: no one across a 64 KiB boundary, each at most 64 KiB */
    uint32_t* prd = g_prd[ch];
    uint32_t n = 0, bytes = count * 512u;
    uintptr_t a = (uintptr_t)buf;
    while (bytes) {
        uint32_t room = 0x10000u - (uint32_t)(a & 0xFFFFu), len = bytes < room ? bytes : room;
        if (n == PRD_N) return -2;
        prd[n * 2] = (uint32_t)a;
        prd[n * 2 + 1] = len & 0xFFFFu;                         /* (0: 64 KiB) */
        a += len;
        bytes -= len;
        n++;
    }
    prd[(n - 1) * 2 + 1] |= 0x80000000u;                        /* end of table */
    uint16_t io = bus_io(bus), bm = bm_of(bus);          /* (the caller holds the channel: ata_lock) */
    int rc = -1;
    if (wait_not_busy(io, 1000000) >= 0) {
        outb(bm + BM_CMD, 0);
        outl(bm + BM_PRDT, (uint32_t)(uintptr_t)prd);
        outb(bm + BM_STATUS, (uint8_t)(inb(bm + BM_STATUS) | BMS_IRQ | BMS_ERR));
        g_irq_done[ch] = 0;
        uint8_t cnt = (uint8_t)count;                           /* (256: 0, as ATA has it) */
        if (ext) {
            setup_lba48(bus, is_slave, lba, 0);
            outb(io + ATA_REG_SECCOUNT0, (uint8_t)(count >> 8)); /* (the count's high byte first) */
            outb(io + ATA_REG_LBA0, (uint8_t)(lba >> 24));
            outb(io + ATA_REG_LBA1, 0);
            outb(io + ATA_REG_LBA2, 0);
            outb(io + ATA_REG_SECCOUNT0, cnt);
            outb(io + ATA_REG_LBA0, (uint8_t)lba);
            outb(io + ATA_REG_LBA1, (uint8_t)(lba >> 8));
            outb(io + ATA_REG_LBA2, (uint8_t)(lba >> 16));
            outb(io + ATA_REG_COMMAND, write ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
        } else {
            setup_lba28(bus, is_slave, lba, cnt);
            outb(io + ATA_REG_COMMAND, write ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA);
        }
        outb(bm + BM_CMD, (uint8_t)(write ? 0x01 : 0x09));     /* start (bit 3: towards memory) */
        uint32_t t0 = timer_ms();
        for (;;) {
            uint8_t st = inb(bm + BM_STATUS);
            if (g_irq_done[ch] || (st & (BMS_IRQ | BMS_ERR)) || !(st & BMS_ACTIVE)) {
                /* (done: the drive may still be finishing - its status says) */
                int ds = wait_not_busy(io, 1000000);
                if (!(st & BMS_ERR) && ds >= 0 && !(ds & (ATA_SR_ERR | ATA_SR_DF))) rc = 0;
                break;
            }
            if (timer_ms() - t0 > 10000u) break;
            io_wait_step();
        }
        outb(bm + BM_CMD, 0);
        outb(bm + BM_STATUS, (uint8_t)(inb(bm + BM_STATUS) | BMS_IRQ | BMS_ERR));
        inb(io + ATA_REG_STATUS);
    }
    return rc;
}

void ata_dma_disable(void) { g_dma_off = 1; }

/* the PIO commands take the channel's lock too (a DMA command may be waiting) */
void ata_lock(int bus)   { io_lock(&g_chan_lock[bus == ATA_BUS_PRIMARY ? 0 : 1]); }
void ata_unlock(int bus) { io_unlock(&g_chan_lock[bus == ATA_BUS_PRIMARY ? 0 : 1]); }

uint32_t ata_dma_max(int bus, int is_slave) { return g_has48[bus & 1][is_slave & 1] ? 65535u : 256u; }
