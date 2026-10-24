/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WEB_ROUND_RECT_H
#define WEB_ROUND_RECT_H
/* Shared row geometry: painting and hit testing must agree on transparent
 * rounded corners and on the hole inside an outline. */
static inline int web_corner_inset(int r,int dy){
    int d=r-dy,x=0;
    while((x+1)*(x+1)+d*d<=r*r)x++;
    return r-x;
}
#endif
