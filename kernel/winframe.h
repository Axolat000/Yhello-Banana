/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WINFRAME_H
#define WINFRAME_H

#include "types.h"

/*
 * Window geometry shared by the desktop's app windows: dragging by the
 * title bar, resizing by the grip in the bottom-right corner, and
 * maximize / restore by double-clicking the title bar or with its button,
 * and the title-bar buttons themselves (minimize, maximize, close).
 */

#define WIN_GRIP    14
#define WIN_TASKBAR 28

typedef struct {
    int x, y, w, h;
    int min_w, min_h;
    int dragging, resizing, dx, dy;
    int maxed, sx, sy, sw, sh;      /* geometry to restore */
    uint32_t title_ms;              /* last title-bar click (double-click) */
    int snapped;                    /* put in a half / quarter of the screen (sx.. restore it) */
    int px, py;                     /* where a drag started */
} win_geom_t;

/* ── snapping, like Windows' Aero Snap ─────────────────────────────────
 * A window dragged by its title bar to the left or right edge of the
 * screen takes that half (near a corner: that quarter), to the top edge
 * the whole screen; while it is over such a zone the desktop shows where it
 * would go (winframe_preview). Dragged away again, a snapped or maximized
 * window gets back its size. The same zones serve the terminal windows and
 * the apps' windows (kernel/gui.c, kernel/appwin.c). */
typedef struct { int x, y, w, h; } win_rect_t;

#define SNAP_NONE 0
#define SNAP_HALF 1                 /* a half or a quarter */
#define SNAP_MAX  2
/* the screen above the taskbar */
void winframe_work_area(win_rect_t* r);
/* the zone a drag at (mx, my) is over: SNAP_* and its rectangle */
int  winframe_snap_zone(int mx, int my, win_rect_t* r);
/* a named snap: 'l' left half, 'r' right half, '1'..'4' quarters (top-left,
 * top-right, bottom-left, bottom-right), 'm' the whole screen */
void winframe_snap_rect(char where, win_rect_t* r);
/* the preview the desktop draws (one at a time): set (NULL clears) / get */
void winframe_set_preview(const win_rect_t* r);
int  winframe_preview(win_rect_t* r);
/* drawing in the desktop's frame: darkens a rectangle (alpha 0-255 of black),
 * tints it towards rgb, and a soft shadow under a window's rectangle */
void winframe_shade(int x, int y, int w, int h, int alpha);
void winframe_tint(int x, int y, int w, int h, uint32_t rgb, int alpha);
void winframe_shadow(int x, int y, int w, int h);
/* the geometry of g put into the half / quarter / whole screen r (where: SNAP_*) */
void win_snap(win_geom_t* g, const win_rect_t* r, int where);
/* back to the size it had before it was snapped or maximized */
void win_unsnap(win_geom_t* g);

/* left button went down in the title bar: starts a drag, or maximizes /
 * restores on a double-click (returns 1 then: the size changed) */
int  win_title_press(win_geom_t* g, int mx, int my);

/* the title-bar buttons */
#define WIN_BTN_NONE  0
#define WIN_BTN_MIN   1
#define WIN_BTN_MAX   2
#define WIN_BTN_CLOSE 3
/* draws [_] [#] [x] ending at x = right, top y, h tall (has_max: with the
 * maximize/restore button; maxed: it shows "restore") */
void win_draw_button_row(int right, int y, int h, int has_max, int maxed);
/* which of those buttons (mx, my) is on, WIN_BTN_NONE if none */
int  win_button_hit(int right, int y, int h, int has_max, int mx, int my);
/* maximize to the whole desktop / restore the old geometry */
void win_toggle_max(win_geom_t* g);
/* the buttons of a window with this geometry, by px from its top, bh tall */
void win_draw_buttons(const win_geom_t* g, int by, int bh);
/* a click on them: maximize/restore is done here, minimize is requested
 * (the desktop takes it with winframe_take_minimize()), close is the
 * caller's job; returns the button */
int  win_button_press(win_geom_t* g, int by, int bh, int mx, int my);
int  winframe_take_minimize(void);
/* 1 if (mx, my) is the resize grip; starts resizing */
int  win_grip_press(win_geom_t* g, int mx, int my);
/* mouse moves while dragging/resizing; 1 if the geometry changed */
int  win_mouse(win_geom_t* g, int mx, int my, int left);
/* keeps the window on screen and at least its minimum size */
void win_clamp(win_geom_t* g);

#endif
