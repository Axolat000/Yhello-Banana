/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SCREENSHOT_H
#define SCREENSHOT_H
#include "types.h"
#define SCREENSHOT_DIR_DEFAULT "/home/banana/Pictures"
int screenshot_set_directory(const char* dir);
const char* screenshot_get_directory(char* out, int cap);
/* PNG in a fresh name in the destination directory. 0 or -1; no durability guarantee. */
int screenshot_take(const char* dir, char* out_path, int cap);
uint8_t* screenshot_encode_png(const uint8_t* pixels, int w, int h, int stride_bytes, int is_bgrx, uint32_t* out_len);
#endif
