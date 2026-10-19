/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "vmtools.h"
#include "virtio_serial.h"
#include "qga.h"
#include "io.h"
#include "pci.h"
#include "usb.h"
#include "kheap.h"
#include "kstring.h"
#include "task.h"
#include "timer.h"
#include "rtc.h"
#include "serial.h"
#include "config.h"
#include "settings.h"
#include "terminal.h"
#include "../net/net.h"

extern void shell_power(int restart);

static char g_hv[16];                   /* CPUID's 12-character vendor */
static const char* g_hv_name = "";
static int  g_off;                      /* vmtools=off */

/* ── which hypervisor ───────────────────────────────────────────────── */

static void cpuid(uint32_t leaf, uint32_t* a, uint32_t* b, uint32_t* c, uint32_t* d) {
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static void detect(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return;                      /* no hypervisor bit: real hardware */
    cpuid(0x40000000u, &a, &b, &c, &d);
    memcpy(g_hv, &b, 4);
    memcpy(g_hv + 4, &c, 4);
    memcpy(g_hv + 8, &d, 4);
    g_hv[12] = 0;
    static const struct { const char* sig; const char* name; } T[] = {
        { "KVMKVMKVM", "KVM" },               { "TCGTCGTCGTCG", "QEMU (TCG)" },
        { "VMwareVMware", "VMware" },          { "VBoxVBoxVBox", "VirtualBox" },
        { "Microsoft Hv", "Hyper-V" },         { "XenVMMXenVMM", "Xen" },
        { " lrpepyh  vr", "Parallels" },       { "bhyve bhyve ", "bhyve" },
        { "ACRNACRNACRN", "ACRN" },            { "QNXQVMBSQG", "QNX" },
    };
    g_hv_name = "a hypervisor";
    for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++)
        if (strncmp(g_hv, T[i].sig, strlen(T[i].sig)) == 0) { g_hv_name = T[i].name; break; }
    /* VirtualBox with its KVM paravirtualization says "KVM": its VMMDev tells */
    pci_dev_t pd;
    if (pci_find(0x80EE, 0xCAFE, &pd)) g_hv_name = "VirtualBox";
}

const char* vmtools_hypervisor(void) { return g_hv_name; }

/* ── VMware's backdoor (also QEMU's vmport) ─────────────────────────── */

#define BD_MAGIC 0x564D5868u            /* "VMXh" */
#define BD_PORT  0x5658u
#define BD_GETVERSION 10
#define BD_GETTIME    23
#define BD_MOUSE_DATA 39
#define BD_MOUSE_STAT 40
#define BD_MOUSE_CMD  41
#define BD_MESSAGE    30

typedef struct { uint32_t ax, bx, cx, dx, si, di; } bd_t;

static int      g_bd;                   /* the backdoor answers */
static uint32_t g_bd_version;

static void bd_io(bd_t* r) {
    __asm__ volatile("inl %%dx, %%eax"
                     : "+a"(r->ax), "+b"(r->bx), "+c"(r->cx), "+d"(r->dx), "+S"(r->si), "+D"(r->di)
                     :: "memory");
}

static void bd(uint32_t cmd, uint32_t arg, bd_t* r) {
    r->ax = BD_MAGIC;
    r->bx = arg;
    r->cx = cmd;
    r->dx = BD_PORT;
    r->si = r->di = 0;
    bd_io(r);
}

static void bd_detect(void) {
    /* only where it can be: an unknown port read on other hypervisors is wasted at best */
    if (strcmp(g_hv_name, "VMware") && strcmp(g_hv_name, "KVM") && strcmp(g_hv_name, "QEMU (TCG)")) return;
    bd_t r;
    bd(BD_GETVERSION, ~BD_MAGIC, &r);
    if (r.bx != BD_MAGIC || r.ax == 0xFFFFFFFFu) return;
    g_bd = 1;
    g_bd_version = r.ax;
}

/* ── VMMouse: the absolute pointer through the backdoor ─────────────── */

#define VMM_CMD_ENABLE   0x45414552u    /* "READ_ID": also turns it on */
#define VMM_CMD_DISABLE  0x000000F5u
#define VMM_CMD_RELATIVE 0x4c455252u
#define VMM_RELATIVE_PACKET 0x00010000u
static int g_pointer_captured;
#define VMM_CMD_ABSOLUTE 0x53424152u
#define VMM_VERSION_ID   0x3442554Au
#define VMM_ERROR        0xFFFF0000u
#define VMM_LEFT   0x20u
#define VMM_RIGHT  0x10u
#define VMM_MIDDLE 0x08u

static int      g_vmmouse;              /* 1: on, absolute */
static uint32_t g_vmm_events;

static int vmmouse_enable(void) {
    bd_t r;
    bd(BD_MOUSE_CMD, VMM_CMD_ENABLE, &r);
    bd(BD_MOUSE_STAT, 0, &r);
    if ((r.ax & VMM_ERROR) == VMM_ERROR || (r.ax & 0xFFFFu) == 0) return -1;
    bd(BD_MOUSE_DATA, 1, &r);
    if (r.ax != VMM_VERSION_ID) return -1;
    bd(BD_MOUSE_CMD, g_pointer_captured?VMM_CMD_RELATIVE:VMM_CMD_ABSOLUTE, &r);
    return 0;
}

static void vmmouse_poll(void) {
    bd_t r;
    for (int guard = 0; guard < 64; guard++) {
        bd(BD_MOUSE_STAT, 0, &r);
        if ((r.ax & VMM_ERROR) == VMM_ERROR) {           /* lost: turned back on (or given up) */
            if (vmmouse_enable() != 0) { g_vmmouse = 0; mouse_ps2_mute(0); }
            return;
        }
        if ((r.ax & 0xFFFFu) < 4) return;
        bd(BD_MOUSE_DATA, 4, &r);
        uint32_t st = r.ax;
        int buttons = ((st & VMM_LEFT) ? 1 : 0) | ((st & VMM_RIGHT) ? 2 : 0) | ((st & VMM_MIDDLE) ? 4 : 0);
        int z = (int32_t)r.dx;
        if(st & VMM_RELATIVE_PACKET)mouse_inject((int32_t)r.bx,(int32_t)r.cx,buttons);
        else mouse_inject_abs((int)(r.bx & 0xFFFFu), (int)(r.cx & 0xFFFFu), buttons);
        if (z) mouse_inject_wheel(z);
        g_vmm_events++;
    }
}

/* ── VMware's tools channel: RPCI (guest -> host) and TCLO (host -> guest) ── */

#define MSG_OPEN      0
#define MSG_SENDSIZE  1
#define MSG_SENDDATA  2
#define MSG_RECVSIZE  3
#define MSG_RECVDATA  4
#define MSG_RECVSTAT  5
#define MSG_CLOSE     6
#define ST_SUCCESS    0x0001u
#define ST_DORECV     0x0002u
#define PROTO_RPCI    0x49435052u       /* "RPCI" */
#define PROTO_TCLO    0x4F4C4354u       /* "TCLO" */
#define FLAG_COOKIE   0x80000000u

typedef struct { int open; uint16_t id; uint32_t c1, c2; } chan_t;

static void msg(chan_t* ch, uint32_t sub, uint32_t arg, bd_t* r) {
    r->ax = BD_MAGIC;
    r->bx = arg;
    r->cx = (sub << 16) | BD_MESSAGE;
    r->dx = ((uint32_t)ch->id << 16) | BD_PORT;
    r->si = ch->c1;
    r->di = ch->c2;
    bd_io(r);
}

static int chan_open(chan_t* ch, uint32_t proto) {
    bd_t r = { BD_MAGIC, proto | FLAG_COOKIE, (MSG_OPEN << 16) | BD_MESSAGE, BD_PORT, 0, 0 };
    bd_io(&r);
    if (!((r.cx >> 16) & ST_SUCCESS)) return -1;
    ch->id = (uint16_t)(r.dx >> 16);
    ch->c1 = r.si;
    ch->c2 = r.di;
    ch->open = 1;
    return 0;
}

static void chan_close(chan_t* ch) {
    if (!ch->open) return;
    bd_t r;
    msg(ch, MSG_CLOSE, 0, &r);
    ch->open = 0;
}

static int chan_send(chan_t* ch, const char* s, uint32_t len) {
    bd_t r;
    msg(ch, MSG_SENDSIZE, len, &r);
    if (!((r.cx >> 16) & ST_SUCCESS)) return -1;
    for (uint32_t i = 0; i < len; i += 4) {
        uint32_t w = 0;
        for (uint32_t k = 0; k < 4 && i + k < len; k++) w |= (uint32_t)(uint8_t)s[i + k] << (8 * k);
        msg(ch, MSG_SENDDATA, w, &r);
        if (!((r.cx >> 16) & ST_SUCCESS)) return -1;
    }
    return 0;
}

/* a message from the host: its length (0: none waiting), or -1 */
static int chan_recv(chan_t* ch, char* out, uint32_t cap) {
    bd_t r;
    msg(ch, MSG_RECVSIZE, 0, &r);
    uint32_t st = r.cx >> 16;
    if (!(st & ST_SUCCESS)) return -1;
    if (!(st & ST_DORECV)) return 0;
    uint32_t len = r.bx;
    if ((r.dx >> 16) != MSG_SENDSIZE || len >= cap) return -1;
    for (uint32_t i = 0; i < len; i += 4) {
        msg(ch, MSG_RECVDATA, ST_SUCCESS, &r);
        if (!((r.cx >> 16) & ST_SUCCESS) || (r.dx >> 16) != MSG_SENDDATA) return -1;
        for (uint32_t k = 0; k < 4 && i + k < len; k++) out[i + k] = (char)(r.bx >> (8 * k));
    }
    out[len] = 0;
    msg(ch, MSG_RECVSTAT, ST_SUCCESS, &r);
    return (int)len;
}

/* one RPCI command; 1 if the host answered "1 ..." (success) */
static int rpci(const char* cmd, char* reply, uint32_t cap) {
    chan_t ch = { 0, 0, 0, 0 };
    if (chan_open(&ch, PROTO_RPCI) != 0) return 0;
    char tmp[128];
    if (!reply) { reply = tmp; cap = sizeof(tmp); }
    int ok = chan_send(&ch, cmd, (uint32_t)strlen(cmd)) == 0 && chan_recv(&ch, reply, cap) >= 1 && reply[0] == '1';
    chan_close(&ch);
    return ok;
}

static int      g_tclo;                 /* the TCLO loop runs */
static uint32_t g_tclo_cmds;
static char     g_tclo_last[40];

static void rpci_guest_info(void) {
    char b[96];
    netif_t* nif = net_if();
    rpci("SetGuestInfo  4 Banana OS 0.6", NULL, 0);           /* (the full OS name) */
    rpci("SetGuestInfo  5 other-64", NULL, 0);
    if (nif && nif->configured) {
        ip4_t ip = nif->ip;
        ksnprintf(b, sizeof(b), "info-set guestinfo.ip %u.%u.%u.%u", ip >> 24, (ip >> 16) & 255u, (ip >> 8) & 255u, ip & 255u);
        rpci(b, NULL, 0);
    }
}

static void tclo_task(void) {
    task_set_background();
    chan_t ch = { 0, 0, 0, 0 };
    char* in = (char*)kmalloc(1024);
    char reply[64] = "";
    int power = -1;                     /* after the reply goes out: 0 off, 1 restart */
    uint32_t ip_seen = 0, info_at = 0;
    if (!in) return;
    rpci("tools.set.version 2147483647", NULL, 0);           /* (tools not managed by the host) */
    for (;;) {
        if (!ch.open && chan_open(&ch, PROTO_TCLO) != 0) { task_sleep_ms(5000); continue; }
        if (chan_send(&ch, reply, (uint32_t)strlen(reply)) != 0) { chan_close(&ch); task_sleep_ms(1000); continue; }
        reply[0] = 0;
        if (power >= 0) {
            chan_close(&ch);
            klog("vmtools: the host asked for a %s\n", power ? "restart" : "shut down");
            shell_power(power);
            power = -1;
        }
        int n = chan_recv(&ch, in, 1024);
        if (n < 0) { chan_close(&ch); task_sleep_ms(1000); continue; }
        if (n == 0) {
            /* idle: the address now and then, then wait */
            netif_t* nif = net_if();
            uint32_t ip = nif && nif->configured ? nif->ip : 0;
            if (ip != ip_seen || timer_ms() - info_at > 60000u) { rpci_guest_info(); ip_seen = ip; info_at = timer_ms(); }
            task_sleep_ms(1000);
            continue;
        }
        g_tclo_cmds++;
        kstrlcpy(g_tclo_last, in, sizeof(g_tclo_last));
        if (strcmp(in, "reset") == 0) kstrlcpy(reply, "OK ATR toolbox", sizeof(reply));
        else if (strcmp(in, "ping") == 0) kstrlcpy(reply, "OK ", sizeof(reply));
        else if (strcmp(in, "Capabilities_Register") == 0) {
            rpci("tools.capability.statechange ", NULL, 0);
            rpci("tools.capability.softpowerop_retry ", NULL, 0);
            kstrlcpy(reply, "OK ", sizeof(reply));
        } else if (strcmp(in, "OS_Halt") == 0 || strcmp(in, "OS_PowerOff") == 0) {
            kstrlcpy(reply, "OK ", sizeof(reply));
            power = 0;
        } else if (strcmp(in, "OS_Reboot") == 0) {
            kstrlcpy(reply, "OK ", sizeof(reply));
            power = 1;
        } else if (strncmp(in, "OS_", 3) == 0 || strncmp(in, "Set_Option", 10) == 0 ||
                   strncmp(in, "vmx.capability", 14) == 0) {
            kstrlcpy(reply, "OK ", sizeof(reply));
        } else {
            kstrlcpy(reply, "ERROR Unknown command", sizeof(reply));
        }
    }
}

/* ── VirtualBox's VMMDev (Guest Additions' device, 80ee:cafe) ───────── */

#define VMMDEV_VERSION     0x00010001u
#define VMMDEV_IF_VERSION  0x00010004u
#define REQ_GET_MOUSE      1
#define REQ_SET_MOUSE      2
#define REQ_HOST_TIME      10
#define REQ_GUEST_INFO     50
#define REQ_GUEST_CAPS     55
#define MOUSE_GUEST_ABS    0x01u
#define MOUSE_HOST_ABS     0x02u
#define MOUSE_NEW_PROTOCOL 0x10u

typedef struct __attribute__((packed)) { uint32_t size, version, type; int32_t rc; uint32_t r1, r2; } vmm_hdr_t;
typedef struct __attribute__((packed)) { vmm_hdr_t h; uint32_t features; int32_t x, y; } vmm_mouse_t;
typedef struct __attribute__((packed)) { vmm_hdr_t h; uint64_t time_ms; } vmm_time_t;
typedef struct __attribute__((packed)) { vmm_hdr_t h; uint32_t if_version, os_type; } vmm_info_t;
typedef struct __attribute__((packed)) { vmm_hdr_t h; uint32_t caps; } vmm_caps_t;

static uint16_t g_vbox_port;
static int      g_vbox, g_vbox_abs;
static uint8_t* g_vreq;                 /* one request at a time (below 4 GiB: the heap) */
static uint32_t g_vbox_last_ms;

static int vbox_req(uint32_t type, uint32_t size) {
    vmm_hdr_t* h = (vmm_hdr_t*)g_vreq;
    h->size = size;
    h->version = VMMDEV_VERSION;
    h->type = type;
    h->rc = -1;
    h->r1 = h->r2 = 0;
    __asm__ volatile("" ::: "memory");
    outl(g_vbox_port, (uint32_t)(uintptr_t)g_vreq);
    __asm__ volatile("" ::: "memory");
    return h->rc >= 0 ? 0 : -1;
}

static void vbox_init(void) {
    pci_dev_t d;
    if (!pci_find(0x80EE, 0xCAFE, &d)) return;
    int io = 0;
    uintptr_t port = pci_bar(&d, 0, &io);
    if (!io || !port) return;
    pci_enable(&d);
    g_vreq = (uint8_t*)kmalloc(64);
    if (!g_vreq || (uint64_t)(uintptr_t)g_vreq >= 0xFFFFFFFFull) return;
    g_vbox_port = (uint16_t)port;
    memset(g_vreq, 0, 64);
    vmm_info_t* gi = (vmm_info_t*)g_vreq;
    gi->if_version = VMMDEV_IF_VERSION;
    gi->os_type = 0;                                       /* (VBOXOSTYPE_Unknown) */
    if (vbox_req(REQ_GUEST_INFO, sizeof(vmm_info_t)) != 0) { klog("vmtools: VirtualBox's VMMDev refused the guest info\n"); return; }
    vmm_caps_t* gc = (vmm_caps_t*)g_vreq;
    gc->caps = 0;
    vbox_req(REQ_GUEST_CAPS, sizeof(vmm_caps_t));
    vmm_mouse_t* m = (vmm_mouse_t*)g_vreq;
    m->features = MOUSE_GUEST_ABS | MOUSE_NEW_PROTOCOL;
    m->x = m->y = 0;
    vbox_req(REQ_SET_MOUSE, sizeof(vmm_mouse_t));
    g_vbox = 1;
}

static void vbox_poll(void) {
    uint32_t now = timer_ms();
    if (now - g_vbox_last_ms < 8u) return;
    g_vbox_last_ms = now;
    vmm_mouse_t* m = (vmm_mouse_t*)g_vreq;
    m->features = 0;
    m->x = m->y = 0;
    if (vbox_req(REQ_GET_MOUSE, sizeof(vmm_mouse_t)) != 0) return;
    g_vbox_abs = (m->features & MOUSE_HOST_ABS) ? 1 : 0;
    if (g_vbox_abs && !g_pointer_captured) mouse_set_abs(m->x, m->y);
}

/* ── everything together ────────────────────────────────────────────── */

void vmtools_capture_pointer(int capture) {
    capture=!!capture;if(capture==g_pointer_captured)return;
    g_pointer_captured=capture;
    if(g_vmmouse) { bd_t r;bd(BD_MOUSE_CMD,capture?VMM_CMD_RELATIVE:VMM_CMD_ABSOLUTE,&r); }
    if(g_vbox) {
        vmm_mouse_t* m=(vmm_mouse_t*)g_vreq;
        m->features=MOUSE_NEW_PROTOCOL|(capture?0:MOUSE_GUEST_ABS);m->x=m->y=0;
        vbox_req(REQ_SET_MOUSE,sizeof(*m));g_vbox_abs=0;
    }
}

void vmtools_s3_resume(void) {
    g_pointer_captured=0;
    if(g_bd && g_vmmouse) {
        if(vmmouse_enable())g_vmmouse=0;
        mouse_ps2_mute(g_vmmouse);
    }
}

void vmtools_poll_pointer(void) {
    if (g_vmmouse) vmmouse_poll();
    else if (g_vbox) vbox_poll();
}

uint32_t vmtools_host_time(void) {
    if (g_bd) {
        bd_t r;
        bd(BD_GETTIME, 0, &r);
        if (r.ax != 0xFFFFFFFFu && r.ax > 946684800u) return r.ax;
    }
    if (g_vbox) {
        vmm_time_t* t = (vmm_time_t*)g_vreq;
        t->time_ms = 0;
        if (vbox_req(REQ_HOST_TIME, sizeof(vmm_time_t)) == 0 && t->time_ms) return (uint32_t)(t->time_ms / 1000u);
    }
    return 0;
}

void vmtools_init(void) {
    detect();
    char v[8];
    if (cfg_get(CFG_SETTINGS, "vmtools", v, sizeof(v)) && strcmp(v, "off") == 0) {
        g_off = 1;
        if (g_hv_name[0]) klog("vmtools: %s - guest tools turned off (vmtools on turns them back on)\n", g_hv_name);
        return;
    }
    if (!g_hv_name[0]) return;
    klog("vmtools: running on %s (%s)\n", g_hv_name, g_hv);
    bd_detect();
    if (g_bd) {
        if (vmmouse_enable() == 0) {
            g_vmmouse = 1;
            mouse_ps2_mute(1);
        }
        klog("vmtools: VMware backdoor, version %u%s\n", g_bd_version, g_vmmouse ? " - VMMouse: absolute pointer" : "");
        if (strcmp(g_hv_name, "VMware") == 0) {           /* (QEMU's vmport has no message channel) */
            g_tclo = 1;
            task_create("vmtools", tclo_task);
        }
    }
    vbox_init();
    if (g_vbox) klog("vmtools: VirtualBox VMMDev on port %x - mouse integration, host clock\n", g_vbox_port);
    if (vser_init()) qga_init();
}

/* ── the shell command ──────────────────────────────────────────────── */

static void line(const char* s) { terminal_writeln(s); }

void vmtools_command(const char* args) {
    while (*args == ' ') args++;
    if (strcmp(args, "off") == 0 || strcmp(args, "on") == 0) {
        cfg_set(CFG_SETTINGS, "vmtools", args[1] == 'f' ? "off" : NULL, "# Banana OS settings (the Settings app writes this)\n");
        cfg_persist();
        terminal_writeln(args[1] == 'f' ? "vmtools: off from the next start" : "vmtools: on from the next start");
        return;
    }
    if (strcmp(args, "time") == 0) {
        uint32_t h = vmtools_host_time(), g = rtc_now_utc();
        char b[96];
        if (!h) { terminal_writeln("vmtools: the hypervisor gives no clock here (the guest agent's guest-set-time sets it)"); return; }
        ksnprintf(b, sizeof(b), "host: %u, this system: %u (%d s apart)", h, g, (int)(g - h));
        terminal_writeln(b);
        return;
    }
    if (args[0]) { terminal_writeln("usage: vmtools [on|off|time]"); return; }
    char b[160];
    ksnprintf(b, sizeof(b), "Hypervisor:   %s%s%s%s", g_hv_name[0] ? g_hv_name : "none (real hardware)",
              g_hv[0] ? " (CPUID \"" : "", g_hv, g_hv[0] ? "\")" : "");
    line(b);
    if (g_off) { line("Guest tools:  off (vmtools on, then restart)"); return; }
    ksnprintf(b, sizeof(b), "VMware port:  %s", g_bd ? "yes" : "no");
    if (g_bd) ksnprintf(b + strlen(b), sizeof(b) - strlen(b), " (version %u)", g_bd_version);
    line(b);
    ksnprintf(b, sizeof(b), "VMMouse:      %s", g_vmmouse ? "on - the pointer follows the host's" : "off");
    if (g_vmmouse) ksnprintf(b + strlen(b), sizeof(b) - strlen(b), " (%u events)", g_vmm_events);
    line(b);
    if (g_tclo) {
        ksnprintf(b, sizeof(b), "VMware tools: channel open, %u host commands%s%s", g_tclo_cmds, g_tclo_last[0] ? ", last: " : "", g_tclo_last);
        line(b);
    }
    ksnprintf(b, sizeof(b), "VirtualBox:   %s", g_vbox ? (g_vbox_abs ? "VMMDev, mouse integration on" : "VMMDev (mouse integration off on the host)") : "no VMMDev");
    line(b);
    char q[120];
    qga_describe(q, sizeof(q));
    ksnprintf(b, sizeof(b), "Guest agent:  %s", q);
    line(b);
    vser_describe(line);
    uint32_t h = vmtools_host_time();
    if (h) { ksnprintf(b, sizeof(b), "Host clock:   %d s from this system's", (int)(rtc_now_utc() - h)); line(b); }
}
