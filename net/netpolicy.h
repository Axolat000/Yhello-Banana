/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef NETPOLICY_H
#define NETPOLICY_H
#include "netdev.h"
enum { NET_PRIORITY_AUTO, NET_PRIORITY_ETHERNET, NET_PRIORITY_WIFI };
/* Wi-Fi link_up must mean associated AND authenticated; no callback is not a link. */
netdev_t* netpolicy_choose(netdev_t* const* devices, int count, netdev_t* current,
                          int priority, const char* pinned);
#endif
