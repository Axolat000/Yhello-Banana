/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WEB_WORKER_H
#define WEB_WORKER_H
#include "types.h"
int web_worker_ipc(int app, uint32_t generation, int op, const void* in, uint32_t len, void* out, uint32_t cap);
#endif
