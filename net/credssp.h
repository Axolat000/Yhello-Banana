#ifndef CREDSSP_H
#define CREDSSP_H

#include "tls12.h"

/*
 * CredSSP (MS-CSSP), client side: Remote Desktop's Network Level
 * Authentication. Over the TLS 1.2 connection, NTLM (net/ntlm.c) logs in
 * with TSRequest versions 2 to 6, the login is bound to the server's TLS
 * public key (pubKeyAuth; from version 5 a SHA-256 hash with a client
 * nonce), then the password is handed to the server (TSCredentials) so
 * it can start the Windows session.
 */

/* 0 = logged in; -1 with a reason in err */
int credssp_login(tls12_conn_t* t, const char* user, const char* domain, const char* password,
                  const char* host, uint32_t timeout_ms, char* err, int errcap);

#endif
