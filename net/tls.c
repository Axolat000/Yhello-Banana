#include "tls.h"
#include "banana_platform.h"
#include "banana_transport.h"
#include "banana_ca.h"
#include "rtc.h"
#include "timer.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/x509_crt.h"
#include "mbedtls/error.h"
#include "mbedtls/net_sockets.h"
#include "kstring.h"
#include "kheap.h"
#include "serial.h"

struct tls_conn {
    mbedtls_ssl_context       ssl;
    mbedtls_ssl_config        conf;
    mbedtls_entropy_context   entropy;
    mbedtls_hmac_drbg_context drbg;
    banana_tls_transport      io;
    int                       eof;
};

const char* tls_cipher_name(const tls_conn_t* t) {
    if (!t) return "NONE";
    const char* s = mbedtls_ssl_get_ciphersuite(&t->ssl);
    return s ? s : "UNKNOWN";
}

void map_tls_error(int ret, uint32_t vflags, int last_net_err,
                   int* out_err, char* errmsg, uint32_t errmsg_len) {
    int err = NET_ERR_PROTO;
    const char* desc = "handshake failed";

    /* Check transport errors first if handshake was interrupted at network layer */
    if (ret == MBEDTLS_ERR_SSL_TIMEOUT || last_net_err == NET_ERR_TIMEOUT) {
        err = NET_ERR_TIMEOUT;
        desc = "TLS operation timed out";
    } else if (ret == MBEDTLS_ERR_NET_CONN_RESET || last_net_err == NET_ERR_RESET || last_net_err == NET_ERR_CLOSED) {
        err = (last_net_err == NET_ERR_CLOSED) ? NET_ERR_CLOSED : NET_ERR_RESET;
        desc = "connection reset or closed by peer";
    } else if (last_net_err == NET_ERR_INTR) {
        err = NET_ERR_INTR;
        desc = "interrupted";
    } else if (last_net_err < 0) {
        err = last_net_err;
        desc = net_strerror(last_net_err);
    } else if (vflags != 0 && vflags != 0xFFFFFFFFu) {
        if (vflags & (MBEDTLS_X509_BADCERT_EXPIRED | MBEDTLS_X509_BADCERT_FUTURE)) {
            uint32_t utc = rtc_now_utc();
            if (utc == 0) {
                err = NET_ERR_CLOCK;
                desc = "system clock not set (cannot verify certificate dates)";
            } else {
                err = NET_ERR_CERT;
                desc = (vflags & MBEDTLS_X509_BADCERT_EXPIRED) ? "certificate has expired"
                                                              : "certificate is not yet valid";
            }
        } else if (vflags & MBEDTLS_X509_BADCERT_CN_MISMATCH) {
            err = NET_ERR_CERT;
            desc = "certificate hostname mismatch";
        } else if (vflags & MBEDTLS_X509_BADCERT_NOT_TRUSTED) {
            err = NET_ERR_CERT;
            desc = "certificate authority unknown or untrusted";
        } else if (vflags & (MBEDTLS_X509_BADCERT_KEY_USAGE | MBEDTLS_X509_BADCERT_EXT_KEY_USAGE)) {
            err = NET_ERR_CERT;
            desc = "certificate key usage invalid for server authentication";
        } else {
            err = NET_ERR_CERT;
            desc = "certificate verification failed";
        }
    } else if (ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED) {
        err = NET_ERR_CERT;
        desc = "certificate verification failed";
    } else if (ret == MBEDTLS_ERR_SSL_ALLOC_FAILED) {
        err = NET_ERR_NOMEM;
        desc = "out of memory";
    } else if (ret == MBEDTLS_ERR_ENTROPY_SOURCE_FAILED || ret == MBEDTLS_ERR_HMAC_DRBG_ENTROPY_SOURCE_FAILED) {
        err = NET_ERR_PROTO;
        desc = "cryptographic entropy source unavailable";
    } else {
        err = NET_ERR_PROTO;
        desc = "protocol error";
    }

    if (out_err) *out_err = err;
    if (errmsg && errmsg_len > 0) {
        ksnprintf(errmsg, errmsg_len, "%s", desc);
    }
}

tls_conn_t* tls_connect(tcp_conn_t* tcp, const char* server_name, int ciphers, uint32_t timeout_ms,
                        int* err, char* errmsg, uint32_t errmsg_len) {
    (void)ciphers;
    if (errmsg && errmsg_len > 0) errmsg[0] = '\0';
    if (err) *err = NET_OK;

    if (!tcp || !server_name || !*server_name) {
        if (err) *err = NET_ERR_PROTO;
        if (errmsg && errmsg_len > 0) kstrlcpy(errmsg, "invalid parameters", errmsg_len);
        return NULL;
    }

    /* Ensure root CA bundle is initialized in filesystem */
    banana_tls_ca_init_fs();

    tls_conn_t* t = (tls_conn_t*)banana_tls_calloc(1, sizeof(tls_conn_t));
    if (!t) {
        if (err) *err = NET_ERR_NOMEM;
        if (errmsg && errmsg_len > 0) kstrlcpy(errmsg, "out of memory", errmsg_len);
        return NULL;
    }

    mbedtls_ssl_init(&t->ssl);
    mbedtls_ssl_config_init(&t->conf);
    mbedtls_entropy_init(&t->entropy);
    mbedtls_hmac_drbg_init(&t->drbg);

    t->io.tcp = tcp;
    t->io.timeout_ms = timeout_ms;
    t->io.last_net_error = 0;

    int r = mbedtls_ssl_config_defaults(&t->conf, MBEDTLS_SSL_IS_CLIENT,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT);
    if (r != 0) {
        map_tls_error(r, 0, 0, err, errmsg, errmsg_len);
        goto fail;
    }

    /* Seed HMAC-DRBG using platform entropy source (banana_tls_seed) */
    r = mbedtls_hmac_drbg_seed(&t->drbg, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                               mbedtls_entropy_func, &t->entropy,
                               (const unsigned char*)server_name, strlen(server_name));
    if (r != 0) {
        map_tls_error(r, 0, 0, err, errmsg, errmsg_len);
        goto fail;
    }
    mbedtls_ssl_conf_rng(&t->conf, mbedtls_hmac_drbg_random, &t->drbg);

    /* Configure mandatory client authentication and trusted CA chain */
    r = banana_tls_configure_client_auth(&t->conf, server_name);
    if (r != 0) {
        if (err) *err = NET_ERR_CERT;
        if (errmsg && errmsg_len > 0) kstrlcpy(errmsg, "trusted root CA store unavailable", errmsg_len);
        goto fail;
    }

    r = mbedtls_ssl_setup(&t->ssl, &t->conf);
    if (r != 0) {
        map_tls_error(r, 0, 0, err, errmsg, errmsg_len);
        goto fail;
    }

    /* Set Server Name Indication (SNI) and hostname for certificate validation */
    r = mbedtls_ssl_set_hostname(&t->ssl, server_name);
    if (r != 0) {
        map_tls_error(r, 0, 0, err, errmsg, errmsg_len);
        goto fail;
    }

    /* Attach BIO callbacks */
    mbedtls_ssl_set_bio(&t->ssl, &t->io, banana_tls_send, banana_tls_recv, NULL);

    /* Perform TLS handshake with deadline enforcement */
    uint32_t start_ms = timer_ms();
    while ((r = mbedtls_ssl_handshake(&t->ssl)) != 0) {
        if (r != MBEDTLS_ERR_SSL_WANT_READ && r != MBEDTLS_ERR_SSL_WANT_WRITE) {
            break;
        }
        if (timeout_ms && (timer_ms() - start_ms >= timeout_ms)) {
            r = MBEDTLS_ERR_SSL_TIMEOUT;
            break;
        }
    }

    if (r != 0) {
        uint32_t vflags = mbedtls_ssl_get_verify_result(&t->ssl);
        map_tls_error(r, vflags, t->io.last_net_error, err, errmsg, errmsg_len);
        klog("tls: handshake with %s failed: %s\n", server_name, (errmsg && errmsg[0]) ? errmsg : "?");
        goto fail;
    }

    return t;

fail:
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_hmac_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    banana_tls_free(t);
    return NULL;
}

int tls_read(tls_conn_t* t, void* buf, uint32_t max, uint32_t timeout_ms) {
    if (!t || !buf) return NET_ERR_PROTO;
    if (t->eof) return 0;

    t->io.timeout_ms = timeout_ms;
    int r = mbedtls_ssl_read(&t->ssl, (unsigned char*)buf, max);
    if (r > 0) return r;
    if (r == 0 || r == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        t->eof = 1;
        return 0;
    }
    if (r == MBEDTLS_ERR_SSL_TIMEOUT || t->io.last_net_error == NET_ERR_TIMEOUT) {
        return NET_ERR_TIMEOUT;
    }
    if (r == MBEDTLS_ERR_NET_CONN_RESET || t->io.last_net_error == NET_ERR_RESET || t->io.last_net_error == NET_ERR_CLOSED) {
        t->eof = 1;
        return 0;
    }
    if (t->io.last_net_error < 0) return t->io.last_net_error;
    return NET_ERR_PROTO;
}

int tls_write(tls_conn_t* t, const void* data, uint32_t len, uint32_t timeout_ms) {
    if (!t || !data) return NET_ERR_PROTO;

    t->io.timeout_ms = timeout_ms;
    uint32_t written = 0;
    while (written < len) {
        int r = mbedtls_ssl_write(&t->ssl, (const unsigned char*)data + written, len - written);
        if (r > 0) {
            written += (uint32_t)r;
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_WANT_READ || r == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (r == MBEDTLS_ERR_SSL_TIMEOUT || t->io.last_net_error == NET_ERR_TIMEOUT) {
            return NET_ERR_TIMEOUT;
        }
        if (r == MBEDTLS_ERR_NET_CONN_RESET || t->io.last_net_error == NET_ERR_RESET) {
            return NET_ERR_RESET;
        }
        if (t->io.last_net_error < 0) return t->io.last_net_error;
        return NET_ERR_PROTO;
    }
    return (int)len;
}

void tls_close(tls_conn_t* t) {
    if (!t) return;
    mbedtls_ssl_close_notify(&t->ssl);
    mbedtls_ssl_free(&t->ssl);
    mbedtls_ssl_config_free(&t->conf);
    mbedtls_hmac_drbg_free(&t->drbg);
    mbedtls_entropy_free(&t->entropy);
    banana_tls_free(t);
}
