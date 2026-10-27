/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef BANANA_DRIVER_EXT_H
#define BANANA_DRIVER_EXT_H
/* Optional, size/version-negotiated extensions to the original Driver Kit.
 * Include banana_driver.h first. All calls/callbacks are TASK context, not IRQ.
 * Drivers remain trusted ring-0 code; this interface is not a sandbox. */
#define BDRV_EXT_NET      0x3154454eu
#define BDRV_EXT_WIFI     0x31494657u
#define BDRV_EXT_USB      0x31425355u
#define BDRV_EXT_POWER    0x31525750u
#define BDRV_EXT_FIRMWARE 0x31575246u
#define BDRV_EXT_VERSION 1u
typedef uint32_t bdrv_handle_t; /* 0: invalid; never assume it is a pointer */
typedef struct { uint32_t version,size; } bdrv_ext_header_t;

/* Ethernet receives complete frames, without FCS, 14..1514 bytes. Receive
 * only from the device's poll callback. TX callbacks copy/queue before return. */
typedef struct {
    uint32_t size;
    const char* name;const char* model;void* ctx;
    uint8_t mac[6],is_usb,reserved;
    int (*send)(void*,const void*,uint32_t);
    int (*poll)(void*);
    int (*link_up)(void*);
    void (*set_multicast)(void*,int);
} bdrv_net_device_t;
typedef struct {
    uint32_t version,size;
    bdrv_handle_t (*register_device)(const bdrv_net_device_t*);
    void (*unregister_device)(bdrv_handle_t);
    int (*receive)(bdrv_handle_t,const void*,uint32_t);
} bdrv_net_api_t;

/* Native station core supplies association, WPA2/CCMP and Ethernet bridging.
 * Raw 802.11 RX excludes FCS. Raw TX must preserve the software encryption.
 * Call receive only from poll. set_station(NULL,0) clears station context.
 * A radio registers only after hardware/firmware/calibration/MAC are ready.
 * Channel support currently remains 2.4GHz 1..11, legacy rates, WPA2/open. */
typedef struct {
    uint32_t size;
    const char* name;const char* model;void* ctx;
    uint8_t mac[6],is_usb,reserved;
    int (*poll)(void*);
    int (*send)(void*,const uint8_t*,uint32_t);
    int (*set_channel)(void*,unsigned);
    int (*set_station)(void*,const uint8_t*,unsigned);
} bdrv_wifi_radio_t;
typedef struct {
    uint32_t version,size;
    bdrv_handle_t (*register_radio)(const bdrv_wifi_radio_t*);
    void (*unregister_radio)(bdrv_handle_t);
    int (*receive)(bdrv_handle_t,const uint8_t*,uint32_t,int rssi_dbm);
} bdrv_wifi_api_t;

/* USB modules are tried after built-in drivers, never steal a bound device.
 * Probe returns configuration index, attach is called after SET_CONFIGURATION.
 * All descriptors and handles are valid only for this attachment.
 * A stale handle is rejected even if USB reuses the same physical slot. */
typedef struct { uint16_t vendor,product; } bdrv_usb_id_t;
typedef struct {
    uint32_t size;
    uint16_t vendor,product,bcd_usb,bcd_device;
    uint8_t device_class,subclass,protocol,speed,configurations,address;
    uint16_t reserved;
    char manufacturer[40],product_name[48];
} bdrv_usb_info_t;
typedef struct __attribute__((packed)) {
    uint8_t length,type,address,attributes;
    uint16_t max_packet;
    uint8_t interval;
} bdrv_usb_endpoint_t;
typedef struct {
    uint32_t size;
    const char* name;const bdrv_usb_id_t* ids;unsigned nids;void* ctx;
    int (*probe)(void*,bdrv_handle_t);
    int (*attach)(void*,bdrv_handle_t,void** device_ctx);
    void (*poll)(void* device_ctx);
    void (*detach)(void* device_ctx);
} bdrv_usb_driver_t;
#define BDRV_USB_PENDING 1
#define BDRV_USB_OK      0
#define BDRV_USB_STALL  (-2)
#define BDRV_USB_ERROR  (-3)
#define BDRV_USB_GONE   (-4)
typedef struct {
    uint32_t version,size;
    int (*register_driver)(const bdrv_usb_driver_t*);
    int (*info)(bdrv_handle_t,bdrv_usb_info_t*);
    /* NULL/0 returns length; otherwise capacity must fit the whole descriptor. */
    int (*configuration)(bdrv_handle_t,unsigned,void*,unsigned capacity);
    int (*control)(bdrv_handle_t,uint8_t reqtype,uint8_t request,uint16_t value,
                   uint16_t index,void* data,uint16_t length);
    int (*open_endpoint)(bdrv_handle_t,const bdrv_usb_endpoint_t*);
    void* (*dma_alloc)(uint32_t bytes);
    bdrv_handle_t (*transfer_create)(bdrv_handle_t,uint8_t endpoint,void* dma_buffer,uint32_t bytes);
    int (*transfer_submit)(bdrv_handle_t);
    int (*transfer_status)(bdrv_handle_t,uint32_t* actual);
    /* Fails while pending. No cancellation/DMA-quiesce facility yet:
     * do not free a pending transfer/buffer, including after detach/timeout. */
    int (*transfer_release)(bdrv_handle_t);
} bdrv_usb_api_t;

#define BDRV_POWER_BATTERY 1u
#define BDRV_POWER_AC      2u
#define BDRV_POWER_LID     4u
#define BDRV_POWER_LIGHT   8u
#define BDRV_POWER_BATTERIES 4
typedef struct {
    char model[48],serial[32],chemistry[24];
    int32_t present,valid,percent,minutes,unit; /* unit 0:mWh/mW, 1:mAh/mA */
    uint32_t state,design,full,remaining,rate,millivolts; /* ACPI _BST state bits */
} bdrv_battery_t;
typedef struct {
    uint32_t size,flags,updated_ms;
    int32_t ac_online,lid_open,brightness;
    uint32_t battery_count;
    bdrv_battery_t batteries[BDRV_POWER_BATTERIES];
} bdrv_power_sample_t;
typedef struct {
    uint32_t size;
    const char* name;void* ctx;
    /* Native worker calls this every ~2s. No samples are evaluated by the UI.
     * 0: complete sample, negative: withdraw cached provider telemetry. */
    int (*sample)(void*,bdrv_power_sample_t*);
    int (*backlight)(void*,int percent); /* optional, 0..100; returns actual level */
} bdrv_power_provider_t;
typedef struct {
    uint32_t size;
    uint16_t ucsi_version,pd_version;
    uint32_t port_count,ready,valid,connected,source,mode,charging,contract_valid;
    uint32_t millivolts,milliamps,milliwatts,rdo,last_error,ppm_error;
} bdrv_usbc_status_t;
typedef struct {
    uint32_t version,size;
    int (*snapshot)(bdrv_power_sample_t*);
    /* Vendor provider supplies fields absent from native ACPI.
     * Four providers maximum, callbacks/data must live until reboot. */
    bdrv_handle_t (*register_provider)(const bdrv_power_provider_t*);
    void (*unregister_provider)(bdrv_handle_t);
    int (*brightness)(int percent); /* negative: read; writes are queued */
    int (*request_power_role)(unsigned port,int source); /* firmware UCSI policy */
    int (*usbc_status)(unsigned port,bdrv_usbc_status_t*); /* one-based port */
} bdrv_power_api_t;

typedef struct {
    uint32_t version,size;
    /* Read installed firmware/package data, without writable VFS internals.
     * Absolute path, max file size 16MiB; NULL/0 queries full size.
     * Otherwise returns bytes copied from offset, or -1. */
    int (*read)(const char* path,uint32_t offset,void* out,uint32_t capacity);
} bdrv_firmware_api_t;
#endif
