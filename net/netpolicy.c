/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "netpolicy.h"
#include "kstring.h"

static int score(netdev_t* d, int priority, const char* pinned) {
    if (!d || !d->present) return -1;
    if (d->is_wifi && !d->link_up) return -1;
    if (d->link_up && !d->link_up(d)) return -1;
    if (pinned && *pinned && !strcmp(pinned, d->ifname)) return 100;
    /* Auto uses a connected wired interface first. USB is a transport,
     * not a reason to displace a working PCI Ethernet connection. */
    return (priority == NET_PRIORITY_WIFI ? d->is_wifi : !d->is_wifi) ? 20 : 10;
}

netdev_t* netpolicy_choose(netdev_t* const* devices, int count, netdev_t* current,
                          int priority, const char* pinned) {
    netdev_t* best = NULL;
    int best_score = -1;
    for (int i = 0; i < count; i++) {
        int s = score(devices[i], priority, pinned);
        if (s > best_score || (s >= 0 && s == best_score && devices[i] == current)) {
            best = devices[i]; best_score = s;
        }
    }
    return best;
}
