#include "certs.h"
#include "ca_bundle_data.h"
#include "fs.h"
#include "kheap.h"
#include "kstring.h"

char* stbi_zlib_decode_malloc(const char* buffer, int len, int* outlen);

void certs_init(void) {
    if (fs_find_file(CERTS_CA_BUNDLE_PATH) >= 0) return;

    int outlen = 0;
    char* data = stbi_zlib_decode_malloc((const char*)g_ca_bundle_z,
                                         (int)CA_BUNDLE_Z_LEN, &outlen);
    if (!data || (uint32_t)outlen != CA_BUNDLE_RAW_LEN) {
        if (data) kfree(data);
        return;
    }

    fs_mkdir_p("/etc/ssl/certs");
    fs_write_path(CERTS_CA_BUNDLE_PATH, data, (uint32_t)outlen);
    kfree(data);
}

const char* certs_load_ca_bundle(uint32_t* len) {
    if (!len) return NULL;
    *len = 0;

    int idx = fs_find_file(CERTS_CA_BUNDLE_PATH);
    if (idx >= 0) {
        fs_file_t* f = fs_get_file(idx);
        if (f && f->content && f->size > 0) {
            char* copy = (char*)kmalloc(f->size + 1);
            if (copy) {
                memcpy(copy, f->content, f->size);
                copy[f->size] = '\0';
                *len = f->size;
                return copy;
            }
        }
    }

    /* Fallback directly to embedded bundle */
    int outlen = 0;
    char* data = stbi_zlib_decode_malloc((const char*)g_ca_bundle_z,
                                         (int)CA_BUNDLE_Z_LEN, &outlen);
    if (data && (uint32_t)outlen == CA_BUNDLE_RAW_LEN) {
        *len = (uint32_t)outlen;
        return data;
    }
    if (data) kfree(data);
    return NULL;
}

void certs_release_ca_bundle(const char* buf) {
    if (buf) kfree((void*)buf);
}
