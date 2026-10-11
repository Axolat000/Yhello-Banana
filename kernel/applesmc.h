#ifndef APPLESMC_H
#define APPLESMC_H

#include "types.h"

/* The Apple SMC of Intel Macs: temperatures and fans (sensors) */

#define APPLESMC_NONE (-1000)

void applesmc_init(void);
int  applesmc_present(void);
int  applesmc_read(const char* key, uint8_t* buf, int len);   /* a 4-letter key: 0 / -1 */
int  applesmc_temp(const char* key);                          /* degrees C, or APPLESMC_NONE */
int  applesmc_fans(int* rpm, int max);                        /* how many fans: their speeds */

#endif
