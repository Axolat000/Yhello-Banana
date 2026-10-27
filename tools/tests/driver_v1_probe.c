/* SPDX-License-Identifier: BSD-2-Clause */
/* Frozen original ABI; intentionally independent of current Driver SDK headers.
 * x86 function/data pointers have the same size. The old 20 slots are retained. */
typedef __UINT32_TYPE__ u32;
typedef struct {
    u32 magic,version,size;const char* arch;const char* os;
    void (*log)(const char*,...);
    void* slots_1_to_9[9];
    void* (*alloc)(unsigned long);
    void (*free)(void*);
    void* slots_12_to_19[8];
} frozen_api_v1;
__attribute__((visibility("default"))) int _banana_driver_start(const frozen_api_v1* api) {
    if(!api || api->magic!=0x56524442u || api->version!=1 || api->size<sizeof(*api)) return -1;
    unsigned char* data=api->alloc(32);
    if(!data || data[0] || data[31]) return -1;
    data[0]=0xaa;data[31]=0x55;api->free(data);
    api->log("driver-v1: ABI compatible (%s)",api->arch);return 0;
}
