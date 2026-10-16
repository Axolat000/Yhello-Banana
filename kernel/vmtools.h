#ifndef VMTOOLS_H
#define VMTOOLS_H

#include "types.h"

/*
 * Guest tools for virtual machines (kernel/vmtools.c) - what VMware Tools,
 * VirtualBox's Guest Additions and the QEMU guest agent do for other
 * systems, built in:
 *
 *   all          which hypervisor this is (CPUID 0x40000000)
 *   QEMU / KVM / Proxmox
 *                the guest agent (kernel/qga.c over kernel/virtio_serial.c):
 *                addresses, clean shutdown / restart, clock, fsfreeze for
 *                backups; VMMouse (QEMU's vmport): the pointer follows the
 *                host's, no capture
 *   VMware       the backdoor: VMMouse, the host's clock, and the tools
 *                channel (TCLO / RPCI) - Shut Down / Restart Guest from the
 *                host, the guest's address and OS shown in vSphere / Workstation
 *   VirtualBox   VMMDev: mouse integration, the host's clock, the guest
 *                reported to the host
 *
 * `vmtools off` (vmtools=off in /etc/settings.conf) turns it all off from
 * the next start.
 */

/* at boot, after the network and the disks */
void vmtools_init(void);
/* "KVM", "VMware", "VirtualBox", "Hyper-V", "QEMU (TCG)"... or "" (none found) */
const char* vmtools_hypervisor(void);
/* the absolute pointer, every frame (kernel/usb.c mouse_read) */
void vmtools_poll_pointer(void);
/* Temporarily ask mouse integration for relative reports; restore on release. */
void vmtools_capture_pointer(int capture);
/* the host's clock, seconds since 1970 (UTC), or 0 if the hypervisor gives none */
uint32_t vmtools_host_time(void);
/* the `vmtools` shell command: status, on / off, time */
void vmtools_command(const char* args);

#endif
