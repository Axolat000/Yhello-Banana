/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "nettray.h"
#include "i18n.h"
#include "theme.h"
#include "gfx.h"
#include "kstring.h"
#include "timer.h"
#include "../net/net.h"
#include "../net/wifi.h"
#include "kbnav.h"

/* ── the connections ─────────────────────────────────────────────────
 * One entry per registered network adapter, including native Wi-Fi state
 * and signal strength from the station core. */

enum { NT_WIRED, NT_USB, NT_WIFI };
enum { ST_OFF, ST_NO_LINK, ST_CONNECTING, ST_CONNECTED };   /* (ST_OFF: present, not in use) */

typedef struct {
    netdev_t* dev;
    int       kind;
    int       active;               /* the one the system uses (one at a time for now) */
    int       state;
    int       signal;               /* Wi-Fi: 0..4 bars */
} nt_conn_t;

#define NT_MAX 16

static int link_of(netdev_t* d) { return d->link_up ? d->link_up(d) != 0 : 1; }

static int collect(nt_conn_t* out, int max) {
    netif_t* nif = net_if();
    int n = 0;
    for (int i = 0; i < net_device_count() && n < max; i++) {
        netdev_t* d = net_device_at(i);
        if (!d || !d->present) continue;
        nt_conn_t* c = &out[n++];
        c->dev = d;
        c->kind = d->is_wifi ? NT_WIFI : d->is_usb ? NT_USB : NT_WIRED;
        c->active = d == nif->dev;
        c->signal = 0;
        wifi_radio_t* radio=wifi_from_device(d);
        if(radio && radio->state==WIFI_UP) {
            int dbm=radio->ap.rssi;c->signal=dbm>=-50?4:dbm>=-65?3:dbm>=-75?2:1;
        }
        if (!c->active) c->state = ST_OFF;
        else if (!link_of(d)) c->state = ST_NO_LINK;
        else c->state = nif->configured ? ST_CONNECTED : ST_CONNECTING;
    }
    return n;
}

/* the state the icon shows: the active connection's, or none at all */
static int overall(nt_conn_t* c, int n, int* kind) {
    for (int i = 0; i < n; i++) if (c[i].active) { *kind = c[i].kind; return c[i].state; }
    *kind = NT_WIRED;
    return -1;
}

static const char* kind_name(int k) { return k == NT_WIFI ? "Wi-Fi" : k == NT_USB ? "USB Ethernet" : "Ethernet"; }

static const char* state_name(int s) {
    switch (s) {
    case ST_CONNECTED:  return "Connected";
    case ST_CONNECTING: return "Getting an address...";
    case ST_NO_LINK:    return "Cable unplugged";
    default:            return "Not in use";
    }
}

/* ── the icon ──────────────────────────────────────────────────────── */

static void px(int x, int y, uint32_t c) { gfx_fill_rect(x, y, 1, 1, c); }

/* a small red cross: no network */
static void cross(int x, int y, uint32_t c) {
    for (int i = 0; i < 6; i++) { px(x + i, y + i, c); px(x + i + 1, y + i, c); px(x + 5 - i, y + i, c); px(x + 6 - i, y + i, c); }
}

/* kind's glyph in 16x16 at x, y: a screen on its stand (wired), the same
 * with a plug (USB), or bars (Wi-Fi), lit by state */
static void draw_glyph(int x, int y, int kind, int state, int signal, uint32_t bg) {
    uint32_t frame = TC(0xC8D2DE), lit = TC(0x68A8F0), dim = TC(0x3A4556);
    uint32_t screen = state == ST_CONNECTED ? lit : state == ST_CONNECTING ? TC(0xC9A45C) : dim;
    (void)bg;
    if (kind == NT_WIFI) {
        for (int b = 0; b < 4; b++) {
            int h = 4 + b * 3;
            gfx_fill_rect(x + 1 + b * 4, y + 15 - h, 3, h, state == ST_CONNECTED && b < signal ? frame : dim);
        }
    } else {
        gfx_fill_rect(x + 1, y + 1, 14, 10, frame);          /* the screen */
        gfx_fill_rect(x + 2, y + 2, 12, 8, screen);
        gfx_fill_rect(x + 6, y + 11, 4, 2, frame);           /* its stand */
        gfx_fill_rect(x + 4, y + 13, 8, 2, frame);
        if (kind == NT_USB) { gfx_fill_rect(x + 12, y + 12, 4, 3, frame); px(x + 13, y + 15, frame); }
        if (state == ST_CONNECTING && (timer_ms() / 400) % 2) gfx_fill_rect(x + 5, y + 5, 6, 2, TC(0xFFFFFF));
    }
    if (state == ST_NO_LINK || state < 0) cross(x + 9, y + 8, TC(0xE05A4A));
}

void nettray_draw_icon(int x, int y, uint32_t bg, int hover) {
    nt_conn_t c[NT_MAX];
    int n = collect(c, NT_MAX), kind, st = overall(c, n, &kind);
    if (hover) gfx_fill_rect(x - 2, y - 3, NETTRAY_ICON_W, 22, TC(0x303A4A));
    int sig = 0;
    for (int i = 0; i < n; i++) if (c[i].active) sig = c[i].signal;
    draw_glyph(x, y, kind, st, sig, hover ? TC(0x303A4A) : bg);
}

void nettray_summary(char* out, int cap) {
    netif_t* nif = net_if();
    nt_conn_t c[NT_MAX];
    int n = collect(c, NT_MAX), kind, st = overall(c, n, &kind);
    if (st < 0) { kstrlcpy(out, n ? "Not connected" : "No network adapter", (size_t)cap); return; }
    if (st == ST_CONNECTED) {
        char ip[16];
        ip4_to_str(nif->ip, ip);
        ksnprintf(out, (size_t)cap, "%s - Connected, %s", nif->dev->ifname, ip);
    } else {
        wifi_radio_t* r=wifi_from_device(nif->dev);
        ksnprintf(out,(size_t)cap,"%s - %s",nif->dev->ifname,r?wifi_state_name(r):state_name(st));
    }
}

/* ── the panel ─────────────────────────────────────────────────────── */

#define PW      360
#define LH      18
#define PAD     12
#define C_BG    TC(0x1D232C)
#define C_EDGE  TC(0x505E74)
#define C_TEXT  TC(0xE8EEF6)
#define C_DIM   TC(0x9AA6B6)
#define C_HEAD  TC(0xFFFFFF)
#define C_LINK  TC(0x68A8F0)
#define C_OK    TC(0x8FE3A1)
#define C_WAIT  TC(0xC9A45C)
#define C_BAD   TC(0xE07A6A)

static int g_open, g_right, g_bar_y, g_x, g_y, g_h;
static int g_wifi_mode,g_adapter_page,g_radio_index,g_ap_page,g_selected=-1,g_password_focus,g_connecting,g_scanning;
static wifi_radio_t* g_radio;
static wifi_ap_t g_selected_ap;
static char g_password[65],g_message[80];
static kbnav_t g_nav;
enum { ACT_USE=1,ACT_SETTINGS,ACT_WIFI,ACT_BACK,ACT_SCAN,ACT_RADIO,ACT_DISCONNECT,
       ACT_AP,ACT_PASSWORD,ACT_CONNECT,ACT_PREV,ACT_NEXT,ACT_CARDS,ACT_WIFI_SETTINGS };
static void wipe_password(void) {
    volatile char* b=g_password;for(unsigned i=0;i<sizeof(g_password);i++) b[i]=0;
    g_password_focus=0;
}
static void clear_selection(void) { wipe_password();g_selected=-1; }
static wifi_radio_t* current_radio(void) {
    wifi_radio_t* found=NULL;int n=0;
    for(int i=0;i<net_device_count();i++) {
        netdev_t* d=net_device_at(i);
        wifi_radio_t* r=d && d->present?wifi_from_device(d):NULL;
        if(r && n++==g_radio_index) found=r;
    }
    if(!found && g_radio_index) { g_radio_index=0;return current_radio(); }
    if(found!=g_radio) { clear_selection();g_ap_page=0;g_message[0]=0;g_connecting=0;g_scanning=0;g_radio=found; }
    return found;
}
static void message(const char* text) { kstrlcpy(g_message,text,sizeof(g_message)); }
static void scan_radio(void) {
    wifi_radio_t* r=current_radio();clear_selection();g_ap_page=0;
    if(!r) return;
    g_scanning=wifi_scan(r)>=0;
    message(g_scanning?"Searching for nearby networks...":"Disconnect first, or wait for Wi-Fi.");
}
static void connect_radio(void) {
    wifi_radio_t* r=current_radio();
    if(!r || g_selected<0 || g_selected>=r->nap) { wipe_password();message("Select a network first.");return; }
    if(r->state!=WIFI_DOWN) {
        wipe_password();message(r->state==WIFI_SCAN?"Wait for the scan to finish.":"Disconnect first, or wait for Wi-Fi.");return;
    }
    wifi_ap_t* a=&r->aps[g_selected];
    if(a->ssid_len>sizeof(a->ssid) || a->ssid_len!=g_selected_ap.ssid_len || memcmp(a->ssid,g_selected_ap.ssid,a->ssid_len) ||
       memcmp(a->bssid,g_selected_ap.bssid,6) || a->privacy!=g_selected_ap.privacy || a->wpa2!=g_selected_ap.wpa2) {
        clear_selection();message("Network list changed. Select it again.");return;
    }
    if(!a->ssid_len || (a->privacy && !a->wpa2)) {
        wipe_password();message("This network's security is unsupported.");return;
    }
    int rc=wifi_connect(r,a->ssid,a->ssid_len,g_password,!a->privacy);
    wipe_password();
    g_connecting=rc>=0;
    message(rc<0?"Could not connect. Check password or scan.":"Connecting; waiting for authentication...");
}
static int g_hover = -1;                      /* index of the hovered control */

/* where the panel's buttons are this frame */
static struct { int x,y,w,h,action,value;netdev_t* dev; } g_btn[32];
static int g_nbtn;

static void fmt_bytes(uint32_t b, char* out, int cap) {
    if (b >= (10u << 20)) ksnprintf(out, (size_t)cap, "%u MB", b >> 20);
    else if (b >= (1u << 20)) ksnprintf(out, (size_t)cap, "%u.%u MB", b >> 20, ((b >> 10) & 1023) * 10 / 1024);
    else if (b >= 1024) ksnprintf(out, (size_t)cap, "%u KB", b >> 10);
    else ksnprintf(out, (size_t)cap, "%u bytes", b);
}

/* the lines an adapter takes in the panel */
static int conn_lines(const nt_conn_t* c) {
    if (c->active && c->state == ST_CONNECTED) return 9;
    return c->active ? 4 : 3;
}

static int panel_height(nt_conn_t* c, int n) {
    int h = PAD + LH + 8;                     /* the title, a rule */
    if (!n) h += LH * 2;
    for (int i = 0; i < n; i++) h += conn_lines(&c[i]) * LH + 8;
    return h + LH * 4 + PAD + 4;                  /* the settings link */
}

static void row(int x, int y, const char* k, const char* v) {
    gfx_draw_text(x, y, k, C_DIM, C_BG);
    gfx_draw_text(x + 15 * 8, y, v, C_TEXT, C_BG);
}

static void hit(int x,int y,int w,int h,int action,int value,netdev_t* dev) {
    if(g_nbtn>=(int)(sizeof(g_btn)/sizeof(g_btn[0]))) return;
    g_btn[g_nbtn].x=x;g_btn[g_nbtn].y=y;g_btn[g_nbtn].w=w;g_btn[g_nbtn].h=h;
    g_btn[g_nbtn].action=action;g_btn[g_nbtn].value=value;g_btn[g_nbtn].dev=dev;g_nbtn++;
    kbnav_add(&g_nav,x,y,w,h);
}
static void control(int x,int y,int w,const char* text,int action,int value,netdev_t* dev) {
    uint32_t bg=g_hover==g_nbtn?TC(0x405478):TC(0x303A4A);
    gfx_fill_rect(x,y-3,w,LH+2,bg);gfx_draw_text(x+6,y,text,C_TEXT,bg);
    hit(x,y-3,w,LH+2,action,value,dev);
}
static void button(int i,int x,int y,const char* text,netdev_t* dev) {
    (void)i;control(x,y,(int)strlen(text)*8+16,text,ACT_USE,0,dev);
}
static void ap_name(const wifi_ap_t* a,char* text,unsigned max) {
    unsigned n=a->ssid_len;if(n>max) n=max;
    for(unsigned i=0;i<n;i++) text[i]=a->ssid[i]>=32 && a->ssid[i]<127?(char)a->ssid[i]:'?';
    text[n]=0;if(!n) kstrlcpy(text,"(hidden)",max+1);
}
static void draw_wifi_panel(int x,int y) {
    char t[80];wifi_radio_t* r=current_radio();
    gfx_draw_text(x,y,"Wi-Fi",C_HEAD,C_BG);
    control(x+PW-2*PAD-72,y,72,_("Back"),ACT_BACK,0,NULL);
    if(!r) {
        gfx_draw_text(x,y+40,_("No supported Wi-Fi radio detected."),C_DIM,C_BG);
        control(x,y+80,192,_("Wi-Fi settings..."),ACT_WIFI_SETTINGS,0,NULL);return;
    }
    if(g_scanning && r->state!=WIFI_SCAN) {
        message(r->nap?"Select a network.":"No networks found. Try Scan.");g_scanning=0;
    }
    if(g_connecting && (r->state==WIFI_UP || r->state==WIFI_DOWN)) {
        message(r->state==WIFI_UP?"Connected.":"Connection failed or disconnected.");g_connecting=0;
    }
    ksnprintf(t,sizeof(t),"%s: %s",r->nd.ifname,wifi_state_name(r));
    gfx_draw_text(x,y+28,t,C_TEXT,C_BG);
    control(x,y+54,68,_("Scan"),ACT_SCAN,0,NULL);
    control(x+74,y+54,116,_("Next radio"),ACT_RADIO,0,NULL);
    control(x+196,y+54,136,_("Disconnect"),ACT_DISCONNECT,0,NULL);
    gfx_draw_text(x,y+80,_("Network / security / signal"),C_DIM,C_BG);
    int pages=(r->nap+3)/4;if(!pages) pages=1;if(g_ap_page>=pages) g_ap_page=pages-1;
    for(int i=0;i<4;i++) {
        int index=g_ap_page*4+i;if(index>=r->nap) break;
        wifi_ap_t* ap=&r->aps[index];char name[23];ap_name(ap,name,22);
        uint32_t bg=index==g_selected?TC(0x405478):g_hover==g_nbtn?TC(0x303A4A):C_BG;
        int yy=y+98+i*28;gfx_fill_rect(x,yy-4,PW-2*PAD,26,bg);
        gfx_draw_text(x+4,yy,name,C_TEXT,bg);
        ksnprintf(t,sizeof(t),"%s %d",ap->wpa2?"WPA2":ap->privacy?"Other":"Open",ap->rssi);
        gfx_draw_text(x+192,yy,t,C_DIM,bg);
        hit(x,yy-4,PW-2*PAD,26,ACT_AP,index,NULL);
    }
    if(!r->nap) gfx_draw_text(x,y+110,r->state==WIFI_SCAN?_("Scanning..."):_("No networks. Try Scan."),C_DIM,C_BG);
    control(x,y+214,40,"<",ACT_PREV,0,NULL);
    ksnprintf(t,sizeof(t),"%d / %d",g_ap_page+1,pages);gfx_draw_text(x+56,y+214,t,C_DIM,C_BG);
    control(x+136,y+214,40,">",ACT_NEXT,0,NULL);
    char name[33]="";if(g_selected>=0) ap_name(&g_selected_ap,name,32);
    name[29]=0;ksnprintf(t,sizeof(t),"Selected: %s",name);gfx_draw_text(x,y+242,t,C_TEXT,C_BG);
    gfx_draw_text(x,y+262,_("Password (WPA2)"),C_DIM,C_BG);
    gfx_fill_rect(x,y+276,PW-2*PAD,24,TC(0x10141C));
    unsigned n=(unsigned)strlen(g_password);if(n>38) n=38;
    char stars[39];memset(stars,'*',n);stars[n]=0;
    gfx_draw_text(x+6,y+284,stars,C_TEXT,TC(0x10141C));
    if(g_password_focus && (timer_ms()/500)%2==0) gfx_fill_rect(x+6+(int)n*8,y+282,2,12,C_LINK);
    hit(x,y+276,PW-2*PAD,24,ACT_PASSWORD,0,NULL);
    control(x,y+316,196,g_selected>=0 && !g_selected_ap.privacy?_("Connect (open)"):_("Connect (WPA2)"),ACT_CONNECT,0,NULL);
    char msg[42];kstrlcpy(msg,g_message,sizeof(msg));gfx_draw_text(x,y+346,msg,C_DIM,C_BG);
    control(x,y+378,192,_("Wi-Fi settings..."),ACT_WIFI_SETTINGS,0,NULL);
}

void nettray_draw(void) {
    if (!g_open) return;
    nt_conn_t c[NT_MAX];
    int n = collect(c, NT_MAX);
    netif_t* nif = net_if();
    int total=n;
    if(g_adapter_page*2>=n) g_adapter_page=0;
    int first=g_adapter_page*2;n-=first;if(n>2) n=2;
    g_h = g_wifi_mode?(current_radio()?414:136):panel_height(c+first,n);
    g_x = g_right - PW;
    if (g_x < 4) g_x = 4;
    g_y = g_bar_y - 4 - g_h;
    if (g_y < 4) g_y = 4;
    int x = g_x + PAD, y = g_y + PAD;
    g_nbtn=0;kbnav_begin(&g_nav);
    if(g_h>g_bar_y-8) { /* The desktop's supported minimum is 640x480. */
        g_h=g_bar_y-8;g_y=4;y=g_y+PAD;
    }
    gfx_fill_rect(g_x + 4, g_y + 4, PW, g_h, TC(0x080A0E));    /* a shadow */
    gfx_fill_rect(g_x, g_y, PW, g_h, C_BG);
    gfx_fill_rect(g_x, g_y, PW, 1, C_EDGE); gfx_fill_rect(g_x, g_y + g_h - 1, PW, 1, C_EDGE);
    gfx_fill_rect(g_x, g_y, 1, g_h, C_EDGE); gfx_fill_rect(g_x + PW - 1, g_y, 1, g_h, C_EDGE);
    if(g_wifi_mode) { draw_wifi_panel(x,y);kbnav_draw(&g_nav,C_LINK);return; }
    gfx_draw_text(x, y, _("Network"), C_HEAD, C_BG);
    y += LH;
    gfx_fill_rect(x, y + 2, PW - 2 * PAD, 1, TC(0x343E4E));
    y += 8;
    g_nbtn = 0;
    if (!n) {
        gfx_draw_text(x, y, _("No network adapter found."), C_TEXT, C_BG);
        gfx_draw_text(x, y + LH, _("Plug one in, or see the README."), C_DIM, C_BG);
        y += LH * 2;
    }
    char t[64], v[48];
    for (int i = 0; i < n; i++) {
        nt_conn_t* k = &c[first+i];
        netdev_t* d = k->dev;
        draw_glyph(x, y, k->kind, k->active ? k->state : ST_OFF, k->signal, C_BG);
        ksnprintf(t, sizeof(t), "%s  %s", kind_name(k->kind), d->ifname);
        gfx_draw_text(x + 24, y, t, C_HEAD, C_BG);
        uint32_t sc = k->state == ST_CONNECTED ? C_OK : k->state == ST_CONNECTING ? C_WAIT
                    : k->state == ST_NO_LINK ? C_BAD : C_DIM;
        wifi_radio_t* radio=wifi_from_device(d);
        const char* st = radio?wifi_state_name(radio):state_name(k->state);
        gfx_draw_text(g_x + PW - PAD - (int)strlen(st) * 8, y + LH, st, sc, C_BG);
        y += LH;
        kstrlcpy(t, d->model ? d->model : d->name, sizeof(t));
        if ((int)strlen(t) > 22) { t[21] = '.'; t[22] = 0; }
        gfx_draw_text(x + 24, y, t, C_DIM, C_BG);
        y += LH;
        if (k->active && k->state == ST_CONNECTED) {
            char a[16];
            ip4_to_str(nif->ip, a);      row(x + 24, y, "IPv4 address", a); y += LH;
            ip4_to_str(nif->netmask, a); row(x + 24, y, "Subnet mask", a);  y += LH;
            ip4_to_str(nif->gateway, a); row(x + 24, y, "Gateway", nif->gateway ? a : "none"); y += LH;
            ip4_to_str(nif->dns, a);     row(x + 24, y, "DNS server", nif->dns ? a : "none"); y += LH;
            if (nif->dhcp) ksnprintf(v, sizeof(v), "DHCP (%u h lease)", nif->lease_s / 3600);
            else kstrlcpy(v, "set by hand", sizeof(v));
            row(x + 24, y, "Address from", v); y += LH;
        }
        ksnprintf(v, sizeof(v), "%02x:%02x:%02x:%02x:%02x:%02x", d->mac[0], d->mac[1], d->mac[2], d->mac[3], d->mac[4], d->mac[5]);
        if (k->active) {
            row(x + 24, y, "MAC address", v); y += LH;
            char s1[16], s2[16];
            fmt_bytes(d->tx_bytes, s1, sizeof(s1));
            fmt_bytes(d->rx_bytes, s2, sizeof(s2));
            ksnprintf(v, sizeof(v), "%s / %s", s1, s2);
            row(x + 24, y, "Sent / recvd", v); y += LH;
        } else {
            button(i, x + 24, y + 2, _("Use this one"), d);
            y += LH;
        }
        y += 8;
    }
    gfx_fill_rect(x, y - 4, PW - 2 * PAD, 1, TC(0x343E4E));
    y += 4;
    if(total>2) {
        control(x,y,132,_("Next adapters"),ACT_CARDS,0,NULL);
        y+=LH+8;
    } else y+=LH+8;
    control(x,y,192,_("Wi-Fi networks..."),ACT_WIFI,0,NULL);y+=LH+8;
    control(x,y,192,_("Network settings..."),ACT_SETTINGS,0,NULL);
    kbnav_draw(&g_nav,C_LINK);
}

void nettray_toggle(int right_x, int bar_y) {
    if(g_open) { nettray_close();return; }
    g_open=1;g_wifi_mode=0;g_adapter_page=0;g_nbtn=0;g_message[0]=0;clear_selection();
    memset(&g_nav,0,sizeof(g_nav));
    g_right = right_x;
    g_bar_y = bar_y;
    g_hover = -1;
}

void nettray_close(void) {
    g_open=0;g_hover=-1;g_nbtn=0;g_connecting=0;g_scanning=0;clear_selection();g_message[0]=0;
}
int  nettray_is_open(void) { return g_open; }

int nettray_contains(int mx, int my) {
    return g_open && mx >= g_x && mx < g_x + PW && my >= g_y && my < g_y + g_h;
}

static int button_at(int mx, int my) {
    for (int i = 0; i < g_nbtn; i++)
        if (mx >= g_btn[i].x && mx < g_btn[i].x + g_btn[i].w && my >= g_btn[i].y && my < g_btn[i].y + g_btn[i].h) return i;
    return -1;
}

void nettray_hover(int mx,int my) {
    if(!g_open) return;
    g_hover=button_at(mx,my);
}
static int activate(int b) {
    if(b<0 || b>=g_nbtn) return 0;
    int action=g_btn[b].action,value=g_btn[b].value;
    wifi_radio_t* previous=g_radio;
    wifi_radio_t* r=current_radio();
    if(g_wifi_mode && r!=previous) { g_nbtn=0;return 0; }
    switch(action) {
    case ACT_SETTINGS:nettray_close();return NETTRAY_SETTINGS;
    case ACT_WIFI_SETTINGS:nettray_close();return NETTRAY_WIFI_SETTINGS;
    case ACT_WIFI:
        clear_selection();g_wifi_mode=1;g_ap_page=0;g_message[0]=0;
        if(r && r->state==WIFI_DOWN) scan_radio();
        break;
    case ACT_BACK:clear_selection();g_wifi_mode=0;break;
    case ACT_USE:
        for(int i=0;i<net_device_count();i++) {
            netdev_t* d=net_device_at(i);if(d==g_btn[b].dev && d->present) net_select_device(d);
        }
        break;
    case ACT_CARDS:g_adapter_page++;break;
    case ACT_SCAN:scan_radio();break;
    case ACT_RADIO:clear_selection();g_radio_index++;r=current_radio();if(r && r->state==WIFI_DOWN) scan_radio();break;
    case ACT_DISCONNECT:clear_selection();if(r) wifi_disconnect(r);g_connecting=0;g_scanning=0;message("Disconnected.");break;
    case ACT_AP:
        clear_selection();
        if(r && value>=0 && value<r->nap) {
            g_selected=value;g_selected_ap=r->aps[value];
            g_password_focus=g_selected_ap.privacy && g_selected_ap.wpa2;
            message(!g_selected_ap.ssid_len?"Hidden SSID is not supported.":
                    g_selected_ap.privacy && !g_selected_ap.wpa2?"This network's security is unsupported.":
                    g_selected_ap.privacy?"Enter the Wi-Fi password.":"Open network: no password required.");
        }
        break;
    case ACT_PASSWORD:g_password_focus=r && g_selected>=0 && g_selected_ap.privacy && g_selected_ap.wpa2;break;
    case ACT_CONNECT:connect_radio();break;
    case ACT_PREV:clear_selection();if(g_ap_page>0) g_ap_page--;break;
    case ACT_NEXT:clear_selection();if(r && (g_ap_page+1)*4<r->nap) g_ap_page++;break;
    }
    /* Rectangles from the previous page/radio must never remain clickable. */
    g_nbtn=0;g_hover=-1;return 0;
}
int nettray_click(int mx,int my) {
    if(!g_open) return 0;
    kbnav_mouse(&g_nav);g_password_focus=0;
    return activate(button_at(mx,my));
}
int nettray_key(int code) {
    if(!g_open) return 0;
    current_radio(); /* Hot-unplug clears a pending password/selection. */
    if(code==27 || code==KB_ESC) { nettray_close();return 0; }
    if(g_wifi_mode && g_password_focus) {
        unsigned n=(unsigned)strlen(g_password);
        if(code=='\n' || code==KB_ENTER) { connect_radio();return 0; }
        if(code=='\b' || code==127 || code==KB_DEL) { if(n) g_password[n-1]=0;return 0; }
        if(code>=32 && code<127) { if(n<sizeof(g_password)-1) { g_password[n]=(char)code;g_password[n+1]=0; }return 0; }
        if(code!='\t' && code!=KB_TAB && code!=KB_BACKTAB) return 0;
        g_password_focus=0;
    }
    if(code=='\n') code=KB_ENTER;
    else if(code=='\t') code=KB_TAB;
    else if(code==' ') code=KB_SPACE;
    int x,y;
    if(kbnav_key(&g_nav,code,&x,&y)==1) return activate(button_at(x,y));
    return 0;
}

uint32_t nettray_sig(void) {
    nt_conn_t c[NT_MAX];
    int n = collect(c, NT_MAX);
    uint32_t s = (uint32_t)n * 131u + (uint32_t)g_open * 7u + (uint32_t)(g_hover + 1) * 17u + net_if()->ip;
    for (int i = 0; i < n; i++) s = s * 31u + (uint32_t)c[i].state * 5u + (uint32_t)c[i].active + (uint32_t)c[i].signal*19u;
    if(g_wifi_mode && g_open) {
        wifi_radio_t* r=current_radio();
        s=s*31u+(uint32_t)(g_selected+1)+(uint32_t)strlen(g_password)*7u+(uint32_t)g_ap_page*13u;
        if(r) s=s*31u+(uint32_t)r->state*17u+(uint32_t)r->nap;
    }
    if (g_open) s += timer_ms() / 500u;               /* (the traffic counters, once a second) */
    else for (int i = 0; i < n; i++) if (c[i].state == ST_CONNECTING) s += timer_ms() / 400u;   /* (the blinking) */
    return s;
}
