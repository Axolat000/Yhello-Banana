/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NTLM_H
#define NTLM_H

#include "types.h"
#include "md5.h"

/*
 * NTLM, client side (MS-NLMP): what Remote Desktop's NLA (net/credssp.c)
 * logs in with. NTLMv2 only, with extended session security, 128-bit
 * keys, key exchange and the MIC; the three messages are raw (CredSSP
 * carries them without SPNEGO). After the login both sides seal their
 * messages with RC4 and sign them with HMAC-MD5.
 */

#define NTLM_MSG_MAX 1024

typedef struct {
    /* the login, as UTF-16LE */
    uint8_t  user[256], domain[256], password[256];
    uint32_t user_len, domain_len, password_len;
    char     spn[96];                   /* "TERMSRV/host" */

    uint8_t  nego[48];                  /* our NEGOTIATE, kept for the MIC */
    uint32_t nego_len;
    uint32_t flags;                     /* as the server answered */

    uint8_t  exported[16];              /* the session key */
    uint8_t  c_sign[16], s_sign[16];
    rc4_t    c_seal, s_seal;
    uint32_t c_seq, s_seq;

    /* known-answer tests: fixed values instead of random ones */
    int      fixed;
    uint8_t  fixed_cc[8], fixed_key[16], fixed_time[8];
} ntlm_t;

/* user/domain/password are UTF-8; host is the server (for its SPN); 0 = ok */
int  ntlm_init(ntlm_t* n, const char* user, const char* domain, const char* password, const char* host);
/* the NEGOTIATE message: its length */
uint32_t ntlm_negotiate(ntlm_t* n, uint8_t* out);
/* answers the server's CHALLENGE with AUTHENTICATE (out: NTLM_MSG_MAX
 * bytes); its length, or -1 with a reason in err */
int  ntlm_authenticate(ntlm_t* n, const uint8_t* chal, uint32_t chal_len, uint8_t* out, char* err, int errcap);
/* EncryptMessage: signature (16 bytes) then the sealed message, into out
 * (len + 16 bytes) - the client's direction */
void ntlm_seal(ntlm_t* n, const uint8_t* msg, uint32_t len, uint8_t* out);
/* DecryptMessage of what the server sent (signature + sealed): the
 * message in place of `in` minus the signature, its length, or -1 if the
 * signature is wrong */
int  ntlm_unseal(ntlm_t* n, uint8_t* in, uint32_t len, uint8_t** msg);
void ntlm_wipe(ntlm_t* n);

/* pieces of the computation, for crypto/selftest.c */
void ntlm_ntowfv2(const uint8_t* pw16, uint32_t pw_len, const uint8_t* user16, uint32_t user_len,
                  const uint8_t* dom16, uint32_t dom_len, uint8_t out[16]);
/* NTProofStr (16) || temp into resp (16 + 28 + av_len + 4 bytes); the
 * session base key; returns the response length */
uint32_t ntlm_v2_response(const uint8_t key[16], const uint8_t server_chal[8], const uint8_t client_chal[8],
                          const uint8_t time[8], const uint8_t* av, uint32_t av_len,
                          uint8_t* resp, uint8_t base_key[16]);
/* the signing and sealing keys from the exported session key */
void ntlm_keys(ntlm_t* n, const uint8_t exported[16]);
/* UTF-8 -> UTF-16LE (at most cap bytes): the byte count */
uint32_t ntlm_utf16(const char* s, uint8_t* out, uint32_t cap);

#endif
