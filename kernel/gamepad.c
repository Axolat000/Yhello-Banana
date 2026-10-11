#include "gamepad.h"
#include "kstring.h"
#include "serial.h"

static gamepad_t g_pad[GP_MAX];

int gamepad_add(const char* name) {
    for (int i = 0; i < GP_MAX; i++) {
        if (g_pad[i].connected) continue;
        memset(&g_pad[i], 0, sizeof(g_pad[i]));
        kstrlcpy(g_pad[i].name, name && name[0] ? name : "Game controller", sizeof(g_pad[i].name));
        g_pad[i].connected = 1;
        klog("gamepad %d: %s\n", i, g_pad[i].name);
        return i;
    }
    return -1;
}

void gamepad_update(int i, const gamepad_t* st) {
    if (i < 0 || i >= GP_MAX || !g_pad[i].connected) return;
    gamepad_t* p = &g_pad[i];
    p->lx = st->lx; p->ly = st->ly; p->rx = st->rx; p->ry = st->ry;
    p->lt = st->lt; p->rt = st->rt;
    uint32_t b = st->buttons;
    if (p->lt > 16384) b |= GP_LT;
    if (p->rt > 16384) b |= GP_RT;
    p->buttons = b;
}

void gamepad_remove(int i) {
    if (i < 0 || i >= GP_MAX || !g_pad[i].connected) return;
    klog("gamepad %d: %s unplugged\n", i, g_pad[i].name);
    memset(&g_pad[i], 0, sizeof(g_pad[i]));
}

int gamepad_get(int i, gamepad_t* out) {
    if (i < 0 || i >= GP_MAX || !g_pad[i].connected) { if (out) memset(out, 0, sizeof(*out)); return 0; }
    if (out) *out = g_pad[i];
    return 1;
}

int gamepad_axis(int32_t v, int32_t lmin, int32_t lmax) {
    if (lmax <= lmin) return 0;
    int64_t r = ((int64_t)(v - lmin) * 65534) / (lmax - lmin) - 32767;
    if (r < -32767) r = -32767;
    if (r > 32767) r = 32767;
    if (r > -300 && r < 300) r = 0;          /* (a centred stick that reads 127 of 0..255) */
    return (int)r;
}
