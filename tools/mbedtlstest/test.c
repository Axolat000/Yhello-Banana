#include "banana_platform.h"
#include "banana_transport.h"
#include "rtc.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509.h"
#include "mbedtls/platform_util.h"

extern int puts(const char* s);
#define CHECK(x) do { if (!(x)) { puts("FAIL: " #x); return 1; } } while (0)
static int seed_fails, allocation_fails, allocation_calls, io_result;
static uint32_t clock_utc = 1709251200u; /* 2024-03-01 00:00:00 UTC */
static size_t requested_len;
extern void* calloc(size_t, size_t);
extern void free(void*);
void* kzalloc(size_t n) {
    allocation_calls++;
    return allocation_fails ? NULL : calloc(1, n);
}
void kfree(void* p) { free(p); }
int banana_tls_seed(unsigned char* out, size_t len) {
    if (seed_fails) return -1;
    /* Deterministic TEST source, never linked into the kernel. */
    for (size_t i = 0; i < len; i++) out[i] = (unsigned char)i;
    return 0;
}
uint32_t rtc_now_utc(void) { return clock_utc; }
uint64_t timer_ms64(void) { return 0x100000001ull; }
/* Use the real civil-date conversion functions, extracted by run.sh. */
int tcp_send(tcp_conn_t* c, const void* buf, uint32_t n, uint32_t timeout) {
    (void)c; (void)buf; (void)timeout;
    requested_len = n;
    return io_result;
}
int tcp_recv(tcp_conn_t* c, void* buf, uint32_t n, uint32_t timeout) {
    return tcp_send(c, buf, n, timeout);
}

int main(void) {
    int before = allocation_calls;
    CHECK(!banana_tls_calloc(__SIZE_MAX__, 2));
    CHECK(!banana_tls_calloc(0, 3));
    CHECK(allocation_calls == before);
    allocation_fails = 1;
    CHECK(!banana_tls_calloc(8, 8));
    allocation_fails = 0;
    unsigned char* mem = banana_tls_calloc(8, 8);
    CHECK(mem);
    for (int i = 0; i < 64; i++) CHECK(mem[i] == 0);
    banana_tls_free(mem);
    CHECK(banana_tls_time(NULL) == clock_utc);
    CHECK(mbedtls_ms_time() == 0x100000001ull);
    struct tm tm;
    int64_t time = clock_utc;
    CHECK(mbedtls_platform_gmtime_r(&time, &tm));
    CHECK(tm.tm_year == 124 && tm.tm_mon == 2 && tm.tm_mday == 1);
    CHECK(tm.tm_hour == 0 && tm.tm_yday == 60 && tm.tm_wday == 5);
    time = 0x100000000ll;
    CHECK(!mbedtls_platform_gmtime_r(&time, &tm));
    mbedtls_x509_time expiry = {2025, 1, 1, 0, 0, 0};
    CHECK(!mbedtls_x509_time_is_past(&expiry));
    clock_utc = 0;
    CHECK(banana_tls_time(&time) == -1 && time == -1);
    CHECK(mbedtls_x509_time_is_past(&expiry));
    CHECK(mbedtls_x509_time_is_future(&expiry));
    clock_utc = 1767225600u; /* 2026-01-01 */
    CHECK(mbedtls_x509_time_is_past(&expiry));
    clock_utc = 1709251200u;
    CHECK(!mbedtls_x509_time_is_past(&expiry));

    mbedtls_entropy_context entropy;
    mbedtls_hmac_drbg_context rng;
    mbedtls_entropy_init(&entropy);
    mbedtls_hmac_drbg_init(&rng);
    seed_fails = 1;
    CHECK(mbedtls_hmac_drbg_seed(&rng, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
          mbedtls_entropy_func, &entropy, NULL, 0) != 0);
    mbedtls_hmac_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    seed_fails = 0;
    mbedtls_entropy_init(&entropy);
    mbedtls_hmac_drbg_init(&rng);
    CHECK(mbedtls_hmac_drbg_seed(&rng, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
          mbedtls_entropy_func, &entropy, NULL, 0) == 0);
    unsigned char buf[64];
    CHECK(mbedtls_hmac_drbg_random(&rng, buf, sizeof(buf)) == 0);

    banana_tls_transport io = {(tcp_conn_t*)1, 100, 0};
    io_result = 7;
    CHECK(banana_tls_send(&io, buf, 7) == 7 && !io.last_net_error);
    CHECK(banana_tls_recv(&io, buf, 12) == 7);
    io_result = 0;
    CHECK(banana_tls_recv(&io, buf, 12) == 0);
    io_result = NET_ERR_TIMEOUT;
    CHECK(banana_tls_recv(&io, buf, 12) == MBEDTLS_ERR_SSL_TIMEOUT);
    CHECK(io.last_net_error == NET_ERR_TIMEOUT);
    io_result = NET_ERR_RESET;
    CHECK(banana_tls_send(&io, buf, 12) == MBEDTLS_ERR_NET_CONN_RESET);
    io_result = NET_ERR_INTR;
    CHECK(banana_tls_recv(&io, buf, 12) == MBEDTLS_ERR_NET_RECV_FAILED);
    CHECK(io.last_net_error == NET_ERR_INTR);
    CHECK(banana_tls_send(&io, buf, __SIZE_MAX__) == MBEDTLS_ERR_NET_SEND_FAILED);
    CHECK(requested_len == 0x7fffffffu);
    CHECK(banana_tls_recv(NULL, buf, 1) == MBEDTLS_ERR_NET_INVALID_CONTEXT);

    mbedtls_ssl_config config;
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_init(&ssl);
    CHECK(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT,
          MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT) == 0);
    CHECK(config.MBEDTLS_PRIVATE(authmode) == MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_rng(&config, mbedtls_hmac_drbg_random, &rng);
    CHECK(mbedtls_ssl_setup(&ssl, &config) == 0);
    CHECK(mbedtls_ssl_set_hostname(&ssl, "example.test") == 0);
    mbedtls_ssl_set_bio(&ssl, &io, banana_tls_send, banana_tls_recv, NULL);
    io_result = NET_ERR_RESET;
    CHECK(mbedtls_ssl_handshake(&ssl) == MBEDTLS_ERR_NET_CONN_RESET);
    CHECK(io.last_net_error == NET_ERR_RESET);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);
    mbedtls_hmac_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    puts("PASS: allocator, UTC/date failure, DRBG seed failure, TCP mapping and TLS setup");
    return 0;
}
