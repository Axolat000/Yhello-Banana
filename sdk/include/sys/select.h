#ifndef _SYS_SELECT_H
#define _SYS_SELECT_H
#ifdef __cplusplus
extern "C" {
#endif
#include <sys/time.h>
#include <time.h>

#define FD_SETSIZE 1024
typedef struct { unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))]; } fd_set;
#define __FD_W(d) ((d) / (8 * sizeof(unsigned long)))
#define __FD_B(d) (1ul << ((d) % (8 * sizeof(unsigned long))))
#define FD_ZERO(s)     do { for (unsigned __i = 0; __i < sizeof((s)->fds_bits) / sizeof((s)->fds_bits[0]); __i++) (s)->fds_bits[__i] = 0; } while (0)
#define FD_SET(d, s)   ((s)->fds_bits[__FD_W(d)] |= __FD_B(d))
#define FD_CLR(d, s)   ((s)->fds_bits[__FD_W(d)] &= ~__FD_B(d))
#define FD_ISSET(d, s) (((s)->fds_bits[__FD_W(d)] & __FD_B(d)) != 0)

int select(int n, fd_set* rd, fd_set* wr, fd_set* ex, struct timeval* timeout);
#ifdef __cplusplus
}
#endif
#endif
