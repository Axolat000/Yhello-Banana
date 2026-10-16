#ifndef VIRTIO_PCI_H
#define VIRTIO_PCI_H

#include "types.h"
#include "pci.h"

/*
 * The virtio PCI transport, both kinds: the legacy one (an I/O port block,
 * "transitional" devices: VirtualBox, older QEMU defaults) and the modern
 * one (virtio 1.x: registers found through PCI capabilities - QEMU with
 * disable-legacy=on or behind PCIe ports, crosvm, cloud hypervisors).
 * A driver asks for the device's features, sets up its queues in memory
 * it gives (VIO_QBYTES, page aligned) and then talks to the rings itself.
 */

#define VIO_S_ACK         1
#define VIO_S_DRIVER      2
#define VIO_S_DRIVER_OK   4
#define VIO_S_FEATURES_OK 8
#define VIO_S_FAILED      128

#define VIO_QMAX   1024
#define VIO_QBYTES (32u * 1024u)        /* a 1024-entry queue: descriptors, avail, used (page aligned) */

typedef struct {
    int      modern;
    uint16_t io;                        /* legacy: the port block */
    volatile uint8_t* common;           /* modern: the configuration structures */
    volatile uint8_t* notify;
    volatile uint8_t* isr;
    volatile uint8_t* device;
    uint32_t notify_mul;
    uint16_t notify_off[32];            /* (per queue: virtio-serial has 2 per port) */
} vio_t;

typedef struct __attribute__((packed)) { uint64_t addr; uint32_t len; uint16_t flags, next; } vio_desc_t;

/* finds the interface (modern if the device has it), resets the device and
 * acknowledges it; 0 = ok */
int      vio_open(vio_t* v, const pci_dev_t* d);
/* the device's first 32 feature bits; then the ones the driver takes
 * (modern devices also get VERSION_1); 0 = accepted */
uint32_t vio_features(vio_t* v);
int      vio_set_features(vio_t* v, uint32_t want);
/* queue qi in mem (VIO_QBYTES, zeroed here); its size (<= max), or 0 */
uint16_t vio_queue(vio_t* v, int qi, uint8_t* mem, uint16_t max,
                   vio_desc_t** desc, volatile uint16_t** avail, volatile uint8_t** used);
void     vio_driver_ok(vio_t* v);
void     vio_failed(vio_t* v);
void     vio_notify(vio_t* v, int qi);
uint8_t  vio_isr(vio_t* v);             /* reading it acknowledges the interrupt */
/* the device-specific configuration */
uint8_t  vio_cfg8(vio_t* v, int off);
uint32_t vio_cfg32(vio_t* v, int off);

#endif
