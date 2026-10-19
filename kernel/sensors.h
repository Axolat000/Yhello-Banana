/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef SENSORS_H
#define SENSORS_H

#include "types.h"

/* hardware sensors: the processor's temperature (Intel DTS, AMD Tctl) */

#define SENSOR_NONE (-1000)

int         sensors_cpu_temp(void);     /* degrees C, or SENSOR_NONE */
int         sensors_cpu_tjmax(void);    /* Intel: the throttling point, else 0 */
const char* sensors_cpu_source(void);   /* where it comes from, or NULL (no sensor) */

#endif
