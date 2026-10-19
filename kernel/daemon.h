/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef DAEMON_H
#define DAEMON_H

#include "types.h"

void daemon_init(void);
void daemon_poll(int prompt_idle);

#endif
