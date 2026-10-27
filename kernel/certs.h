#ifndef CERTS_H
#define CERTS_H

#include "types.h"

#define CERTS_CA_BUNDLE_PATH "/etc/ssl/certs/ca-certificates.crt"

/* Installs /etc/ssl/certs/ca-certificates.crt if not present. */
void certs_init(void);

/* Returns a pointer to raw CA bundle data (either from VFS or memory fallback).
 * Sets *len to byte count. The pointer must be freed with certs_release_ca_bundle.
 * Returns NULL if unreadable. */
const char* certs_load_ca_bundle(uint32_t* len);
void certs_release_ca_bundle(const char* buf);

#endif /* CERTS_H */
