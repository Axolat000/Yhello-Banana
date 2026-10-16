#ifndef _ARPA_INET_H
#define _ARPA_INET_H
#include <netinet/in.h>
in_addr_t   inet_addr(const char* s);
int         inet_aton(const char* s, struct in_addr* out);
char*       inet_ntoa(struct in_addr a);
int         inet_pton(int family, const char* s, void* out);
const char* inet_ntop(int family, const void* a, char* out, socklen_t len);
#endif
