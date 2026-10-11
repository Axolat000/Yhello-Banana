#ifndef GAMEPAD_H
#define GAMEPAD_H

#include "types.h"

/*
 * Game controllers: whatever the device (a HID gamepad or joystick, an
 * Xbox 360 or Xbox One controller), its state in one layout - the Xbox
 * one: two sticks, two analog triggers, A B X Y, bumpers, Back / Start /
 * Guide, the stick buttons and the d-pad. The same layout apps get
 * (banana_api.h: banana_gamepad_t, BANANA_PAD_*).
 */

#define GP_MAX 4

#define GP_A      0x0001u
#define GP_B      0x0002u
#define GP_X      0x0004u
#define GP_Y      0x0008u
#define GP_LB     0x0010u
#define GP_RB     0x0020u
#define GP_BACK   0x0040u
#define GP_START  0x0080u
#define GP_LS     0x0100u
#define GP_RS     0x0200u
#define GP_UP     0x0400u
#define GP_DOWN   0x0800u
#define GP_LEFT   0x1000u
#define GP_RIGHT  0x2000u
#define GP_GUIDE  0x4000u
#define GP_LT     0x8000u      /* the triggers pressed past half */
#define GP_RT     0x10000u

typedef struct {
    int      connected;
    char     name[40];
    int      lx, ly, rx, ry;   /* sticks: -32767..32767, + = right / down */
    int      lt, rt;           /* triggers: 0..32767 */
    uint32_t buttons;          /* GP_* */
} gamepad_t;

int  gamepad_add(const char* name);                  /* a slot, or -1 (all taken) */
void gamepad_update(int i, const gamepad_t* st);     /* (name and connected kept) */
void gamepad_remove(int i);
int  gamepad_get(int i, gamepad_t* out);             /* 1 if a controller is there */

/* -32767..32767 from a value in lmin..lmax (the middle is 0) */
int  gamepad_axis(int32_t v, int32_t lmin, int32_t lmax);

#endif
