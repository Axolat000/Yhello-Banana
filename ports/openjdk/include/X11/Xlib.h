/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS has no X11: what java.desktop's headless native code takes
 * from <X11/Xlib.h> - the rectangle type of its region code
 * (src/java.desktop/unix/native/common/awt/utility/rect.h) */
#ifndef BANANA_X11_XLIB_H
#define BANANA_X11_XLIB_H
typedef struct { short x, y; unsigned short width, height; } XRectangle;
#endif
