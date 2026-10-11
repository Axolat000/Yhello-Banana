/*
 * ACPI power-off: the PM1 control registers come from the FADT and the S5
 * sleep type from the \_S5_ package in the DSDT - what VMware, real PCs and
 * every other machine expect, instead of guessing well-known ports.
 */
#include "task.h"
#include "acpi.h"
#include "smp.h"
#include "io.h"
#include "kstring.h"
#include "serial.h"

static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t rd64(const uint8_t* p) { uint64_t v; memcpy(&v, p, 8); return v; }

static void delay_us(uint32_t us) { while (us--) inb(0x80); }   /* ~1 us each */

/* one AML integer (ZeroOp, OneOp, BytePrefix, WordPrefix): its length in bytes, 0 if none */
static int aml_int(const uint8_t* p, const uint8_t* end, uint32_t* v) {
    if (p >= end) return 0;
    switch (p[0]) {
    case 0x00: *v = 0; return 1;
    case 0x01: *v = 1; return 1;
    case 0x0A: if (p + 2 > end) return 0; *v = p[1]; return 2;
    case 0x0B: if (p + 3 > end) return 0; *v = p[1] | (p[2] << 8); return 3;
    default:   return 0;
    }
}

/* SLP_TYPa / SLP_TYPb from Name(\_S5_, Package(){a, b, ...}) */
static int find_s5(const uint8_t* aml, uint32_t len, uint32_t* a, uint32_t* b) {
    const uint8_t* end = aml + len;
    for (const uint8_t* p = aml; p + 4 < end; p++) {
        if (memcmp(p, "_S5_", 4) != 0) continue;
        /* NameOp before it (maybe with a '\' root prefix), PackageOp after */
        if (!(p[-1] == 0x08 || (p[-1] == 0x5C && p[-2] == 0x08))) continue;
        const uint8_t* q = p + 4;
        if (q >= end || *q != 0x12) continue;
        q++;
        if (q >= end) return 0;
        q += 1 + (*q >> 6);                    /* PkgLength: 1 to 4 bytes */
        q++;                                   /* NumElements */
        int n = aml_int(q, end, a);
        if (!n) return 0;
        q += n;
        if (!aml_int(q, end, b)) *b = 0;
        return 1;
    }
    return 0;
}

void acpi_s5_poweroff(void) {
    const uint8_t* f = acpi_find_table("FACP");
    if (!f) { klog("acpi: no FADT\n"); return; }
    uint32_t flen = rd32(f + 4);

    uint64_t dsdt_a = rd32(f + 40);
    if (flen >= 148 && rd64(f + 140)) dsdt_a = rd64(f + 140);
    const uint8_t* dsdt = acpi_table_at(dsdt_a, "DSDT");
    uint32_t typa, typb;
    if (!dsdt || !find_s5(dsdt + 36, rd32(dsdt + 4) - 36, &typa, &typb)) {
        klog("acpi: no _S5_ in the DSDT\n");
        return;
    }

    uint32_t smi_cmd = rd32(f + 48);
    uint8_t  enable  = f[52];
    uint32_t pm1a = rd32(f + 64), pm1b = rd32(f + 68);
    /* the extended (GAS) addresses, when only they are given in I/O space */
    if (!pm1a && flen >= 184 && f[172] == 1) pm1a = (uint32_t)rd64(f + 176);
    if (!pm1b && flen >= 196 && f[184] == 1) pm1b = (uint32_t)rd64(f + 188);
    if (!pm1a || pm1a > 0xFFFF) { klog("acpi: no PM1a control port\n"); return; }

    /* still in legacy mode (VMware's BIOS boots so): ask the firmware to
     * switch ACPI on, and wait for SCI_EN */
    if (!(inw(pm1a) & 1) && smi_cmd && smi_cmd <= 0xFFFF && enable) {
        outb((uint16_t)smi_cmd, enable);
        for (int i = 0; i < 300 && !(inw(pm1a) & 1); i++) delay_us(10000);
    }

    klog("acpi: S5 (SLP_TYP %u) on port %x\n", typa, pm1a);
    delay_us(20000);                           /* (the log out on the serial port) */
    __asm__ volatile("cli");
    uint16_t v = inw(pm1a) & ~(uint16_t)(7 << 10);
    outw((uint16_t)pm1a, v | (uint16_t)((typa & 7) << 10) | (1 << 13));
    if (pm1b && pm1b <= 0xFFFF) {
        v = inw(pm1b) & ~(uint16_t)(7 << 10);
        outw((uint16_t)pm1b, v | (uint16_t)((typb & 7) << 10) | (1 << 13));
    }
    delay_us(500000);                          /* (the machine goes off here) */
    klog("acpi: S5 did not power off\n");
}

/* ── the power button ───────────────────────────────────────────────
 * The fixed-hardware power button (FADT flag PWR_BUTTON clear: QEMU,
 * VirtualBox's "ACPI Shutdown", most desktops): its status bit in the PM1
 * event block. ACPI mode on (as every ACPI OS), every event but that one
 * off (nobody here would handle them: the SCI must not fire for them), and
 * a task looking at the status ten times a second - a press shuts down
 * like Start > Shut down (the files saved first). A control-method button
 * (laptops: a GPE and AML) would need an AML interpreter. */

#define PM1_PWRBTN (1u << 8)

static uint16_t g_evt_a, g_evt_b;
static uint8_t  g_evt_len;

extern void shell_power(int restart);

static uint32_t gas_io(const uint8_t* f, uint32_t flen, uint32_t legacy_off, uint32_t gas_off) {
    uint32_t v = rd32(f + legacy_off);
    if (!v && flen >= gas_off + 12 && f[gas_off] == 1) v = (uint32_t)rd64(f + gas_off + 4);
    return v <= 0xFFFF ? v : 0;
}

static void button_task(void) {
    task_set_background();
    for (;;) {
        task_sleep_ms(100);
        uint16_t st = inw(g_evt_a);
        if (g_evt_b) st |= inw(g_evt_b);
        if (!(st & PM1_PWRBTN)) continue;
        outw(g_evt_a, PM1_PWRBTN);                 /* (write 1 to clear) */
        if (g_evt_b) outw(g_evt_b, PM1_PWRBTN);
        klog("acpi: power button - shutting down\n");
        shell_power(0);
    }
}

void acpi_power_button_init(void) {
    const uint8_t* f = acpi_find_table("FACP");
    if (!f) return;
    uint32_t flen = rd32(f + 4);
    if (flen < 116) return;
    uint32_t flags = rd32(f + 112);
    if (flags & (1u << 4)) { klog("acpi: the power button is a control-method device (not handled)\n"); return; }
    g_evt_a = (uint16_t)gas_io(f, flen, 56, 148);
    g_evt_b = (uint16_t)gas_io(f, flen, 60, 160);
    g_evt_len = f[88];
    uint32_t cnt_a = gas_io(f, flen, 64, 172);
    if (!g_evt_a || g_evt_len < 4 || !cnt_a) { klog("acpi: no PM1 event block\n"); return; }
    /* ACPI mode */
    uint32_t smi_cmd = rd32(f + 48);
    uint8_t enable = f[52];
    if (!(inw((uint16_t)cnt_a) & 1) && smi_cmd && smi_cmd <= 0xFFFF && enable) {
        outb((uint16_t)smi_cmd, enable);
        for (int i = 0; i < 300 && !(inw((uint16_t)cnt_a) & 1); i++) delay_us(10000);
    }
    /* the general-purpose events off: only the button may raise the SCI */
    uint32_t gpe0 = rd32(f + 80), gpe1 = rd32(f + 84);
    uint8_t gpe0_len = f[92], gpe1_len = f[93];
    if (gpe0 && gpe0 <= 0xFFFF) for (int i = 0; i < gpe0_len / 2; i++) outb((uint16_t)(gpe0 + gpe0_len / 2 + i), 0);
    if (gpe1 && gpe1 <= 0xFFFF) for (int i = 0; i < gpe1_len / 2; i++) outb((uint16_t)(gpe1 + gpe1_len / 2 + i), 0);
    uint16_t en = (uint16_t)(g_evt_len / 2);
    outw(g_evt_a, 0xFFFF);                         /* old events cleared */
    outw((uint16_t)(g_evt_a + en), PM1_PWRBTN);    /* the button only */
    if (g_evt_b) { outw(g_evt_b, 0xFFFF); outw((uint16_t)(g_evt_b + en), PM1_PWRBTN); }
    if (task_create("acpi", button_task) < 0) return;
    klog("acpi: power button on port %x\n", g_evt_a);
}

/* ── devices the firmware declares ────────────────────────────────── */

static int hexv(char c) { return c >= '0' && c <= '9' ? c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (c | 32) - 'a' + 10 : 0; }

static int table_has(const uint8_t* t, const uint8_t* pat, int n) {
    if (!t) return 0;
    uint32_t len = rd32(t + 4);
    if (len > (16u << 20)) return 0;
    for (uint32_t i = 36; i + (uint32_t)n <= len; i++)
        if (!memcmp(t + i, pat, (uint32_t)n)) return 1;
    return 0;
}

int acpi_has_device(const char* hid) {
    if (strlen(hid) != 7) return 0;
    /* _HID as an EISA ID (DWordConst of the compressed letters + product) or as a string */
    uint16_t w = (uint16_t)(((hid[0] - 0x40) & 31) << 10 | ((hid[1] - 0x40) & 31) << 5 | ((hid[2] - 0x40) & 31));
    uint8_t eisa[5] = { 0x0C, (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(hexv(hid[3]) << 4 | hexv(hid[4])), (uint8_t)(hexv(hid[5]) << 4 | hexv(hid[6])) };
    const uint8_t* f = acpi_find_table("FACP");
    const uint8_t* tabs[9];
    int nt = 0;
    if (f) {
        uint32_t flen = rd32(f + 4);
        uint64_t a = rd32(f + 40);
        if (flen >= 148 && rd64(f + 140)) a = rd64(f + 140);
        tabs[nt++] = acpi_table_at(a, "DSDT");
    }
    for (int i = 0; i < 8; i++) tabs[nt++] = acpi_find_table_n("SSDT", i);
    for (int i = 0; i < nt; i++)
        if (table_has(tabs[i], eisa, 5) || table_has(tabs[i], (const uint8_t*)hid, 7)) return 1;
    return 0;
}
