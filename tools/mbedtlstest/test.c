#include "banana_platform.h"
#include "banana_transport.h"
#include "banana_ca.h"
#include "tls.h"
#include "net.h"
#include "rtc.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/platform_util.h"
#include "test_certs.h"

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
uint32_t timer_ms(void) { return 1000u; }
void klog(const char* fmt, ...) { (void)fmt; }
const char* net_strerror(int err) {
    switch (err) {
    case NET_ERR_TIMEOUT: return "timeout";
    case NET_ERR_RESET: return "reset";
    case NET_ERR_CLOSED: return "closed";
    case NET_ERR_CERT: return "certificate verification failed";
    case NET_ERR_CLOCK: return "system clock not set";
    default: return "error";
    }
}
/* Use the real civil-date conversion functions, extracted by run.sh. */
int tcp_send(tcp_conn_t* c, const void* buf, uint32_t n, uint32_t timeout) {
    (void)c; (void)buf; (void)timeout;
    requested_len = n;
    return io_result;
}
int tcp_recv(tcp_conn_t* c, void* buf, uint32_t n, uint32_t timeout) {
    return tcp_send(c, buf, n, timeout);
}

/* Mock CA bundle implementation for host harness */
static const char* mock_ca_bundle_data = (const char*)cert_ca_pem;
static uint32_t mock_ca_bundle_len = sizeof(cert_ca_pem);
static int mock_ca_bundle_inited = 0;

void certs_init(void) {
    mock_ca_bundle_inited = 1;
}

const char* certs_load_ca_bundle(uint32_t* len) {
    if (!len) return NULL;
    *len = mock_ca_bundle_len;
    return mock_ca_bundle_data;
}

void certs_release_ca_bundle(const char* buf) {
    (void)buf;
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

    /* Test CA initialization and configuration via banana_tls_configure_client_auth */
    banana_tls_ca_init_fs();
    CHECK(mock_ca_bundle_inited == 1);
    CHECK(banana_tls_configure_client_auth(&config, "example.test") == 0);
    CHECK(config.MBEDTLS_PRIVATE(authmode) == MBEDTLS_SSL_VERIFY_REQUIRED);
    CHECK(config.MBEDTLS_PRIVATE(f_vrfy) == banana_tls_verify_cert_callback);

    mbedtls_ssl_conf_rng(&config, mbedtls_hmac_drbg_random, &rng);
    CHECK(mbedtls_ssl_setup(&ssl, &config) == 0);
    CHECK(mbedtls_ssl_set_hostname(&ssl, "example.test") == 0);
    mbedtls_ssl_set_bio(&ssl, &io, banana_tls_send, banana_tls_recv, NULL);
    io_result = NET_ERR_RESET;
    CHECK(mbedtls_ssl_handshake(&ssl) == MBEDTLS_ERR_NET_CONN_RESET);
    CHECK(io.last_net_error == NET_ERR_RESET);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&config);

    /* ── Negative verification matrix tests ── */
    mbedtls_x509_crt crt;
    uint32_t flags = 0;

    /* 1. Valid certificate */
    mbedtls_x509_crt_init(&crt);
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_valid_der, cert_valid_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) == 0);
    CHECK(flags == 0);
    mbedtls_x509_crt_free(&crt);

    /* 2. Expired certificate */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_expired_der, cert_expired_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_EXPIRED) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 3. Future certificate */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_future_der, cert_future_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_FUTURE) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 4. Bad hostname (CN mismatch) */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_badname_der, cert_badname_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_CN_MISMATCH) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 5. Unknown CA */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_unknownca_der, cert_unknownca_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 6. Invalid signature */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_invalidsig_der, cert_invalidsig_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 7. Bad key usage */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_badusage_der, cert_badusage_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_KEY_USAGE) != 0);
    mbedtls_x509_crt_free(&crt);

    /* 8. Clock change simulation: verify clock tampering does not bypass validation */
    clock_utc = 1767225600u; /* Fast forward to 2026: cert_valid_der is still valid (2020..2030) */
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_valid_der, cert_valid_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) == 0);
    CHECK(flags == 0);
    mbedtls_x509_crt_free(&crt);

    /* Clock shifted past expiration (2035) -> must fail with expired */
    clock_utc = 2051222400u;
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_valid_der, cert_valid_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    CHECK((flags & MBEDTLS_X509_BADCERT_EXPIRED) != 0);
    mbedtls_x509_crt_free(&crt);

    /* Clock zeroed / invalid -> fails gracefully, verification never disabled */
    clock_utc = 0;
    mbedtls_x509_crt_init(&crt);
    flags = 0;
    CHECK(mbedtls_x509_crt_parse_der(&crt, cert_valid_der, cert_valid_der_len) == 0);
    CHECK(banana_tls_verify_chain(&crt, "example.test", &flags) != 0);
    mbedtls_x509_crt_free(&crt);

    /* Restore clock */
    clock_utc = 1709251200u;

    /* ── tls_connect and error mapping verification ── */
    {
        int err = 0;
        char errmsg[128] = "";

        /* Null args -> NET_ERR_PROTO */
        CHECK(tls_connect(NULL, "example.test", 0, 1000, &err, errmsg, sizeof(errmsg)) == NULL);
        CHECK(err == NET_ERR_PROTO);

        /* Transport reset during handshake -> NET_ERR_RESET */
        io_result = NET_ERR_RESET;
        CHECK(tls_connect((tcp_conn_t*)1, "example.test", 0, 1000, &err, errmsg, sizeof(errmsg)) == NULL);
        CHECK(err == NET_ERR_RESET);

        /* Transport timeout -> NET_ERR_TIMEOUT */
        io_result = NET_ERR_TIMEOUT;
        CHECK(tls_connect((tcp_conn_t*)1, "example.test", 0, 1000, &err, errmsg, sizeof(errmsg)) == NULL);
        CHECK(err == NET_ERR_TIMEOUT);

        /* Error mapping tests */
        {
            int mapped_err = 0;
            char msg[128] = "";

            /* 1. Expired certificate mapping */
            extern void map_tls_error(int ret, uint32_t vflags, int last_net_err,
                                      int* out_err, char* errmsg, uint32_t errmsg_len);
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_EXPIRED, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CERT);
            CHECK(strstr(msg, "expired") != NULL);

            /* 2. Future certificate mapping */
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_FUTURE, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CERT);
            CHECK(strstr(msg, "not yet valid") != NULL);

            /* 3. Bad hostname (CN mismatch) mapping */
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_CN_MISMATCH, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CERT);
            CHECK(strstr(msg, "hostname mismatch") != NULL);

            /* 4. Unknown CA mapping */
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_NOT_TRUSTED, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CERT);
            CHECK(strstr(msg, "untrusted") != NULL);

            /* 5. Bad key usage mapping */
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_KEY_USAGE, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CERT);
            CHECK(strstr(msg, "key usage") != NULL);

            /* 6. System clock not set (clock_utc == 0) mapping */
            clock_utc = 0;
            map_tls_error(MBEDTLS_ERR_X509_CERT_VERIFY_FAILED, MBEDTLS_X509_BADCERT_EXPIRED, 0,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_CLOCK);
            CHECK(strstr(msg, "system clock not set") != NULL);
            clock_utc = 1709251200u;

            /* 7. Connection reset mapping */
            map_tls_error(MBEDTLS_ERR_NET_CONN_RESET, 0, NET_ERR_RESET,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_RESET);

            /* 8. Timeout mapping */
            map_tls_error(MBEDTLS_ERR_SSL_TIMEOUT, 0, NET_ERR_TIMEOUT,
                          &mapped_err, msg, sizeof(msg));
            CHECK(mapped_err == NET_ERR_TIMEOUT);
        }
    }

    mbedtls_hmac_drbg_free(&rng);
    mbedtls_entropy_free(&entropy);
    puts("PASS: allocator, UTC/date failure, DRBG seed failure, TCP mapping, TLS setup, and X.509 verification matrix");
    return 0;
}
