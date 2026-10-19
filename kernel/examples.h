/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef EXAMPLES_H
#define EXAMPLES_H

#include "types.h"

/* puts the SDK's example apps (embedded in the kernel) into
 * ~/Examples - `pkg install ~/Examples/paint.bpk` */
void examples_seed(void);
/* the examples one by one ("paint.bpk" and its zlib-compressed bytes), for the App Store */
int examples_count(void);
const char* examples_file(int i);
const uint8_t* examples_data(int i, uint32_t* len);

#endif
