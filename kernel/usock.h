#ifndef USOCK_H
#define USOCK_H

#include "types.h"

/*
 * BSD sockets for apps (kernel/app.c keeps them in an app's file
 * descriptors): TCP and UDP over IPv4 and IPv6, the loopback included,
 * on the stack of net/. Addresses are Linux's struct sockaddr_in and
 * sockaddr_in6, constants Linux's values, errors negative errno numbers
 * (-ECONNREFUSED...).
 *
 * The calls that wait (connect, accept, recv, send) wait in short steps,
 * calling the wait callback between them: it lets the app be stopped
 * (Ctrl+C, End task) and the desktop paint. A non-blocking socket never
 * waits (-EAGAIN) - except connect, which completes before it returns.
 */

typedef struct usock usock_t;

/* what blocking calls run between their steps (kernel/app.c: breathe) */
void     usock_set_wait(void (*fn)(void));

usock_t* usock_new(int family, int type, int* err);
void     usock_close(usock_t* s);
int      usock_bind(usock_t* s, const void* sa, int len);
int      usock_listen(usock_t* s, int backlog);
/* a connection, its peer in sa (len in/out); NULL with *err */
usock_t* usock_accept(usock_t* s, void* sa, int* len, int* err);
int      usock_connect(usock_t* s, const void* sa, int len);
/* sa NULL: the connected peer */
long     usock_send(usock_t* s, const void* buf, uint32_t len, int flags, const void* sa, int salen);
long     usock_recv(usock_t* s, void* buf, uint32_t len, int flags, void* sa, int* salen);
int      usock_shutdown(usock_t* s, int how);
/* get/setsockopt: val holds len bytes (len in/out for get) */
int      usock_opt(usock_t* s, int set, int level, int name, void* val, int* len);
int      usock_name(usock_t* s, int peer, void* sa, int* len);
void     usock_set_nonblock(usock_t* s, int on);
int      usock_nonblock(usock_t* s);
int      usock_nread(usock_t* s);                  /* FIONREAD: bytes there to read */
/* POLLIN / POLLOUT / POLLERR / POLLHUP of events that are true now */
int      usock_poll(usock_t* s, int events);

#endif
