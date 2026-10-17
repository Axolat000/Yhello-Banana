#ifndef DEFLATE_H
#define DEFLATE_H

#include "types.h"

/* Raw DEFLATE (RFC 1951) compression, as in ZIP files and gzip: LZ77 over a
 * 32 KiB window with dynamic Huffman codes per block (a block stored as it
 * is when that is smaller). Decompression is stb_image's inflater
 * (third_party/stb). */

/* len bytes of in, compressed: a kmalloc'd buffer (kfree it) of *out_len
 * bytes, or NULL when memory runs out */
uint8_t* deflate_compress(const uint8_t* in, uint32_t len, uint32_t* out_len);

/* CRC-32 (IEEE 802.3, the one of ZIP, gzip and PNG): crc32_update(0, ...)
 * to start, then the previous result to go on */
uint32_t crc32_update(uint32_t crc, const void* data, uint32_t len);

#endif
