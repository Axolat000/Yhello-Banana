#include "sensors.h"
#include "sysinfo.h"
#include "pci.h"
#include "kstring.h"
#include "applesmc.h"

/*
 * The processor's temperature.
 *
 * Intel (Core 2010 on, Atom, Xeon): the digital thermal sensor - how far
 * below TjMax the core (IA32_THERM_STATUS) and the package
 * (IA32_PACKAGE_THERM_STATUS) are, TjMax from MSR_TEMPERATURE_TARGET.
 * Only read when CPUID says the sensor is there and no hypervisor is
 * (a virtual machine would fault on these MSRs).
 * AMD Zen (family 17h, 19h, 1Ah): Tctl through the SMN registers, reached
 * from the host bridge's configuration space; AMD family 10h-16h: the
 * northbridge's "reported temperature" register (device 18h, function 3).
 */

enum { S_NONE, S_INTEL, S_ZEN, S_K10 };

static int g_kind = -1;
static int g_tjmax = 100;
static int g_pkg;                       /* Intel: a package sensor */

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return (uint64_t)hi << 32 | lo;
}

static void detect(void) {
    g_kind = S_NONE;
    const sysinfo_t* si = sysinfo_get();
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    uint32_t max = a;
    cpuid(1, &a, &b, &c, &d);
    if (c & (1u << 31)) return;         /* a hypervisor: no real sensors to read */
    uint32_t fam = (a >> 8) & 0xF, model = (a >> 4) & 0xF;
    if (fam == 0xF) fam += (a >> 20) & 0xFF;
    if (fam == 0x6 || fam == 0xF) model |= ((a >> 16) & 0xF) << 4;

    if (strcmp(si->cpu_vendor, "GenuineIntel") == 0) {
        if (max < 6) return;
        cpuid(6, &a, &b, &c, &d);
        if (!(a & 1)) return;           /* no digital thermal sensor */
        g_pkg = (a >> 6) & 1;
        /* TjMax: MSR_TEMPERATURE_TARGET from Nehalem on, 100 C before */
        if (fam == 6 && model >= 0x1A) {
            uint32_t t = (uint32_t)(rdmsr(0x1A2) >> 16) & 0xFF;
            if (t >= 70 && t <= 120) g_tjmax = (int)t;
        }
        g_kind = S_INTEL;
    } else if (strcmp(si->cpu_vendor, "AuthenticAMD") == 0 || strcmp(si->cpu_vendor, "HygonGenuine") == 0) {
        if (fam >= 0x17) {
            if (pci_read16(0, 0, 0, 0) != 0x1022 && pci_read16(0, 0, 0, 0) != 0x1D94) return;
            g_kind = S_ZEN;
        } else if (fam >= 0x10) {
            if (pci_read16(0, 0x18, 3, 0) != 0x1022) return;
            g_kind = S_K10;
        }
    }
}

int sensors_cpu_temp(void) {
    if (g_kind < 0) detect();
    switch (g_kind) {
    case S_INTEL: {
        uint64_t st = rdmsr(g_pkg ? 0x1B1 : 0x19C);
        if (!g_pkg && !(st & (1u << 31))) return SENSOR_NONE;     /* reading not valid */
        return g_tjmax - (int)((st >> 16) & 0x7F);
    }
    case S_ZEN: {
        pci_write32(0, 0, 0, 0x60, 0x00059800);
        uint32_t v = pci_read32(0, 0, 0, 0x64);
        int t = (int)(((v >> 21) & 0x7FF) * 125 / 1000);
        if (v & (1u << 19)) t -= 49;    /* the -49..206 C range */
        return t;
    }
    case S_K10: {
        uint32_t v = pci_read32(0, 0x18, 3, 0xA4);
        return (int)(((v >> 21) & 0x7FF) * 125 / 1000);
    }
    }
    return SENSOR_NONE;
}

int sensors_cpu_tjmax(void) {
    if (g_kind < 0) detect();
    return g_kind == S_INTEL ? g_tjmax : 0;
}

const char* sensors_cpu_source(void) {
    if (g_kind < 0) detect();
    switch (g_kind) {
    case S_INTEL: return g_pkg ? "Intel digital thermal sensor (package)" : "Intel digital thermal sensor (core)";
    case S_ZEN:   return "AMD Tctl (SMN)";
    case S_K10:   return "AMD northbridge sensor";
    }
    return NULL;
}
