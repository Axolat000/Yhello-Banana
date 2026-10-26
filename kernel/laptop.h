/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LAPTOP_H
#define LAPTOP_H
#include "types.h"
#include "../usb/ucsi.h"
#define LAPTOP_BATTERIES 4
typedef struct {
    char model[48],serial[32],chemistry[24];
    int present,valid,percent,minutes,unit;
    uint32_t state,design,full,remaining,rate,millivolts;
} laptop_battery_t;
typedef struct {
    int runtime,ec,battery_count,ac_present,ac_online,lid,brightness,brightness_pending;
    char status[112];
    uint32_t generation,updated_ms,aml_bytes;
    laptop_battery_t batteries[LAPTOP_BATTERIES];
    ucsi_t usbc;
} laptop_info_t;
void laptop_init(void);
void laptop_get_info(laptop_info_t*);
int laptop_backlight(int percent);
int laptop_backlight_step(int delta);
int laptop_usbc_role(unsigned port,int source);
#endif
