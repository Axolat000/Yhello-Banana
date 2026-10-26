#include "usock.h"
#include "kheap.h"
#include "kstring.h"
#include "timer.h"
#include "random.h"
#include "../net/net.h"
#include "../net/tcp.h"

/* Linux's values (the SDK's <sys/socket.h> has the same) */
#define AF_INET      2
#define AF_INET6     10
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOL_SOCKET   1
#define IPPROTO_TCP  6
#define IPPROTO_IPV6 41
#define IPV6_V6ONLY  26
#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_BROADCAST 6
#define SO_SNDBUF    7
#define SO_RCVBUF    8
#define SO_KEEPALIVE 9
#define SO_RCVTIMEO  20
#define SO_SNDTIMEO  21
#define SO_ACCEPTCONN 30
#define TCP_NODELAY  1
#define MSG_PEEK     0x2
#define MSG_DONTWAIT 0x40
#define MSG_WAITALL  0x100
#define SHUT_RD      0
#define SHUT_WR      1
#define SHUT_RDWR    2
#define POLLIN       0x1
#define POLLOUT      0x4
#define POLLERR      0x8
#define POLLHUP      0x10

#ifndef EPIPE
#define EPIPE        32
#endif
#ifndef EAGAIN
#define EAGAIN       11
#endif
#ifndef EINVAL
#define EINVAL       22
#endif
#ifndef ENOMEM
#define ENOMEM       12
#endif
#ifndef EINTR
#define EINTR        4
#endif
#ifndef EMSGSIZE
#define EMSGSIZE     90
#endif
#ifndef ENOPROTOOPT
#define ENOPROTOOPT  92
#endif
#ifndef EPROTONOSUPPORT
#define EPROTONOSUPPORT 93
#endif
#ifndef EOPNOTSUPP
#define EOPNOTSUPP   95
#endif
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT 97
#endif
#ifndef EADDRINUSE
#define EADDRINUSE   98
#endif
#ifndef ENETDOWN
#define ENETDOWN     100
#endif
#ifndef ENETUNREACH
#define ENETUNREACH  101
#endif
#ifndef ECONNRESET
#define ECONNRESET   104
#endif
#ifndef ECONNABORTED
#define ECONNABORTED 103
#endif
#ifndef ETIME
#define ETIME        62
#endif
#ifndef EISCONN
#define EISCONN      106
#endif
#ifndef ENOTCONN
#define ENOTCONN     107
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT    110
#endif
#ifndef ECONNREFUSED
#define ECONNREFUSED 111
#endif
#ifndef EHOSTUNREACH
#define EHOSTUNREACH 113
#endif
#ifndef EPROTO
#define EPROTO       71
#endif

enum { S_NEW, S_BOUND, S_LISTEN, S_CONN };

typedef struct dgram {
    struct dgram* next;
    int           family;
    ip4_t         ip;
    ip6_t         ip6;
    uint16_t      port;
    uint32_t      len;
    uint8_t       data[];
} dgram_t;

#define DGRAM_QUEUE 64

struct usock {
    int             family;
    int             type, nonblock, state;
    tcp_conn_t*     c;
    tcp_listener_t* l;
    uint16_t        lport;
    int             auto_port;     /* lport picked by bind (port 0): listen may pick another */
    ip4_t           rip;
    ip6_t           rip6;
    uint16_t        rport;
    int             has_peer;
    int             err;                  /* SO_ERROR */
    uint32_t        rcvtimeo, sndtimeo;   /* ms (0: none) */
    int             shut_rd, shut_wr;
    int             reuse, nodelay, keepalive, broadcast, v6only;
    int             udp_bound;
    dgram_t*        qh;
    dgram_t*        qt;
    int             qn;
};

static void (*g_wait)(void);
void usock_set_wait(void (*fn)(void)) { g_wait = fn; }
static void step(void) { if (g_wait) g_wait(); }

static int net_errno(int e) {
    switch (e) {
    case NET_ERR_TIMEOUT:  return -ETIMEDOUT;
    case NET_ERR_RESET:    return -ECONNRESET;
    case NET_ERR_REFUSED:  return -ECONNREFUSED;
    case NET_ERR_NOROUTE:  return -EHOSTUNREACH;
    case NET_ERR_NOMEM:    return -ENOMEM;
    case NET_ERR_CLOSED:   return -EPIPE;
    case NET_ERR_NOTREADY: case NET_ERR_NODEV: return -ENETDOWN;
    case NET_ERR_INTR:     return -EINTR;
    case NET_ERR_CERT:     return -ECONNABORTED;
    case NET_ERR_CLOCK:    return -ETIME;
    default:               return -EPROTO;
    }
}

/* struct sockaddr_in (16 bytes) and struct sockaddr_in6 (28 bytes) */
static int sa_get(const void* sa, int len, int expected_family,
                  ip4_t* ip4, ip6_t* ip6, uint16_t* port, int* out_family) {
    const uint8_t* b = (const uint8_t*)sa;
    if (!b || len < 2) return -EINVAL;
    uint16_t fam = (uint16_t)(b[0] | (b[1] << 8));
    if (expected_family && fam != (uint16_t)expected_family) return -EAFNOSUPPORT;

    if (fam == AF_INET) {
        if (len < 8) return -EINVAL;
        if (port) *port = (uint16_t)(b[2] << 8 | b[3]);
        if (ip4) *ip4 = (ip4_t)b[4] << 24 | (ip4_t)b[5] << 16 | (ip4_t)b[6] << 8 | b[7];
        if (out_family) *out_family = AF_INET;
        return 0;
    }
    if (fam == AF_INET6) {
        if (len < 24) return -EINVAL; /* sin6_family(2) + sin6_port(2) + sin6_flowinfo(4) + sin6_addr(16) */
        if (port) *port = (uint16_t)(b[2] << 8 | b[3]);
        if (ip6) memcpy(ip6->b, b + 8, 16);
        if (out_family) *out_family = AF_INET6;
        return 0;
    }
    return -EAFNOSUPPORT;
}

static void sa_put(void* sa, int* len, int family, ip4_t ip4, const ip6_t* ip6, uint16_t port) {
    if (!sa || !len) return;
    if (family == AF_INET6) {
        uint8_t b[28];
        memset(b, 0, sizeof(b));
        b[0] = (uint8_t)AF_INET6;
        b[1] = (uint8_t)(AF_INET6 >> 8);
        b[2] = (uint8_t)(port >> 8);
        b[3] = (uint8_t)port;
        if (ip6) memcpy(b + 8, ip6->b, 16);
        int n = *len < 28 ? (*len < 0 ? 0 : *len) : 28;
        memcpy(sa, b, (size_t)n);
        *len = 28;
        return;
    }
    uint8_t b[16] = { AF_INET, 0, (uint8_t)(port >> 8), (uint8_t)port,
                      (uint8_t)(ip4 >> 24), (uint8_t)(ip4 >> 16), (uint8_t)(ip4 >> 8), (uint8_t)ip4 };
    int n = *len < 16 ? (*len < 0 ? 0 : *len) : 16;
    memcpy(sa, b, (size_t)n);
    *len = 16;
}

static uint16_t ephemeral(void) { return (uint16_t)(49152u + random_u32() % 16384u); }

/* the address this end has towards rip */
static ip4_t local_ip(ip4_t rip) {
    if ((rip >> 24) == 127) return rip;
    netif_t* nf = net_if();
    return nf && nf->configured ? nf->ip : 0;
}

static ip6_t local_ip6(const ip6_t* rip6) {
    ip6_t src;
    memset(&src, 0, sizeof(src));
    if (rip6 && ipv6_source(rip6, &src) == NET_OK) return src;
    if (ipv6_global(&src, NULL) || ipv6_link_local(&src)) return src;
    return src;
}

usock_t* usock_new(int family, int type, int* err) {
    if (family != AF_INET && family != AF_INET6) { *err = -EAFNOSUPPORT; return NULL; }
    if (type != SOCK_STREAM && type != SOCK_DGRAM) { *err = -EPROTONOSUPPORT; return NULL; }
    usock_t* s = (usock_t*)kzalloc(sizeof(usock_t));
    if (!s) { *err = -ENOMEM; return NULL; }
    s->family = family;
    s->type = type;
    return s;
}

/* a datagram for an IPv4 UDP socket */
static void udp_in(ip4_t src, uint16_t sport, const uint8_t* data, uint32_t len, void* ctx) {
    usock_t* s = (usock_t*)ctx;
    if (s->qn >= DGRAM_QUEUE) return;               /* full: dropped, as UDP may */
    dgram_t* d = (dgram_t*)kmalloc(sizeof(dgram_t) + len);
    if (!d) return;
    d->next = NULL;
    d->family = AF_INET;
    d->ip = src;
    memset(&d->ip6, 0, sizeof(d->ip6));
    d->port = sport;
    d->len = len;
    memcpy(d->data, data, len);
    if (s->qt) s->qt->next = d; else s->qh = d;
    s->qt = d;
    s->qn++;
}

/* a datagram for an IPv6 UDP socket */
static void udp6_in(const ip6_t* src, uint16_t sport, const uint8_t* data, uint32_t len, void* ctx) {
    usock_t* s = (usock_t*)ctx;
    if (s->qn >= DGRAM_QUEUE) return;
    dgram_t* d = (dgram_t*)kmalloc(sizeof(dgram_t) + len);
    if (!d) return;
    d->next = NULL;
    d->family = AF_INET6;
    d->ip = 0;
    if (src) d->ip6 = *src; else memset(&d->ip6, 0, sizeof(d->ip6));
    d->port = sport;
    d->len = len;
    memcpy(d->data, data, len);
    if (s->qt) s->qt->next = d; else s->qh = d;
    s->qt = d;
    s->qn++;
}

static int udp_bind_port(usock_t* s, uint16_t port) {
    if (s->udp_bound) return -EINVAL;
    for (int tries = 0; tries < 64; tries++) {
        uint16_t p = port ? port : (s->family == AF_INET6 ? udp6_ephemeral_port() : ephemeral());
        if (!p) p = ephemeral();
        int rc = (s->family == AF_INET6) ? udp6_bind(p, udp6_in, s) : udp_bind(p, udp_in, s);
        if (rc == 0) {
            s->lport = p;
            s->udp_bound = 1;
            s->state = S_BOUND;
            return 0;
        }
        if (port) return -EADDRINUSE;
    }
    return -EADDRINUSE;
}

void usock_close(usock_t* s) {
    if (!s) return;
    if (s->c) tcp_close(s->c);                       /* (graceful: the stack finishes it) */
    if (s->l) tcp_unlisten(s->l);
    if (s->udp_bound) {
        if (s->family == AF_INET6) udp6_unbind(s->lport);
        else udp_unbind(s->lport);
    }
    while (s->qh) { dgram_t* d = s->qh; s->qh = d->next; kfree(d); }
    kfree(s);
}

int usock_bind(usock_t* s, const void* sa, int len) {
    ip4_t ip = 0;
    ip6_t ip6;
    memset(&ip6, 0, sizeof(ip6));
    uint16_t port = 0;
    int rc = sa_get(sa, len, s->family, &ip, &ip6, &port, NULL);
    if (rc) return rc;
    if (s->state != S_NEW) return -EINVAL;
    if (s->type == SOCK_DGRAM) return udp_bind_port(s, port);
    s->lport = port ? port : ephemeral();            /* (taken by listen; getsockname shows it now) */
    s->auto_port = !port;
    s->state = S_BOUND;
    return 0;
}

int usock_listen(usock_t* s, int backlog) {
    (void)backlog;
    if (s->type != SOCK_STREAM) return -EOPNOTSUPP;
    if (s->state == S_LISTEN) return 0;
    if (s->state == S_CONN) return -EINVAL;
    int err = 0;
    for (int tries = 0; tries < 64; tries++) {
        uint16_t p = s->lport && (!s->auto_port || !tries) ? s->lport : ephemeral();
        s->l = (s->family == AF_INET6) ? tcp_listen6(p, &err) : tcp_listen(p, &err);
        if (s->l) { s->lport = p; s->state = S_LISTEN; return 0; }
        if (s->lport && !s->auto_port) break;
    }
    return -EADDRINUSE;
}

usock_t* usock_accept(usock_t* s, void* sa, int* len, int* err) {
    if (s->state != S_LISTEN) { *err = -EINVAL; return NULL; }
    uint32_t start = timer_ms();
    for (;;) {
        tcp_conn_t* c = tcp_accept(s->l, s->nonblock ? 0 : 50);
        if (c) {
            usock_t* n = (usock_t*)kzalloc(sizeof(usock_t));
            if (!n) { tcp_abort(c); *err = -ENOMEM; return NULL; }
            n->family = s->family;
            n->type = SOCK_STREAM;
            n->c = c;
            n->state = S_CONN;
            n->lport = s->lport;
            if (s->family == AF_INET6) {
                tcp_peer6(c, &n->rip6, &n->rport);
                n->has_peer = 1;
                sa_put(sa, len, AF_INET6, 0, &n->rip6, n->rport);
            } else {
                tcp_peer(c, &n->rip, &n->rport);
                n->has_peer = 1;
                sa_put(sa, len, AF_INET, n->rip, NULL, n->rport);
            }
            return n;
        }
        if (s->nonblock || (s->rcvtimeo && timer_ms() - start >= s->rcvtimeo)) { *err = -EAGAIN; return NULL; }
        step();
    }
}

int usock_connect(usock_t* s, const void* sa, int len) {
    ip4_t ip = 0;
    ip6_t ip6;
    memset(&ip6, 0, sizeof(ip6));
    uint16_t port = 0;
    int rc = sa_get(sa, len, s->family, &ip, &ip6, &port, NULL);
    if (rc) return rc;
    if (s->type == SOCK_DGRAM) {                    /* the default peer */
        if (!s->udp_bound && (rc = udp_bind_port(s, 0)) != 0) return rc;
        if (s->family == AF_INET6) s->rip6 = ip6;
        else s->rip = ip;
        s->rport = port;
        s->has_peer = 1;
        return 0;
    }
    if (s->state == S_CONN) return -EISCONN;
    if (s->state == S_LISTEN) return -EINVAL;
    int err = 0;
    tcp_conn_t* c = (s->family == AF_INET6)
        ? tcp_connect6(&ip6, port, s->sndtimeo ? s->sndtimeo : 30000, &err)
        : tcp_connect(ip, port, s->sndtimeo ? s->sndtimeo : 30000, &err);
    if (!c) { s->err = -net_errno(err); return net_errno(err); }
    s->c = c;
    s->state = S_CONN;
    if (s->family == AF_INET6) s->rip6 = ip6;
    else s->rip = ip;
    s->rport = port;
    s->has_peer = 1;
    s->lport = tcp_local_port(c);
    return 0;
}

long usock_send(usock_t* s, const void* buf, uint32_t len, int flags, const void* sa, int salen) {
    if (s->shut_wr) return -EPIPE;
    if (s->type == SOCK_DGRAM) {
        ip4_t ip = s->rip;
        ip6_t ip6 = s->rip6;
        uint16_t port = s->rport;
        if (sa) {
            int rc = sa_get(sa, salen, s->family, &ip, &ip6, &port, NULL);
            if (rc) return rc;
        } else if (!s->has_peer) return -ENOTCONN;

        if (s->family == AF_INET6) {
            if (len > IP6_MAX_PAYLOAD - 8) return -EMSGSIZE;
            if (!s->udp_bound) { int rc = udp_bind_port(s, 0); if (rc) return rc; }
            int rc = udp6_send(&ip6, s->lport, port, buf, len);
            return rc < 0 ? net_errno(rc) : (long)len;
        } else {
            if (len > 1472) return -EMSGSIZE;
            if (!s->udp_bound) { int rc = udp_bind_port(s, 0); if (rc) return rc; }
            int rc = udp_send(ip, s->lport, port, buf, len);
            return rc < 0 ? net_errno(rc) : (long)len;
        }
    }
    if (s->state != S_CONN) return -ENOTCONN;
    if (!len) return 0;
    if ((s->nonblock || (flags & MSG_DONTWAIT))) {
        uint32_t room = tcp_send_space(s->c);
        if (!room) return tcp_state(s->c) == TCP_CLOSED ? -EPIPE : -EAGAIN;
        if (len > room) len = room;
    }
    int rc = tcp_send(s->c, buf, len, s->sndtimeo ? s->sndtimeo : 60000);
    if (rc < 0) return rc == NET_ERR_TIMEOUT ? -EAGAIN : net_errno(rc) == -EPIPE ? -EPIPE : net_errno(rc);
    return rc;
}

long usock_recv(usock_t* s, void* buf, uint32_t len, int flags, void* sa, int* salen) {
    int nb = s->nonblock || (flags & MSG_DONTWAIT);
    uint32_t start = timer_ms();
    if (s->type == SOCK_DGRAM) {
        if (!s->udp_bound) return nb ? -EAGAIN : -ENOTCONN;
        while (!s->qh) {
            if (nb || (s->rcvtimeo && timer_ms() - start >= s->rcvtimeo)) return -EAGAIN;
            net_wait(10);
            step();
        }
        dgram_t* d = s->qh;
        uint32_t n = d->len < len ? d->len : len;
        memcpy(buf, d->data, n);
        sa_put(sa, salen, d->family, d->ip, &d->ip6, d->port);
        if (!(flags & MSG_PEEK)) {
            s->qh = d->next;
            if (!s->qh) s->qt = NULL;
            s->qn--;
            kfree(d);
        }
        return (long)n;
    }
    if (s->state != S_CONN) return -ENOTCONN;
    if (s->shut_rd || !len) return 0;
    uint32_t got = 0;
    for (;;) {
        if (flags & MSG_PEEK) {
            int r = tcp_readable(s->c);
            if (r > 0) return tcp_peek(s->c, buf, len);
            if (r < 0) return 0;
        } else {
            int r = tcp_recv(s->c, (uint8_t*)buf + got, len - got, nb ? 0 : 50);
            if (r > 0) {
                got += (uint32_t)r;
                if (!(flags & MSG_WAITALL) || got == len) return (long)got;
                continue;
            }
            if (r == 0) return (long)got;                 /* the peer closed */
            if (r != NET_ERR_TIMEOUT) return got ? (long)got : net_errno(r) == -EPIPE ? 0 : net_errno(r);
        }
        if (nb || (s->rcvtimeo && timer_ms() - start >= s->rcvtimeo)) return got ? (long)got : -EAGAIN;
        if (flags & MSG_PEEK) net_wait(10);
        step();
    }
}

int usock_shutdown(usock_t* s, int how) {
    if (s->state != S_CONN && s->type == SOCK_STREAM) return -ENOTCONN;
    if (how == SHUT_RD || how == SHUT_RDWR) s->shut_rd = 1;
    if (how == SHUT_WR || how == SHUT_RDWR) s->shut_wr = 1;
    if (how == SHUT_RDWR && s->c) { tcp_close(s->c); s->c = NULL; s->state = S_BOUND; }
    return 0;
}

static int opt_int(void* val, int* len, int v) {
    if (!val || !len || *len < 4) return -EINVAL;
    *(int*)val = v;
    *len = 4;
    return 0;
}

int usock_opt(usock_t* s, int set, int level, int name, void* val, int* len) {
    if (!len) return -EINVAL;
    int iv = set && val && *len >= 4 ? *(int*)val : 0;
    if (level == SOL_SOCKET) {
        switch (name) {
        case SO_REUSEADDR: case 15 /* SO_REUSEPORT */:
            if (set) { s->reuse = iv != 0; return 0; }
            return opt_int(val, len, s->reuse);
        case SO_TYPE:  return set ? -ENOPROTOOPT : opt_int(val, len, s->type);
        case SO_ERROR: {
            int e = s->err;
            s->err = 0;
            return set ? -ENOPROTOOPT : opt_int(val, len, e);
        }
        case SO_BROADCAST:
            if (set) { s->broadcast = iv != 0; return 0; }
            return opt_int(val, len, s->broadcast);
        case SO_KEEPALIVE:
            if (set) { s->keepalive = iv != 0; return 0; }
            return opt_int(val, len, s->keepalive);
        case SO_SNDBUF: case SO_RCVBUF:
            return set ? 0 : opt_int(val, len, 65536);
        case SO_ACCEPTCONN: return set ? -ENOPROTOOPT : opt_int(val, len, s->state == S_LISTEN);
        case SO_RCVTIMEO: case SO_SNDTIMEO: {
            /* struct timeval: two longs (the app's size: the kernel's) */
            uint32_t* t = name == SO_RCVTIMEO ? &s->rcvtimeo : &s->sndtimeo;
            if ((size_t)*len < 2 * sizeof(long) || !val) return -EINVAL;
            long* tv = (long*)val;
            if (set) { *t = (uint32_t)(tv[0] * 1000 + tv[1] / 1000); return 0; }
            tv[0] = (long)(*t / 1000);
            tv[1] = (long)(*t % 1000) * 1000;
            *len = (int)(2 * sizeof(long));
            return 0;
        }
        default:
            return set ? 0 : opt_int(val, len, 0);  /* (linger and the rest: accepted, no effect) */
        }
    }
    if (level == IPPROTO_TCP) {
        if (name == TCP_NODELAY) {
            if (set) { s->nodelay = iv != 0; return 0; }
            return opt_int(val, len, s->nodelay);
        }
        return set ? 0 : opt_int(val, len, 0);
    }
    if (level == IPPROTO_IPV6) {
        if (name == IPV6_V6ONLY) {
            if (set) { s->v6only = iv != 0; return 0; }
            return opt_int(val, len, s->v6only);
        }
        return set ? 0 : opt_int(val, len, 0);
    }
    if (level == 0) return set ? 0 : opt_int(val, len, 0);      /* IPPROTO_IP: TOS, TTL... */
    return -ENOPROTOOPT;
}

int usock_name(usock_t* s, int peer, void* sa, int* len) {
    if (s->family == AF_INET6) {
        if (peer) {
            if (!s->has_peer) return -ENOTCONN;
            sa_put(sa, len, AF_INET6, 0, &s->rip6, s->rport);
            return 0;
        }
        ip6_t loc = s->has_peer ? local_ip6(&s->rip6) : local_ip6(NULL);
        sa_put(sa, len, AF_INET6, 0, &loc, s->lport);
        return 0;
    }
    if (peer) {
        if (!s->has_peer) return -ENOTCONN;
        sa_put(sa, len, AF_INET, s->rip, NULL, s->rport);
        return 0;
    }
    sa_put(sa, len, AF_INET, s->has_peer ? local_ip(s->rip) : 0, NULL, s->lport);
    return 0;
}

void usock_set_nonblock(usock_t* s, int on) { s->nonblock = on != 0; }
int  usock_nonblock(usock_t* s) { return s->nonblock; }

int usock_nread(usock_t* s) {
    if (s->type == SOCK_DGRAM) return s->qh ? (int)s->qh->len : 0;
    if (s->state != S_CONN) return 0;
    int r = tcp_readable(s->c);
    return r > 0 ? r : 0;
}

int usock_poll(usock_t* s, int events) {
    int r = 0;
    if (s->type == SOCK_DGRAM) {
        if (s->qh) r |= POLLIN;
        r |= POLLOUT;
    } else if (s->state == S_LISTEN) {
        if (tcp_pending(s->l)) r |= POLLIN;
    } else if (s->state == S_CONN) {
        int n = tcp_readable(s->c);
        if (n) r |= POLLIN;                             /* data, or the end (read gives 0) */
        if (n < 0) r |= POLLHUP;
        if (tcp_send_space(s->c)) r |= POLLOUT;
        if (tcp_state(s->c) == TCP_CLOSED && n <= 0) r |= POLLERR;
    } else {
        r |= POLLOUT;
    }
    return r & (events | POLLERR | POLLHUP);
}
