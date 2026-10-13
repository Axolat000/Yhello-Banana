#ifndef RSA_H
#define RSA_H

#include "types.h"

/*
 * RSA-2048 with e = 65537: what `adb` needs - a key pair of its own, a
 * PKCS#1 v1.5 signature over the phone's 20-byte token (signed as a SHA-1
 * digest), and the public key in Android's format.
 */

#define RSA_WORDS 64                       /* 2048 bits, 32-bit words, little endian */
#define RSA_BYTES 256

typedef struct {
    uint32_t n[RSA_WORDS];                 /* modulus */
    uint32_t d[RSA_WORDS];                 /* private exponent */
    uint32_t e;
} rsa_key_t;

/* a new key; rnd fills bytes with random data (the kernel's CSPRNG); 0 = ok */
int  rsa_generate(rsa_key_t* k, void (*rnd)(void* buf, uint32_t len));
/* PKCS#1 v1.5 signature of a SHA-1 digest (or anything 20 bytes long, as
 * adb signs its token): sig is RSA_BYTES, big endian */
void rsa_sign_sha1(const rsa_key_t* k, const uint8_t digest[20], uint8_t sig[RSA_BYTES]);
/* the same over a SHA-256 digest (the Remote Desktop server's TLS key) */
void rsa_sign_sha256(const rsa_key_t* k, const uint8_t digest[32], uint8_t sig[RSA_BYTES]);
/* s^e mod n (checking a signature): out RSA_BYTES, big endian */
void rsa_public(const rsa_key_t* k, const uint8_t in[RSA_BYTES], uint8_t out[RSA_BYTES]);
/* Android's RSAPublicKey structure (524 bytes): len, n0inv, n, rr, exponent */
#define RSA_ANDROID_PUB_LEN 524
void rsa_android_pubkey(const rsa_key_t* k, uint8_t out[RSA_ANDROID_PUB_LEN]);

#endif
