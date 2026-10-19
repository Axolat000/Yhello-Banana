/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CRC32C_H
#define CRC32C_H

#include "types.h"

/* CRC-32C (Castagnoli), the checksum ext4 and btrfs put on their metadata:
 * crc32c(0, data, len) for a whole buffer, or feed the previous result back
 * in to go on with the next piece. */
uint32_t crc32c(uint32_t crc, const void* data, uint32_t len);

#endif
