/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BHISTORY_H
#define BHISTORY_H

#include "types.h"

/* The Browser's visited pages (kernel/bhistory.c): what its address bar
 * suggests while you type. Kept in ~/.config/browser-history. */

#define BH_URL   512
#define BH_TITLE 96

/* a page was shown (http / https / file: about: pages are not kept) */
void bh_visit(const char* url, const char* title);
/* up to max pages whose address or title holds q (any case), the most
 * visited first; their indexes for bh_url / bh_title */
int  bh_match(const char* q, int* out, int max);
const char* bh_url(int i);
const char* bh_title(int i);

#endif
