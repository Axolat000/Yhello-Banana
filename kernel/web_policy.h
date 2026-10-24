/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef WEB_POLICY_H
#define WEB_POLICY_H
#include "types.h"
int web_network_url(const char* url);
int web_same_origin(const char* a, const char* b);
int web_endpoint_allowed(const char* principal,const char* host,uint16_t port,int family,const void* address);
#endif
