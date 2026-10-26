/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NETCONF_H
#define NETCONF_H

#include "net.h"
#include "netdev.h"
#include "netpolicy.h"

/*
 * Saved network configuration (/etc/network.conf):
 *
 *   priority=auto        auto, ethernet or wifi (auto prefers a wired link)
 *   iface=eth0            interface to use (optional)
 *   mode=static           or dhcp
 *   ip=192.168.1.50       static mode only
 *   netmask=255.255.255.0
 *   gateway=192.168.1.1
 *   dns=1.1.1.1
 *
 * `ifconfig` and `dhcp` write it; it is applied at boot (once the
 * filesystem is loaded) and again whenever the saved interface appears
 * (a USB adapter plugged in later).
 */

int netconf_priority(void);
const char* netconf_pinned_iface(void);
/* Changing media preference returns to DHCP and removes an explicit interface pin. */
int netconf_set_priority(int priority);
void net_reselect(void); /* apply preference now; periodic changes are debounced */

void netconf_boot(void);                      /* load + apply */
/* net_register_device(): 1 if the saved config names this device */
int  netconf_claims(const netdev_t* nd);
/* net_register_device(): 1 if a saved config pins another interface */
int  netconf_pins_iface(void);
void netconf_apply_to(netdev_t* nd);          /* select nd + its saved addresses */

/* save; 1 if also written to an installed disk */
int  netconf_save_static(const netdev_t* nd, ip4_t ip, ip4_t mask, ip4_t gw, ip4_t dns);
int  netconf_save_dhcp(const netdev_t* nd);
int  netconf_use_dhcp(void); /* change address mode, preserve interface/media preference */
int  netconf_forget(void);                    /* back to the defaults (DHCP, any card) */
int  netconf_is_static(void);

#endif
