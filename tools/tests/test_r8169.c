/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hosted run of net/r8169.c against a model of the Realtek RTL8111/8168
 * (the mainboard chip), through its I/O-port path (tools/tests/r8169/io.h
 * routes inb/outb here). The model: reset, the C+ descriptor rings (OWN,
 * EOR, FS/LS), transmit poll, interrupt status (write 1 to clear), the PHY
 * through PHYAR, the MAC OCP registers through OCPDR, and on the 8168g and
 * later the state a firmware may leave: its own controller in charge
 * (MCU NOW_IS_OOB), the receive path gated (MISC RXDV_GATED_EN), the PLL
 * off (PMCH) - then frames go out and none come in.
 *   cc -O1 -no-pie -w -I tools/tests/r8169 -I kernel -I net tools/tests/test_r8169.c net/r8169.c -o /tmp/r8169 && /tmp/r8169 */
#include "netdev.h"
#include "pci.h"

extern int printf(const char*, ...);
extern void* memcpy(void*, const void*, size_t);
extern void* memset(void*, int, size_t);
extern int memcmp(const void*, const void*, size_t);

/* ── the chip ── */
static uint8_t  R[256];
static uint16_t ocp[0x8000];
static int      model_g;                  /* an 8168g+ (the firmware's states apply) */
static int      sent, sent_ok_frame;
static uint8_t  last_tx[2048];
static uint32_t last_tx_len;
static uint32_t rx_slot, tx_slot;

typedef struct { uint32_t opts1, opts2, lo, hi; } desc_t;
static desc_t* ring(uint32_t reg) { return (desc_t*)(uintptr_t)(R[reg] | R[reg + 1] << 8 | R[reg + 2] << 16 | (uint32_t)R[reg + 3] << 24); }

static uint32_t get32(int r) { return R[r] | R[r + 1] << 8 | R[r + 2] << 16 | (uint32_t)R[r + 3] << 24; }
static void put32(int r, uint32_t v) { R[r] = (uint8_t)v; R[r + 1] = (uint8_t)(v >> 8); R[r + 2] = (uint8_t)(v >> 16); R[r + 3] = (uint8_t)(v >> 24); }

static int rx_path_open(void) {
    if (!(R[0x37] & 0x08)) return 0;                                 /* CMD RE */
    if (model_g && (R[0xD3] & 0x80)) return 0;                       /* the firmware's controller has it */
    if (model_g && (get32(0xF0) & 0x00080000u)) return 0;            /* receive path gated */
    if (model_g && (R[0x6F] & 0xC0) != 0xC0) return 0;               /* no PLL */
    return 1;
}

static void tx_poll(void) {
    desc_t* t = ring(0x20);
    if (!t) return;
    for (int n = 0; n < 64; n++) {
        desc_t* d = &t[tx_slot];
        if (!(d->opts1 & 0x80000000u)) break;
        if (!(R[0x37] & 0x04)) break;                                /* CMD TE */
        uint32_t len = d->opts1 & 0xFFFF;
        memcpy(last_tx, (void*)(uintptr_t)d->lo, len < sizeof(last_tx) ? len : sizeof(last_tx));
        last_tx_len = len;
        sent++;
        d->opts1 &= ~0x80000000u;
        R[0x3E] |= 0x04;                                             /* TOK */
        tx_slot = (d->opts1 & 0x40000000u) ? 0 : tx_slot + 1;
    }
}

static int inject_rx(const uint8_t* f, uint32_t len) {
    if (!rx_path_open()) return 0;
    desc_t* r = ring(0xE4);
    desc_t* d = &r[rx_slot];
    if (!(d->opts1 & 0x80000000u)) return 0;                         /* no buffer given */
    memcpy((void*)(uintptr_t)d->lo, f, len);
    memset((uint8_t*)(uintptr_t)d->lo + len, 0xCC, 4);               /* the CRC */
    uint32_t eor = d->opts1 & 0x40000000u;
    d->opts1 = eor | 0x20000000u | 0x10000000u | (len + 4);
    rx_slot = eor ? 0 : rx_slot + 1;
    R[0x3E] |= 0x01;                                                 /* ROK */
    return 1;
}

static uint32_t rd(uint16_t port, int n) {
    int r = port - 0xC000;
    if (r == 0x40) put32(0x40, get32(0x40) | 0x800);                 /* TCR: TXCFG_EMPTY */
    if (r == 0xD3) R[0xD3] |= 0x32;                                  /* MCU: RX/TX empty, link list ready */
    uint32_t v = 0;
    for (int i = 0; i < n; i++) v |= (uint32_t)R[r + i] << (8 * i);
    return v;
}

static void wr(uint16_t port, uint32_t v, int n) {
    int r = port - 0xC000;
    if (r == 0x3E && n == 2) { R[0x3E] &= (uint8_t)~v; R[0x3F] &= (uint8_t)~(v >> 8); return; }   /* write 1 to clear */
    if (r == 0xB0 && n == 4) {                                       /* MAC OCP */
        uint32_t reg = (v >> 15) & 0xFFFF;
        if (v & 0x80000000u) ocp[reg & 0x7FFF] = (uint16_t)v;
        else put32(0xB0, (uint32_t)(reg == 0xE00E ? (ocp[reg & 0x7FFF] | 0x2000) : ocp[reg & 0x7FFF]));
        return;
    }
    for (int i = 0; i < n; i++) R[r + i] = (uint8_t)(v >> (8 * i));
    if (r == 0x37 && (v & 0x10)) {                                   /* reset: the rings start over (the OOB state stays) */
        R[0x37] = 0; rx_slot = tx_slot = 0;
    }
    if (r == 0x38 && (v & 0x40)) tx_poll();
    if (r == 0x60 && n == 4 && (v & 0x80000000u)) {                  /* PHY write, done at once */
        if (((v >> 16) & 0x1F) == 0 && (v & 0x1000)) R[0x6C] |= 0x02;   /* autonegotiation: link */
        put32(0x60, v & ~0x80000000u);
    }
}

uint8_t  fake_inb(uint16_t p) { return (uint8_t)rd(p, 1); }
uint16_t fake_inw(uint16_t p) { return (uint16_t)rd(p, 2); }
uint32_t fake_inl(uint16_t p) { return rd(p, 4); }
void fake_outb(uint16_t p, uint8_t v)  { wr(p, v, 1); }
void fake_outw(uint16_t p, uint16_t v) { wr(p, v, 2); }
void fake_outl(uint16_t p, uint32_t v) { wr(p, v, 4); }

/* ── the kernel, stubbed ── */
static uint16_t g_dev_id = 0x8168;
int pci_find(uint16_t v, uint16_t d, pci_dev_t* out) {
    if (v != 0x10EC || d != g_dev_id) return 0;
    memset(out, 0, sizeof(*out)); out->vendor = v; out->device = d; out->class_code = 2; out->irq_line = 11;
    return 1;
}
uintptr_t pci_bar(const pci_dev_t* d, int bar, int* is_io) { (void)d; if (bar == 0) { *is_io = 1; return 0xC000; } *is_io = 0; return 0; }
void pci_enable_from(const pci_dev_t* d, const char* f) { (void)d; (void)f; }
void irq_install(int irq, void (*h)(void)) { (void)irq; (void)h; }
void klog(const char* f, ...) { printf("    klog: %s", f); }
void net_irq_notify(void) {}
static int got_rx; static uint8_t got[2048]; static uint32_t got_len;
void net_rx(netdev_t* nd, const uint8_t* f, uint32_t len) { (void)nd; got_rx++; memcpy(got, f, len); got_len = len; }

/* ── the cases ── */
static int failed;
static void chip_power_on(uint32_t xid, int firmware_left) {
    memset(R, 0, sizeof(R)); memset(ocp, 0, sizeof(ocp));
    rx_slot = tx_slot = 0; sent = 0; got_rx = 0;
    const uint8_t mac[6] = { 0x00, 0xe0, 0x4c, 0x68, 0x11, 0x68 };
    memcpy(R, mac, 6);
    put32(0x40, xid << 20);
    model_g = xid >= 0x4C0;
    if (model_g) {
        R[0x6F] = 0xC0;                                              /* PLL on */
        if (firmware_left) { R[0xD3] = 0x80; put32(0xF0, 0x00080000u); R[0x6F] = 0x00; }
    }
}

static void run(const char* name, uint32_t xid, int firmware_left) {
    printf("%s\n", name);
    chip_power_on(xid, firmware_left);
    netdev_t* nd = r8169_probe();
    int ok = nd != NULL;
    if (!nd) { printf("    FAIL: no device\n"); failed++; return; }
    /* a DHCP DISCOVER out */
    uint8_t f[342]; memset(f, 0, sizeof(f)); memset(f, 0xFF, 6); memcpy(f + 6, nd->mac, 6); f[12] = 0x08;
    nd->send(nd, f, sizeof(f));
    if (sent != 1 || last_tx_len != sizeof(f) || memcmp(last_tx, f, sizeof(f))) { printf("    FAIL: the frame did not go out\n"); ok = 0; }
    /* the OFFER in: two frames, the second past the first slot */
    uint8_t in[300]; memset(in, 0x5A, sizeof(in)); memcpy(in, nd->mac, 6);
    int took = inject_rx(in, sizeof(in));
    in[20] = 0x77;
    took += inject_rx(in, sizeof(in));
    nd->poll(nd);
    if (took != 2 || got_rx != 2 || got_len != sizeof(in) || got[20] != 0x77) {
        printf("    FAIL: frames in: the chip took %d, the driver delivered %d\n", took, got_rx); ok = 0;
    }
    if (!nd->link_up(nd)) { printf("    FAIL: no link after autonegotiation\n"); ok = 0; }
    if (model_g && firmware_left)
        printf("    state after the driver: MCU %02x (OOB %s), MISC %08x (gate %s), PMCH %02x\n", R[0xD3], R[0xD3] & 0x80 ? "still on" : "off",
               get32(0xF0), get32(0xF0) & 0x00080000u ? "closed" : "open", R[0x6F]);
    /* many frames: the ring wraps (EOR) */
    int before = got_rx;
    for (int k = 0; k < 100; k++) { in[20] = (uint8_t)k; if (!inject_rx(in, sizeof(in))) break; if ((k & 7) == 7) nd->poll(nd); }
    nd->poll(nd);
    if (got_rx - before != 100) { printf("    FAIL: of 100 more frames %d came in\n", got_rx - before); ok = 0; }
    for (int k = 0; k < 40; k++) nd->send(nd, f, sizeof(f));
    if (sent != 41) { printf("    FAIL: of 40 more frames %d went out\n", sent - 1); ok = 0; }
    printf("  %s\n", ok ? "ok" : "FAIL");
    failed += !ok;
}

int main(void) {
    run("RTL8168h (XID 541), as a cold start leaves it", 0x541, 0);
    run("RTL8168h (XID 541), left by the firmware: out of band, receive gated, PLL off", 0x541, 1);
    run("RTL8168g (XID 4c0), left by the firmware", 0x4C0, 1);
    run("RTL8168c (XID 380), an older one", 0x380, 0);
    printf("%s: %d failed\n", failed ? "FAIL" : "ok", failed);
    return failed ? 1 : 0;
}
