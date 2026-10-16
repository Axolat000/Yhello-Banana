#ifndef IPV6TEST_H
#define IPV6TEST_H
#include "types.h"
/* Local tests need no router. Optional host/port exercises the real network. */
void ipv6test_run(const char* host, uint16_t port,
                 void (*report)(const char* what, int ok, const char* note));
#endif
