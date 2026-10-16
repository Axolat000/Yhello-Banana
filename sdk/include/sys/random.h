#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H
#ifdef __cplusplus
extern "C" {
#endif
/* random bytes from the kernel's generator */
#include <sys/types.h>
#define GRND_NONBLOCK 1
#define GRND_RANDOM   2
ssize_t getrandom(void* buf, size_t len, unsigned int flags);
int     getentropy(void* buf, size_t len);
#ifdef __cplusplus
}
#endif
#endif
