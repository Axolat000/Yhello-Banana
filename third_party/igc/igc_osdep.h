/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Banana OS adaptation: no host/BSD kernel interfaces. */
#ifndef BANANA_IGC_OSDEP_H
#define BANANA_IGC_OSDEP_H
#include "types.h"
#include "kstring.h"
typedef _Bool bool;
#define true 1
#define false 0
#define ETHER_ADDR_LEN 6
#define __packed __attribute__((packed))
#define DEBUGOUT(...) ((void)0)
#define DEBUGOUT1(...) ((void)0)
#define DEBUGOUT2(...) ((void)0)
#define DEBUGOUT3(...) ((void)0)
#define DEBUGOUT7(...) ((void)0)
#define DEBUGFUNC(...) ((void)0)
struct igc_hw;
uint32_t igc_mmio_read(struct igc_hw*, uint32_t);
void igc_mmio_write(struct igc_hw*, uint32_t, uint32_t);
void igc_delay_us(uint32_t);
#define IGC_READ_REG(h,r) igc_mmio_read((h),(r))
#define IGC_WRITE_REG(h,r,v) igc_mmio_write((h),(r),(v))
#define IGC_READ_REG_ARRAY(h,r,o) IGC_READ_REG((h),(r)+4*(o))
#define IGC_WRITE_REG_ARRAY(h,r,o,v) IGC_WRITE_REG((h),(r)+4*(o),(v))
#define IGC_WRITE_FLUSH(h) ((void)IGC_READ_REG((h),IGC_STATUS))
#define DELAY(us) igc_delay_us(us)
#define msec_delay(ms) igc_delay_us(1000u*(ms))
#endif
