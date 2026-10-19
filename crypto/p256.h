/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef P256_H
#define P256_H

#include "types.h"

/*
 * ECDH on NIST P-256 (secp256r1). Windows' Schannel offers the NIST curves
 * (P-256/P-384), not X25519, so Remote Desktop's TLS 1.2 needs this as well
 * as X25519 (net/tls12.c). Points are the uncompressed SEC1 form
 * 0x04 || X || Y (65 bytes); scalars and coordinates are 32-byte big-endian.
 */

/* the public key for a private scalar: out = 0x04 || X || Y */
void p256_public(uint8_t out[65], const uint8_t priv[32]);
/* the ECDH shared secret's X coordinate (32 bytes) = priv * peer.
 * 0 on success, -1 if peer is not a valid point on the curve */
int  p256_ecdh(uint8_t out[32], const uint8_t priv[32], const uint8_t peer[65]);

#endif
