/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef AML_HOST_H
#define AML_HOST_H
#include "types.h"
int aml_host_start(void);
void aml_host_drain(void);
uint32_t aml_host_bytes(void);
#endif
