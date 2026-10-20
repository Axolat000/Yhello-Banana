#include "banana_transport.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"

static int transport_result(banana_tls_transport* io, int rc, int sending) {
    io->last_net_error = rc < 0 ? rc : 0;
    if (rc >= 0) return rc;
    if (rc == NET_ERR_TIMEOUT) return MBEDTLS_ERR_SSL_TIMEOUT;
    if (rc == NET_ERR_RESET || rc == NET_ERR_CLOSED) return MBEDTLS_ERR_NET_CONN_RESET;
    return sending ? MBEDTLS_ERR_NET_SEND_FAILED : MBEDTLS_ERR_NET_RECV_FAILED;
}

int banana_tls_send(void* ctx, const unsigned char* buf, size_t len) {
    banana_tls_transport* io = ctx;
    if (!io || !io->tcp || !buf) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    if (len > 0x7fffffffu) len = 0x7fffffffu;
    return transport_result(io, tcp_send(io->tcp, buf, (uint32_t)len, io->timeout_ms), 1);
}

int banana_tls_recv(void* ctx, unsigned char* buf, size_t len) {
    banana_tls_transport* io = ctx;
    if (!io || !io->tcp || !buf) return MBEDTLS_ERR_NET_INVALID_CONTEXT;
    if (len > 0x7fffffffu) len = 0x7fffffffu;
    return transport_result(io, tcp_recv(io->tcp, buf, (uint32_t)len, io->timeout_ms), 0);
}
