#include "applesmc.h"
#include "acpi.h"
#include "io.h"
#include "kstring.h"
#include "timer.h"
#include "serial.h"

/*
 * The Apple SMC of Intel Macs (System Management Controller): the
 * temperatures, the fans, the ambient light sensor. Keys of four letters
 * ("TC0P", "F0Ac"), read through two I/O ports, a byte at a time, the way
 * Linux's applesmc does it. Only looked for when the firmware's ACPI
 * tables declare it (device APP0001): the ports are left alone on PCs.
 */

#define DATA_PORT 0x300
#define CMD_PORT  0x304
#define READ_CMD  0x10

#define ST_AWAITING_DATA 0x01
#define ST_IB_CLOSED     0x02
#define ST_BUSY          0x04

static int g_smc;

static int wait_status(uint8_t val, uint8_t mask) {
    uint32_t t0 = timer_ms();
    for (;;) {
        if ((inb(CMD_PORT) & mask) == val) return 0;
        if (timer_ms() - t0 > 50) return -1;
        for (int i = 0; i < 200; i++) __asm__ volatile("pause");
    }
}

static int send_byte(uint8_t b, uint16_t port) {
    if (wait_status(0, ST_IB_CLOSED)) return -1;
    if (wait_status(ST_BUSY, ST_BUSY)) return -1;
    outb(port, b);
    return 0;
}

int applesmc_read(const char* key, uint8_t* buf, int len) {
    if (!g_smc && key[0] != 'R') return -1;          /* (the probe reads "REV ") */
    if (wait_status(0, ST_IB_CLOSED)) return -1;
    outb(CMD_PORT, READ_CMD);
    for (int i = 0; i < 4; i++) if (send_byte((uint8_t)key[i], DATA_PORT)) return -1;
    if (send_byte((uint8_t)len, DATA_PORT)) return -1;
    for (int i = 0; i < len; i++) {
        if (wait_status(ST_AWAITING_DATA | ST_BUSY, ST_AWAITING_DATA | ST_BUSY)) return -1;
        buf[i] = inb(DATA_PORT);
    }
    for (int i = 0; i < 16 && (inb(CMD_PORT) & ST_AWAITING_DATA); i++) (void)inb(DATA_PORT);   /* (anything left over) */
    return 0;
}

int applesmc_present(void) { return g_smc; }

/* sp78: signed 8.8 fixed point, big endian */
int applesmc_temp(const char* key) {
    uint8_t b[2];
    if (applesmc_read(key, b, 2) != 0) return APPLESMC_NONE;
    int v = (int16_t)(b[0] << 8 | b[1]);
    int c = v / 256;
    if (c <= 0 || c > 125) return APPLESMC_NONE;     /* (an absent sensor reads 0 or nonsense) */
    return c;
}

int applesmc_fans(int* rpm, int max) {
    uint8_t n = 0;
    if (applesmc_read("FNum", &n, 1) != 0) return 0;
    int k = 0;
    for (int i = 0; i < n && k < max && i < 10; i++) {
        char key[5] = { 'F', (char)('0' + i), 'A', 'c', 0 };
        uint8_t b[2];
        if (applesmc_read(key, b, 2) == 0) rpm[k++] = (b[0] << 8 | b[1]) >> 2;   /* fpe2 */
    }
    return k;
}

void applesmc_init(void) {
    if (!acpi_has_device("APP0001")) return;
    uint8_t rev[6];
    if (applesmc_read("REV ", rev, 6) != 0) { klog("applesmc: declared, but it does not answer\n"); return; }
    g_smc = 1;
    klog("applesmc: Apple SMC, firmware %x.%x%x\n", rev[0], rev[1], rev[2]);
}
