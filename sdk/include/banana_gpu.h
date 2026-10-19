/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef BANANA_GPU_H
#define BANANA_GPU_H
/* Fixed-width request wire format for the optional virtio/virgl backend.
 * No host addresses or device descriptors cross the application boundary. */
enum {
    BGPU_INFO, BGPU_CAPS, BGPU_CONTEXT_CREATE, BGPU_CONTEXT_DESTROY,
    BGPU_RESOURCE_CREATE, BGPU_RESOURCE_DESTROY, BGPU_ATTACH,
    BGPU_SUBMIT, BGPU_TRANSFER_PUT, BGPU_TRANSFER_GET, BGPU_WRITE, BGPU_READ
};
typedef struct {
    unsigned int context, resource;
    unsigned long long offset;
    unsigned int p[16];
} banana_gpu_request_t;
typedef struct {
    unsigned int version, capset, cap_version, cap_bytes;
    unsigned int max_resource_bytes, max_submit_bytes, reserved[2];
} banana_gpu_info_t;
#endif
