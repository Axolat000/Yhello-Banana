#include "banana_ca.h"
#include "../../kernel/certs.h"
#include "mbedtls/error.h"
#include "mbedtls/oid.h"
#include <string.h>

static mbedtls_x509_crt g_ca_chain;
static int g_ca_chain_inited = 0;
static int g_ca_chain_loaded = 0;

void banana_tls_ca_init_fs(void) {
    certs_init();
}

mbedtls_x509_crt* banana_tls_get_ca_chain(void) {
    if (g_ca_chain_loaded) {
        return &g_ca_chain;
    }

    if (!g_ca_chain_inited) {
        mbedtls_x509_crt_init(&g_ca_chain);
        g_ca_chain_inited = 1;
    }

    uint32_t len = 0;
    const char* bundle = certs_load_ca_bundle(&len);
    if (!bundle || len == 0) {
        return NULL;
    }

    /* mbedtls_x509_crt_parse requires trailing null byte for PEM */
    char* copy = (char*)banana_tls_calloc(1, len + 1);
    if (!copy) {
        certs_release_ca_bundle(bundle);
        return NULL;
    }
    memcpy(copy, bundle, len);
    copy[len] = '\0';
    certs_release_ca_bundle(bundle);

    int ret = mbedtls_x509_crt_parse(&g_ca_chain, (const unsigned char*)copy, (size_t)len + 1);
    banana_tls_free(copy);

    if (ret < 0) {
        mbedtls_x509_crt_free(&g_ca_chain);
        g_ca_chain_inited = 0;
        return NULL;
    }

    g_ca_chain_loaded = 1;
    return &g_ca_chain;
}

int banana_tls_verify_cert_callback(void* data, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    (void)data;
    if (!crt || !flags) return 0;

    /* For leaf certificates (depth == 0), enforce valid server key usages */
    if (depth == 0) {
        /* If keyUsage extension is present, ensure digitalSignature and/or keyEncipherment is set */
        if (mbedtls_x509_crt_has_ext_type(crt, MBEDTLS_X509_EXT_KEY_USAGE)) {
            if (mbedtls_x509_crt_check_key_usage(crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE) != 0 &&
                mbedtls_x509_crt_check_key_usage(crt, MBEDTLS_X509_KU_KEY_ENCIPHERMENT) != 0) {
                *flags |= MBEDTLS_X509_BADCERT_KEY_USAGE;
            }
        }

        /* If extendedKeyUsage is present, ensure serverAuth or anyExtendedKeyUsage is present */
        if (mbedtls_x509_crt_has_ext_type(crt, MBEDTLS_X509_EXT_EXTENDED_KEY_USAGE)) {
            if (mbedtls_x509_crt_check_extended_key_usage(crt, MBEDTLS_OID_SERVER_AUTH,
                                                          MBEDTLS_OID_SIZE(MBEDTLS_OID_SERVER_AUTH)) != 0 &&
                mbedtls_x509_crt_check_extended_key_usage(crt, MBEDTLS_OID_ANY_EXTENDED_KEY_USAGE,
                                                          MBEDTLS_OID_SIZE(MBEDTLS_OID_ANY_EXTENDED_KEY_USAGE)) != 0) {
                *flags |= MBEDTLS_X509_BADCERT_EXT_KEY_USAGE;
            }
        }
    }

    return 0;
}

int banana_tls_configure_client_auth(mbedtls_ssl_config* conf, const char* hostname) {
    if (!conf) return -1;

    mbedtls_x509_crt* ca = banana_tls_get_ca_chain();
    if (!ca) return -2;

    /* Enforce mandatory certificate verification */
    mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(conf, ca, NULL);
    mbedtls_ssl_conf_verify(conf, banana_tls_verify_cert_callback, NULL);

    (void)hostname;
    return 0;
}

int banana_tls_verify_chain(mbedtls_x509_crt* crt, const char* hostname, uint32_t* flags) {
    if (!flags) return -1;
    *flags = 0;
    if (!crt) {
        *flags = MBEDTLS_X509_BADCERT_MISSING;
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }

    mbedtls_x509_crt* ca = banana_tls_get_ca_chain();
    if (!ca) {
        *flags = MBEDTLS_X509_BADCERT_NOT_TRUSTED;
        return MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
    }

    int ret = mbedtls_x509_crt_verify(crt, ca, NULL, hostname, flags,
                                      banana_tls_verify_cert_callback, NULL);
    return ret;
}
