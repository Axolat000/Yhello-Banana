/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DRIVER_EXT_H
#define DRIVER_EXT_H
#include "laptop.h"
const void* driver_extension(uint32_t id,uint32_t version,uint32_t size);
void driver_power_merge(laptop_info_t*);
int driver_power_backlight(int percent);
#endif
