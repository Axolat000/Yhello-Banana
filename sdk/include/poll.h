#ifndef _POLL_H
#define _POLL_H
struct pollfd { int fd; short events; short revents; };
typedef unsigned long nfds_t;
#define POLLIN     0x001
#define POLLPRI    0x002
#define POLLOUT    0x004
#define POLLERR    0x008
#define POLLHUP    0x010
#define POLLNVAL   0x020
#define POLLRDNORM 0x040
#define POLLRDBAND 0x080
#define POLLWRNORM 0x100
#define POLLWRBAND 0x200
#define POLLRDHUP  0x2000
/* sockets, files (always ready) and the terminal; timeout -1: none */
int poll(struct pollfd* fds, nfds_t n, int timeout_ms);
#endif
