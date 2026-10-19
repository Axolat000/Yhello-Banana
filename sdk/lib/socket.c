/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: BSD sockets, poll / select, name lookups (<sys/socket.h>,
 * <netdb.h>, <arpa/inet.h>, <poll.h>, <sys/select.h>, <sys/ioctl.h>) over
 * the system's sock_* calls (API version 18), which return -errno. */
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT, in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
int h_errno;

static int has_sock(void) { return __banana->version >= 18 && BANANA_HAS(__banana, resolve); }

/* a system call's result: >= 0 as it is, -errno as -1 with errno set */
static long ret(long r) {
    if (r >= 0) return r;
    errno = (int)-r;
    return -1;
}

#define NEED_SOCK(bad) do { if (!has_sock()) { errno = ENOSYS; return bad; } } while (0)

/* ufd.c: the descriptors the C library makes itself (socket pairs, pipes,
 * eventfd, epoll) */
int __ufd_is(int fd);
ssize_t __ufd_read(int fd, void* buf, size_t n, int dontwait);
ssize_t __ufd_write(int fd, const void* buf, size_t n, int dontwait);
int __ufd_poll(struct pollfd* fds, nfds_t n, int timeout_ms);
int __ufd_shutdown(int fd, int how);
int __ufd_ioctl(int fd, unsigned long req, void* arg);

int socket(int family, int type, int proto) { NEED_SOCK(-1); return (int)ret(__banana->sock_open(family, type, proto)); }
int bind(int fd, const struct sockaddr* sa, socklen_t len) { NEED_SOCK(-1); return (int)ret(__banana->sock_bind(fd, sa, (int)len)); }
int listen(int fd, int backlog) { NEED_SOCK(-1); return (int)ret(__banana->sock_listen(fd, backlog)); }
int connect(int fd, const struct sockaddr* sa, socklen_t len) { NEED_SOCK(-1); return (int)ret(__banana->sock_connect(fd, sa, (int)len)); }
int shutdown(int fd, int how) {
    if (__ufd_is(fd)) return __ufd_shutdown(fd, how);
    NEED_SOCK(-1);
    return (int)ret(__banana->sock_shutdown(fd, how));
}

int accept4(int fd, struct sockaddr* sa, socklen_t* len, int flags) {
    NEED_SOCK(-1);
    int l = len ? (int)*len : 0;
    int r = (int)ret(__banana->sock_accept(fd, sa, len ? &l : NULL));
    if (r >= 0) {
        if (len) *len = (socklen_t)l;
        if (flags & SOCK_NONBLOCK) __banana->sock_ctl(r, 1, 1);
    }
    return r;
}
int accept(int fd, struct sockaddr* sa, socklen_t* len) { return accept4(fd, sa, len, 0); }

ssize_t sendto(int fd, const void* buf, size_t len, int flags, const struct sockaddr* sa, socklen_t salen) {
    if (__ufd_is(fd)) return __ufd_write(fd, buf, len, (flags & MSG_DONTWAIT) != 0);
    NEED_SOCK(-1);
    return ret(__banana->sock_send(fd, buf, len, flags, sa, (int)salen));
}
ssize_t send(int fd, const void* buf, size_t len, int flags) { return sendto(fd, buf, len, flags, NULL, 0); }

ssize_t recvfrom(int fd, void* buf, size_t len, int flags, struct sockaddr* sa, socklen_t* salen) {
    if (__ufd_is(fd)) {
        if (salen) *salen = 0;
        return __ufd_read(fd, buf, len, (flags & MSG_DONTWAIT) != 0);
    }
    NEED_SOCK(-1);
    int l = salen ? (int)*salen : 0;
    long r = ret(__banana->sock_recv(fd, buf, len, flags, sa, salen ? &l : NULL));
    if (r >= 0 && salen) *salen = (socklen_t)l;
    return r;
}
ssize_t recv(int fd, void* buf, size_t len, int flags) { return recvfrom(fd, buf, len, flags, NULL, NULL); }

/* (the pieces gathered into one buffer: one datagram, or one write) */
ssize_t sendmsg(int fd, const struct msghdr* m, int flags) {
    size_t total = 0;
    for (size_t i = 0; i < m->msg_iovlen; i++) total += m->msg_iov[i].iov_len;
    char* b = malloc(total ? total : 1);
    if (!b) { errno = ENOMEM; return -1; }
    size_t at = 0;
    for (size_t i = 0; i < m->msg_iovlen; i++) { memcpy(b + at, m->msg_iov[i].iov_base, m->msg_iov[i].iov_len); at += m->msg_iov[i].iov_len; }
    ssize_t r = sendto(fd, b, total, flags, m->msg_name, m->msg_namelen);
    free(b);
    return r;
}

ssize_t recvmsg(int fd, struct msghdr* m, int flags) {
    size_t total = 0;
    for (size_t i = 0; i < m->msg_iovlen; i++) total += m->msg_iov[i].iov_len;
    char* b = malloc(total ? total : 1);
    if (!b) { errno = ENOMEM; return -1; }
    socklen_t nl = m->msg_namelen;
    ssize_t r = recvfrom(fd, b, total, flags, m->msg_name, m->msg_name ? &nl : NULL);
    if (r >= 0) {
        size_t at = 0;
        for (size_t i = 0; i < m->msg_iovlen && at < (size_t)r; i++) {
            size_t n = m->msg_iov[i].iov_len < (size_t)r - at ? m->msg_iov[i].iov_len : (size_t)r - at;
            memcpy(m->msg_iov[i].iov_base, b + at, n);
            at += n;
        }
        m->msg_namelen = nl;
        m->msg_controllen = 0;
        m->msg_flags = 0;
    }
    free(b);
    return r;
}

int getsockopt(int fd, int level, int name, void* val, socklen_t* len) {
    NEED_SOCK(-1);
    int l = len ? (int)*len : 0;
    int r = (int)ret(__banana->sock_opt(fd, 0, level, name, val, &l));
    if (r >= 0 && len) *len = (socklen_t)l;
    return r;
}

int setsockopt(int fd, int level, int name, const void* val, socklen_t len) {
    NEED_SOCK(-1);
    int l = (int)len;
    return (int)ret(__banana->sock_opt(fd, 1, level, name, (void*)val, &l));
}

static int sockname(int fd, int peer, struct sockaddr* sa, socklen_t* len) {
    NEED_SOCK(-1);
    int l = len ? (int)*len : 0;
    int r = (int)ret(__banana->sock_name(fd, peer, sa, &l));
    if (r >= 0 && len) *len = (socklen_t)l;
    return r;
}
int getsockname(int fd, struct sockaddr* sa, socklen_t* len) { return sockname(fd, 0, sa, len); }
int getpeername(int fd, struct sockaddr* sa, socklen_t* len) { return sockname(fd, 1, sa, len); }

/* ── poll, select, ioctl ──────────────────────────────────────────── */

/* the system's poll (its files, sockets and the terminal) */
int __kpoll(struct pollfd* fds, nfds_t n, int timeout_ms) {
    if (!has_sock()) {                       /* an older system: files and the terminal are ready */
        for (nfds_t i = 0; i < n; i++) fds[i].revents = fds[i].events & (POLLIN | POLLOUT);
        return (int)n;
    }
    return (int)ret(__banana->poll(fds, (int)n, timeout_ms));
}

int poll(struct pollfd* fds, nfds_t n, int timeout_ms) {
    for (nfds_t i = 0; i < n; i++)
        if (__ufd_is(fds[i].fd)) return __ufd_poll(fds, n, timeout_ms);
    return __kpoll(fds, n, timeout_ms);
}

int select(int n, fd_set* rd, fd_set* wr, fd_set* ex, struct timeval* tv) {
    struct pollfd pf[64];
    int map[64], k = 0;
    for (int fd = 0; fd < n && fd < FD_SETSIZE; fd++) {
        short ev = 0;
        if (rd && FD_ISSET(fd, rd)) ev |= POLLIN;
        if (wr && FD_ISSET(fd, wr)) ev |= POLLOUT;
        if (ex && FD_ISSET(fd, ex)) ev |= POLLPRI;
        if (!ev) continue;
        if (k == 64) { errno = EINVAL; return -1; }
        pf[k].fd = fd;
        pf[k].events = ev;
        pf[k].revents = 0;
        map[k++] = fd;
    }
    int ms = tv ? (int)(tv->tv_sec * 1000 + tv->tv_usec / 1000) : -1;
    int r = poll(pf, (nfds_t)k, ms);
    if (r < 0) return -1;
    if (rd) FD_ZERO(rd);
    if (wr) FD_ZERO(wr);
    if (ex) FD_ZERO(ex);
    int count = 0;
    for (int i = 0; i < k; i++) {
        int fd = map[i];
        short rv = pf[i].revents;
        if (rd && (rv & (POLLIN | POLLHUP | POLLERR))) { FD_SET(fd, rd); count++; }
        if (wr && (rv & (POLLOUT | POLLERR))) { FD_SET(fd, wr); count++; }
        if (ex && (rv & POLLPRI)) { FD_SET(fd, ex); count++; }
    }
    return count;
}

int ioctl(int fd, unsigned long req, ...) {
    va_list ap;
    va_start(ap, req);
    void* arg = va_arg(ap, void*);
    va_end(ap);
    if (req == TIOCGWINSZ) {
        if (fd > 2) { errno = ENOTTY; return -1; }
        struct winsize* w = arg;
        int c = 80, r = 25;
        __banana->term_size(&c, &r);
        w->ws_col = (unsigned short)c;
        w->ws_row = (unsigned short)r;
        w->ws_xpixel = w->ws_ypixel = 0;
        return 0;
    }
    if (__ufd_is(fd)) return __ufd_ioctl(fd, req, arg);
    NEED_SOCK(-1);
    if (req == FIONREAD) {
        int r = (int)ret(__banana->sock_ctl(fd, 3, 0));
        if (r < 0) return -1;
        *(int*)arg = r;
        return 0;
    }
    if (req == FIONBIO) return (int)ret(__banana->sock_ctl(fd, 1, *(int*)arg != 0));
    errno = ENOTTY;
    return -1;
}

/* fcntl's flags of a socket (posix.c): O_NONBLOCK; -1 if fd is not one */
int __sock_getfl(int fd) {
    if (!has_sock()) return -1;
    int r = __banana->sock_ctl(fd, 2, 0);
    return r < 0 ? -1 : (r ? O_NONBLOCK : 0);
}
int __sock_setfl(int fd, int fl) {
    if (!has_sock()) return -1;
    return __banana->sock_ctl(fd, 1, (fl & O_NONBLOCK) != 0) < 0 ? -1 : 0;
}

ssize_t readv(int fd, const struct iovec* iov, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = read(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

ssize_t writev(int fd, const struct iovec* iov, int n) {
    ssize_t total = 0;
    for (int i = 0; i < n; i++) {
        ssize_t r = write(fd, iov[i].iov_base, iov[i].iov_len);
        if (r < 0) return total ? total : -1;
        total += r;
        if ((size_t)r < iov[i].iov_len) break;
    }
    return total;
}

/* ── addresses ────────────────────────────────────────────────────── */

int inet_aton(const char* s, struct in_addr* out) {
    unsigned v = 0;
    for (int part = 0; part < 4; part++) {
        if (*s < '0' || *s > '9') return 0;
        unsigned n = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') { n = n * 10 + (unsigned)(*s++ - '0'); if (++digits > 3) return 0; }
        if (n > 255) return 0;
        v = v << 8 | n;
        if (part < 3 && *s++ != '.') return 0;
    }
    if (*s) return 0;
    if (out) out->s_addr = htonl(v);
    return 1;
}

in_addr_t inet_addr(const char* s) { struct in_addr a; return inet_aton(s, &a) ? a.s_addr : INADDR_NONE; }

char* inet_ntoa(struct in_addr a) {
    static char buf[INET_ADDRSTRLEN];
    unsigned v = ntohl(a.s_addr);
    snprintf(buf, sizeof(buf), "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255);
    return buf;
}

static int hexval(int c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* "::1", "fe80::1:2", "::ffff:1.2.3.4" */
static int pton6(const char* s, uint8_t out[16]) {
    uint16_t w[8] = { 0 };
    int n = 0, gap = -1;
    if (s[0] == ':' && s[1] == ':') { gap = 0; s += 2; }
    while (*s) {
        if (n == 8) return 0;
        const char* dot = strchr(s, '.');
        if (dot && !strchr(s, ':')) {                    /* the IPv4 tail */
            struct in_addr a;
            if (n > 6 || !inet_aton(s, &a)) return 0;
            uint32_t v = ntohl(a.s_addr);
            w[n++] = (uint16_t)(v >> 16);
            w[n++] = (uint16_t)v;
            break;
        }
        int v = 0, digits = 0;
        while (hexval(*s) >= 0) { v = v * 16 + hexval(*s++); if (++digits > 4) return 0; }
        if (!digits) return 0;
        w[n++] = (uint16_t)v;
        if (!*s) break;
        if (*s++ != ':') return 0;
        if (*s == ':') { if (gap >= 0) return 0; gap = n; s++; }
    }
    if (gap >= 0) {
        int tail = n - gap;
        for (int i = 0; i < tail; i++) w[7 - i] = w[n - 1 - i];
        for (int i = gap; i < 8 - tail; i++) w[i] = 0;
    } else if (n != 8) return 0;
    for (int i = 0; i < 8; i++) { out[2 * i] = (uint8_t)(w[i] >> 8); out[2 * i + 1] = (uint8_t)w[i]; }
    return 1;
}

int inet_pton(int family, const char* s, void* out) {
    if (family == AF_INET) return inet_aton(s, out) ? 1 : 0;
    if (family == AF_INET6) return pton6(s, out) ? 1 : 0;
    errno = EAFNOSUPPORT;
    return -1;
}

const char* inet_ntop(int family, const void* a, char* out, socklen_t len) {
    char buf[INET6_ADDRSTRLEN];
    if (family == AF_INET) {
        const uint8_t* b = a;
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    } else if (family == AF_INET6) {
        const uint8_t* b = a;
        uint16_t w[8];
        for (int i = 0; i < 8; i++) w[i] = (uint16_t)(b[2 * i] << 8 | b[2 * i + 1]);
        int best = -1, bestn = 0;                        /* the longest run of zeros becomes "::" */
        for (int i = 0; i < 8; ) {
            if (w[i]) { i++; continue; }
            int j = i;
            while (j < 8 && !w[j]) j++;
            if (j - i > bestn && j - i > 1) { best = i; bestn = j - i; }
            i = j;
        }
        char* p = buf;
        for (int i = 0; i < 8; i++) {
            if (i == best) { p += sprintf(p, "::"); i += bestn - 1; continue; }
            p += sprintf(p, (i && i != best + bestn) ? ":%x" : "%x", w[i]);
        }
    } else {
        errno = EAFNOSUPPORT;
        return NULL;
    }
    if (strlen(buf) + 1 > len) { errno = ENOSPC; return NULL; }
    strcpy(out, buf);
    return out;
}

/* ── names ────────────────────────────────────────────────────────── */

static int lookup(const char* name, struct in_addr* out) {
    if (inet_aton(name, out)) return 0;
    /* what /etc/hosts says on Linux: localhost, and this machine's own name
     * (Java's InetAddress.getLocalHost(): log4j asks for it at every start) */
    char self[64];
    if (!strcasecmp(name, "localhost") || !strcasecmp(name, "localhost.localdomain") ||
        (gethostname(self, sizeof(self)) == 0 && !strcasecmp(name, self))) {
        out->s_addr = htonl(INADDR_LOOPBACK);
        return 0;
    }
    unsigned char ip[4];
    if (!has_sock() || __banana->resolve(name, ip) != 0) return -1;
    memcpy(&out->s_addr, ip, 4);
    return 0;
}

static int service_port(const char* serv, int* port) {
    if (!serv) { *port = 0; return 0; }
    char* end;
    long v = strtol(serv, &end, 10);
    if (*serv && !*end && v >= 0 && v <= 65535) { *port = (int)v; return 0; }
    static const struct { const char* n; int p; } known[] = {
        { "http", 80 }, { "https", 443 }, { "ftp", 21 }, { "ssh", 22 }, { "telnet", 23 }, { "smtp", 25 },
        { "domain", 53 }, { "ntp", 123 }, { "imap", 143 }, { "rdp", 3389 }, { "minecraft", 25565 },
    };
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); i++)
        if (!strcmp(serv, known[i].n)) { *port = known[i].p; return 0; }
    return -1;
}

int getaddrinfo(const char* node, const char* service, const struct addrinfo* hints, struct addrinfo** res) {
    int family = hints ? hints->ai_family : AF_UNSPEC;
    int flags = hints ? hints->ai_flags : 0;
    int socktype = hints ? hints->ai_socktype : 0;
    if (family != AF_UNSPEC && family != AF_INET) return EAI_FAMILY;
    if (!node && !service) return EAI_NONAME;
    int port;
    if (service_port(service, &port)) return EAI_SERVICE;
    struct in_addr a;
    if (!node) a.s_addr = (flags & AI_PASSIVE) ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
    else if ((flags & AI_NUMERICHOST) ? !inet_aton(node, &a) : lookup(node, &a) != 0) return EAI_NONAME;
    static const int types[2] = { SOCK_STREAM, SOCK_DGRAM };
    struct addrinfo* first = NULL;
    struct addrinfo** tail = &first;
    for (int t = 0; t < 2; t++) {
        if (socktype && socktype != types[t]) continue;
        struct addrinfo* ai = calloc(1, sizeof(struct addrinfo) + sizeof(struct sockaddr_in));
        if (!ai) { freeaddrinfo(first); return EAI_MEMORY; }
        struct sockaddr_in* sin = (struct sockaddr_in*)(ai + 1);
        sin->sin_family = AF_INET;
        sin->sin_port = htons((uint16_t)port);
        sin->sin_addr = a;
        ai->ai_family = AF_INET;
        ai->ai_socktype = types[t];
        ai->ai_protocol = types[t] == SOCK_STREAM ? IPPROTO_TCP : IPPROTO_UDP;
        ai->ai_addrlen = sizeof(struct sockaddr_in);
        ai->ai_addr = (struct sockaddr*)sin;
        if ((flags & AI_CANONNAME) && node && !first) ai->ai_canonname = strdup(node);
        *tail = ai;
        tail = &ai->ai_next;
    }
    if (!first) return EAI_SOCKTYPE;
    *res = first;
    return 0;
}

void freeaddrinfo(struct addrinfo* ai) {
    while (ai) {
        struct addrinfo* next = ai->ai_next;
        free(ai->ai_canonname);
        free(ai);
        ai = next;
    }
}

const char* gai_strerror(int code) {
    switch (code) {
    case 0:            return "Success";
    case EAI_NONAME:   return "Name or service not known";
    case EAI_AGAIN:    return "Temporary failure in name resolution";
    case EAI_FAMILY:   return "Address family not supported";
    case EAI_SOCKTYPE: return "Socket type not supported";
    case EAI_SERVICE:  return "Service not known";
    case EAI_MEMORY:   return "Out of memory";
    default:           return "Name lookup failed";
    }
}

int getnameinfo(const struct sockaddr* sa, socklen_t salen, char* host, socklen_t hostlen,
                char* serv, socklen_t servlen, int flags) {
    (void)flags;                                     /* (no reverse lookups: numbers) */
    if (!sa || salen < sizeof(struct sockaddr_in) || sa->sa_family != AF_INET) return EAI_FAMILY;
    const struct sockaddr_in* sin = (const struct sockaddr_in*)sa;
    if (host && hostlen && !inet_ntop(AF_INET, &sin->sin_addr, host, hostlen)) return EAI_OVERFLOW;
    if (serv && servlen) snprintf(serv, servlen, "%u", ntohs(sin->sin_port));
    return 0;
}

struct hostent* gethostbyname(const char* name) {
    static struct hostent h;
    static struct in_addr a;
    static char* addrs[2];
    static char* aliases[1];
    static char hname[256];
    if (!name || lookup(name, &a) != 0) { h_errno = HOST_NOT_FOUND; return NULL; }
    strncpy(hname, name, sizeof(hname) - 1);
    addrs[0] = (char*)&a;
    addrs[1] = NULL;
    aliases[0] = NULL;
    h.h_name = hname;
    h.h_aliases = aliases;
    h.h_addrtype = AF_INET;
    h.h_length = 4;
    h.h_addr_list = addrs;
    return &h;
}

struct hostent* gethostbyaddr(const void* addr, socklen_t len, int type) {
    static char name[INET_ADDRSTRLEN];
    if (type != AF_INET || len != 4) { h_errno = HOST_NOT_FOUND; return NULL; }
    inet_ntop(AF_INET, addr, name, sizeof(name));
    return gethostbyname(name);
}

struct servent* getservbyname(const char* name, const char* proto) {
    static struct servent s;
    static char* aliases[1];
    int port;
    if (service_port(name, &port) || !port) return NULL;
    s.s_name = (char*)name;
    s.s_aliases = aliases;
    s.s_port = htons((uint16_t)port);
    s.s_proto = (char*)(proto ? proto : "tcp");
    return &s;
}

int gethostname(char* name, size_t len) {
    const char* h = "banana-os";
    if (strlen(h) + 1 > len) { errno = ENAMETOOLONG; return -1; }
    strcpy(name, h);
    return 0;
}
