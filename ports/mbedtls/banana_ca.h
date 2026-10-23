#ifndef BANANA_TLS_CA_H
#define BANANA_TLS_CA_H

#include "types.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/ssl.h"

#define BANANA_CA_BUNDLE_PATH "/etc/ssl/certs/ca-certificates.crt"

/* Seeds /etc/ssl/certs/ca-certificates.crt in the VFS if not already present.
 * Uses the compressed system CA bundle embedded in the kernel. */
void banana_tls_ca_init_fs(void);

/* Returns a pointer to the shared trusted CA store parsed from
 * /etc/ssl/certs/ca-certificates.crt (or built-in system bundle fallback).
 * Returns NULL on allocation or parse error. */
mbedtls_x509_crt* banana_tls_get_ca_chain(void);

/* Verification callback that enforces key usage and extended key usage for leaf certs. */
int banana_tls_verify_cert_callback(void* data, mbedtls_x509_crt* crt, int depth, uint32_t* flags);

/* Configures SSL client context with mandatory verification:
 * sets MBEDTLS_SSL_VERIFY_REQUIRED, installs trusted CA chain,
 * enforces hostname verification and required key usages.
 * Returns 0 on success, negative error code on failure. */
int banana_tls_configure_client_auth(mbedtls_ssl_config* conf, const char* hostname);

/* Verifies an X.509 certificate chain against the trusted root store,
 * expected hostname, and mandatory key usages.
 * Returns 0 on success, or a negative error code / bitmask in *flags. */
int banana_tls_verify_chain(mbedtls_x509_crt* crt, const char* hostname, uint32_t* flags);

#endif /* BANANA_TLS_CA_H */
