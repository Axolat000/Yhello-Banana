/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QGA_H
#define QGA_H

/* The QEMU guest agent (kernel/qga.c): answers QEMU's, KVM's and Proxmox
 * VE's agent commands on the virtio-serial port org.qemu.guest_agent.0. */

/* after vser_init(): starts the agent (it waits for its port); 1 if it may run */
int  qga_init(void);
int  qga_active(void);
/* "on port 1 (host connected), 12 commands answered, last: guest-ping" */
void qga_describe(char* out, int cap);

#endif
