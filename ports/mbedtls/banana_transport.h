#ifndef BANANA_TLS_TRANSPORT_H
#define BANANA_TLS_TRANSPORT_H
#include "tcp.h"
/* Caller owns TCP. Retain last_net_error to diagnose the original error.
 * Callbacks block for at most timeout_ms per TCP operation. */
typedef struct {
    tcp_conn_t* tcp;
    uint32_t timeout_ms;
    int last_net_error;
} banana_tls_transport;
int banana_tls_send(void* ctx, const unsigned char* buf, size_t len);
int banana_tls_recv(void* ctx, unsigned char* buf, size_t len);
#endif
