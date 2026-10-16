#ifndef VIRTIO_SERIAL_H
#define VIRTIO_SERIAL_H

#include "types.h"

/*
 * virtio-serial (virtio-console with several ports): the channel QEMU,
 * KVM and Proxmox give their guest agent (org.qemu.guest_agent.0) and
 * SPICE's agent. kernel/virtio_serial.c finds the device, learns its ports
 * by name from the host, and moves bytes both ways; the ports are polled
 * by a task of their own (every 20 ms) - no interrupt.
 */

#define VSER_MAX_PORTS 15            /* port 0 + 14 more: 32 queues */

/* data from the host for a port: called from the virtio-serial task */
typedef void (*vser_rx_t)(int port, const uint8_t* data, uint32_t len, void* ctx);

/* finds the device and starts its task; 1 if there is one */
int  vser_init(void);
int  vser_present(void);
/* the port with this name ("org.qemu.guest_agent.0"), or -1 (not (yet) known) */
int  vser_find(const char* name);
/* data for the port is handed to rx from now on, and the host is told the
 * guest side is open */
int  vser_open(int port, vser_rx_t rx, void* ctx);
/* to the host: 0, or -1 (no such port, the device stopped) */
int  vser_write(int port, const void* data, uint32_t len);
/* 1 while the host side of the port is connected (a client on the socket) */
int  vser_host_connected(int port);
/* "port 1: org.qemu.guest_agent.0 (host connected)" lines, for vmtools */
void vser_describe(void (*out)(const char* line));

#endif
