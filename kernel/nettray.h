/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NETTRAY_H
#define NETTRAY_H

#include "types.h"

/*
 * The network icon in the taskbar's tray, and the panel a click on it
 * opens: every network adapter with its state, the active one's address,
 * gateway, DNS and traffic, and a button to use another one. The panel is
 * built from a list of connections (kernel/nettray.c) where each adapter
 * is one entry with its kind - wired, USB, and Wi-Fi with its signal -
 * so more adapters and kinds only add entries. The Wi-Fi page scans, selects
 * SSIDs and accepts a masked password through the native station core.
 */

#define NETTRAY_ICON_W 20

/* the icon, its box's top-left at x, y (16 pixels high) */
void nettray_draw_icon(int x, int y, uint32_t bg, int hover);
/* "eth0 - Connected, 10.0.2.15": for the icon's tooltip */
void nettray_summary(char* out, int cap);

/* the panel above the tray: anchored by its right edge and the taskbar's top */
void nettray_toggle(int right_x, int bar_y);
void nettray_close(void);
int  nettray_is_open(void);
int  nettray_contains(int mx, int my);
void nettray_draw(void);

/* Panel actions open Settings > Network / Wi-Fi, or return 0. */
#define NETTRAY_SETTINGS 1
#define NETTRAY_WIFI_SETTINGS 2
/* Consumes ASCII or KB_* input while open, returns an optional Settings action. */
int nettray_key(int code);
int  nettray_click(int mx, int my);
void nettray_hover(int mx, int my);

/* changes when what the icon or the panel shows changes (the desktop redraws) */
uint32_t nettray_sig(void);

#endif
