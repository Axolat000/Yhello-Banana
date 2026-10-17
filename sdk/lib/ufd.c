/* Banana OS SDK: the file descriptors the C library makes itself, where the
 * system has none of its own - eventfd counters, epoll sets (over poll()),
 * pipes and AF_UNIX socket pairs (between the app's threads). They are
 * numbered from UFD_BASE up, above the system's files and sockets (3..255,
 * kernel/app.c's APP_FD_MAX; all below select()'s FD_SETSIZE of 1024);
 * read, write, close, poll / select, fcntl, fstat, ioctl, send / recv,
 * shutdown and dup / dup2 know them. Java's selectors (epoll + eventfd)
 * and its FileDispatcherImpl (a socketpair, dup2 to close) run on these. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

int __kpoll(struct pollfd* fds, nfds_t n, int timeout_ms);       /* socket.c: the system's poll */

#define UFD_BASE  256
#define UFD_MAX   128
#define PIPE_SIZE 65536
#define EV_MAX    0xfffffffffffffffeull                         /* an eventfd's largest count */
#define SLICE_MS  10                     /* how often a wait on system fds looks at ours too */

enum { U_EVENT = 1, U_EPOLL, U_STREAM };

typedef struct {                         /* one direction of a pipe or a socket pair */
    char*    buf;                        /* (PIPE_SIZE, made on the first write) */
    unsigned head, len;
    int      readers, writers;
} upipe_t;

typedef struct {
    int      fd;
    uint32_t events;
    uint64_t data;
    int      off;                        /* an EPOLLONESHOT that fired: quiet until EPOLL_CTL_MOD */
} uent_t;

typedef struct {
    int       type, refs, nonblock;
    uint64_t  count;                     /* U_EVENT */
    int       semaphore;
    uent_t*   ents;                      /* U_EPOLL */
    int       nents, cap, rot;
    volatile int gen;                    /* (bumped by every epoll_ctl) */
    upipe_t*  in;                        /* U_STREAM: what read takes from, write puts in */
    upipe_t*  out;
    int       is_sock;
} uobj_t;

static uobj_t* g_fd[UFD_MAX];
static volatile int g_lk;                /* 0 free, 1 taken, 2 taken and waited for */
static volatile int g_seq;               /* bumped (and woken) on every change to ours */
static int g_nepoll;

/* ── the lock, and waiting for a change ───────────────────────────── */

static void lock(void) {
    int c = __sync_val_compare_and_swap(&g_lk, 0, 1);
    if (!c) return;
    if (c != 2) c = __sync_lock_test_and_set(&g_lk, 2);
    while (c) {
        __banana->wait(&g_lk, 2, -1);
        c = __sync_lock_test_and_set(&g_lk, 2);
    }
}

static void unlock(void) {
    if (__sync_fetch_and_sub(&g_lk, 1) != 1) {
        g_lk = 0;
        __banana->wake(&g_lk, 1);
    }
}

static void changed(void) {
    __sync_fetch_and_add(&g_seq, 1);
    __banana->wake(&g_seq, 0);
}

/* (the lock held) waits up to ms (-1: no limit) for the next change */
static void wait_change(int ms) {
    int seq = g_seq;
    unlock();
    __banana->wait(&g_seq, seq, ms);
    lock();
}

/* ── the table ────────────────────────────────────────────────────── */

int __ufd_is(int fd) { return fd >= UFD_BASE && fd < UFD_BASE + UFD_MAX && g_fd[fd - UFD_BASE]; }

static uobj_t* get(int fd) { return fd >= UFD_BASE && fd < UFD_BASE + UFD_MAX ? g_fd[fd - UFD_BASE] : NULL; }

static int slot_for(uobj_t* o, int from) {               /* (the lock held) the new fd, or -1 */
    for (int i = from > UFD_BASE ? from - UFD_BASE : 0; i < UFD_MAX; i++)
        if (!g_fd[i]) { g_fd[i] = o; o->refs++; return UFD_BASE + i; }
    errno = EMFILE;
    return -1;
}

static void drop_in(uobj_t* o) {
    upipe_t* p = o->in;
    if (!p) return;
    o->in = NULL;
    if (--p->readers == 0 && p->writers == 0) { free(p->buf); free(p); }
}

static void drop_out(uobj_t* o) {
    upipe_t* p = o->out;
    if (!p) return;
    o->out = NULL;
    if (--p->writers == 0 && p->readers == 0) { free(p->buf); free(p); }
}

static void unref(uobj_t* o) {
    if (--o->refs) return;
    drop_in(o);
    drop_out(o);
    if (o->type == U_EPOLL) { free(o->ents); g_nepoll--; }
    free(o);
    changed();
}

static uobj_t* new_obj(int type, int nonblock) {
    uobj_t* o = calloc(1, sizeof(uobj_t));
    if (!o) { errno = ENOMEM; return NULL; }
    o->type = type;
    o->nonblock = nonblock;
    return o;
}

static upipe_t* new_pipe(void) {
    upipe_t* p = calloc(1, sizeof(upipe_t));
    if (p) p->readers = p->writers = 1;
    return p;
}

/* a descriptor is gone: out of every epoll set (as Linux does) */
static void forget(int fd) {
    for (int i = 0; i < UFD_MAX; i++) {
        uobj_t* e = g_fd[i];
        if (!e || e->type != U_EPOLL) continue;
        for (int k = 0; k < e->nents; k++)
            if (e->ents[k].fd == fd) { e->ents[k] = e->ents[--e->nents]; e->gen++; break; }
    }
}

/* close() of one of the system's descriptors */
void __ufd_forget(int fd) {
    if (!g_nepoll) return;
    lock();
    forget(fd);
    changed();
    unlock();
}

int __ufd_close(int fd) {
    lock();
    uobj_t* o = get(fd);
    if (!o) { unlock(); errno = EBADF; return -1; }
    g_fd[fd - UFD_BASE] = NULL;
    forget(fd);
    unref(o);
    changed();
    unlock();
    return 0;
}

/* ── read and write ───────────────────────────────────────────────── */

ssize_t __ufd_read(int fd, void* buf, size_t n, int dontwait) {
    lock();
    uobj_t* o = get(fd);
    if (!o) { unlock(); errno = EBADF; return -1; }
    o->refs++;
    ssize_t r;
    for (;;) {
        if (o->type == U_EVENT) {
            if (n < 8) { errno = EINVAL; r = -1; break; }
            if (o->count) {
                uint64_t v = o->semaphore ? 1 : o->count;
                o->count -= v;
                memcpy(buf, &v, 8);
                changed();
                r = 8;
                break;
            }
        } else if (o->type == U_STREAM) {
            upipe_t* p = o->in;
            if (!p) { if (o->is_sock) { r = 0; } else { errno = EBADF; r = -1; } break; }   /* (shut down) */
            if (!n) { r = 0; break; }
            if (p->len) {
                size_t got = 0;
                while (got < n && p->len) {
                    unsigned run = PIPE_SIZE - p->head;
                    if (run > p->len) run = p->len;
                    if (run > n - got) run = (unsigned)(n - got);
                    memcpy((char*)buf + got, p->buf + p->head, run);
                    p->head = (p->head + run) % PIPE_SIZE;
                    p->len -= run;
                    got += run;
                }
                changed();
                r = (ssize_t)got;
                break;
            }
            if (!p->writers) { r = 0; break; }                       /* the end */
        } else {
            errno = EINVAL;
            r = -1;
            break;
        }
        if (o->nonblock || dontwait) { errno = EAGAIN; r = -1; break; }
        wait_change(-1);
    }
    unref(o);
    unlock();
    return r;
}

ssize_t __ufd_write(int fd, const void* buf, size_t n, int dontwait) {
    lock();
    uobj_t* o = get(fd);
    if (!o) { unlock(); errno = EBADF; return -1; }
    o->refs++;
    ssize_t r;
    size_t done = 0;
    for (;;) {
        if (o->type == U_EVENT) {
            uint64_t v;
            if (n < 8) { errno = EINVAL; r = -1; break; }
            memcpy(&v, buf, 8);
            if (v == ~0ull) { errno = EINVAL; r = -1; break; }
            if (v <= EV_MAX - o->count) {
                o->count += v;
                changed();
                r = 8;
                break;
            }
        } else if (o->type == U_STREAM) {
            upipe_t* p = o->out;
            if (!p || !p->readers) { errno = EPIPE; r = done ? (ssize_t)done : -1; break; }
            if (done == n) { r = (ssize_t)n; break; }
            if (!p->buf && !(p->buf = malloc(PIPE_SIZE))) { errno = ENOMEM; r = done ? (ssize_t)done : -1; break; }
            if (p->len < PIPE_SIZE) {
                while (done < n && p->len < PIPE_SIZE) {
                    unsigned tail = (p->head + p->len) % PIPE_SIZE;
                    unsigned run = tail >= p->head ? PIPE_SIZE - tail : p->head - tail;
                    if (run > PIPE_SIZE - p->len) run = PIPE_SIZE - p->len;
                    if (run > n - done) run = (unsigned)(n - done);
                    memcpy(p->buf + tail, (const char*)buf + done, run);
                    p->len += run;
                    done += run;
                }
                changed();
                continue;                                            /* (all of it, or wait for room) */
            }
        } else {
            errno = EINVAL;
            r = -1;
            break;
        }
        if (o->nonblock || dontwait) {
            if (done) { r = (ssize_t)done; break; }
            errno = EAGAIN;
            r = -1;
            break;
        }
        wait_change(-1);
    }
    unref(o);
    unlock();
    return r;
}

/* ── readiness, poll ──────────────────────────────────────────────── */

static int epoll_ready(uobj_t* e, int depth);

/* (the lock held) what one of ours is ready for, as poll's revents */
static int ready(uobj_t* o, int events, int depth) {
    int r = 0;
    if (o->type == U_EVENT) {
        if (o->count) r |= POLLIN;
        if (o->count < EV_MAX) r |= POLLOUT;
    } else if (o->type == U_STREAM) {
        upipe_t* i = o->in;
        upipe_t* w = o->out;
        if (i && i->len) r |= POLLIN;
        if (o->is_sock) {
            if (!i || !i->writers) r |= POLLIN | POLLRDHUP;
            if ((!i || !i->writers) && (!w || !w->readers)) r |= POLLHUP;
            if (w && w->readers && w->len < PIPE_SIZE) r |= POLLOUT;
            if (w && !w->readers) r |= POLLOUT | POLLERR;
        } else {
            if (i && !i->writers) r |= POLLHUP;
            if (w && !w->readers) r |= POLLERR;
            else if (w && w->len < PIPE_SIZE) r |= POLLOUT;
        }
    } else if (o->type == U_EPOLL) {
        if (depth < 4 && epoll_ready(o, depth + 1)) r |= POLLIN;
    }
    return r & (events | POLLERR | POLLHUP);
}

/* (the lock held) how many of an epoll set's descriptors are ready now */
static int epoll_ready(uobj_t* e, int depth) {
    int n = e->nents, count = 0, nk = 0;
    if (!n) return 0;
    struct pollfd* pf = malloc((size_t)n * sizeof(struct pollfd));
    if (!pf) return 0;
    for (int i = 0; i < n; i++) {
        const uent_t* x = &e->ents[i];
        uobj_t* o = get(x->fd);
        pf[i].fd = -1;
        pf[i].events = (short)x->events;
        pf[i].revents = 0;
        if (x->off) continue;
        if (o) { if (ready(o, (int)x->events, depth)) count++; }
        else { pf[i].fd = x->fd; nk++; }
    }
    if (!count && nk) {
        int k = __kpoll(pf, (nfds_t)n, 0);
        for (int i = 0; i < n; i++) if (pf[i].fd >= 0 && (pf[i].revents & POLLNVAL)) k--;
        if (k > 0) count += k;
    }
    free(pf);
    return count;
}

/* poll() over ours and the system's: the system's in slices of SLICE_MS,
 * ours in between; or, with only ours, a wait for the next change. With
 * watch, 0 as soon as *watch is no longer wval (an epoll set changed). */
static int upoll(struct pollfd* fds, nfds_t n, int timeout_ms, volatile int* watch, int wval) {
    nfds_t nk = 0;
    for (nfds_t i = 0; i < n; i++) if (fds[i].fd >= 0 && !get(fds[i].fd)) nk++;
    struct pollfd* kf = NULL;
    if (nk) {
        kf = malloc(n * sizeof(struct pollfd));
        if (!kf) { errno = ENOMEM; return -1; }
    }
    unsigned start = __banana->ticks_ms();
    for (;;) {
        int nu = 0;
        lock();
        int seq = g_seq;
        for (nfds_t i = 0; i < n; i++) {
            uobj_t* o = fds[i].fd >= 0 ? get(fds[i].fd) : NULL;
            if (kf) { kf[i] = fds[i]; if (o) kf[i].fd = -1; }
            if (o) {
                fds[i].revents = (short)ready(o, fds[i].events, 0);
                if (fds[i].revents) nu++;
            } else if (fds[i].fd < 0) fds[i].revents = 0;
        }
        unlock();
        int left = -1;
        if (timeout_ms >= 0) {
            unsigned took = __banana->ticks_ms() - start;
            left = took >= (unsigned)timeout_ms ? 0 : timeout_ms - (int)took;
        }
        int nkr = 0;
        if (nk) {
            int slice = nu ? 0 : (left < 0 || left > SLICE_MS) ? SLICE_MS : left;
            nkr = __kpoll(kf, n, slice);
            if (nkr < 0) { free(kf); return -1; }
            for (nfds_t i = 0; i < n; i++) if (kf[i].fd >= 0) fds[i].revents = kf[i].revents;
        }
        if (nu + nkr || !left) { free(kf); return nu + nkr; }
        if (watch && *watch != wval) { free(kf); return 0; }
        if (!nk) {
            if (g_seq == seq) __banana->wait(&g_seq, seq, left);
        }
    }
}

int __ufd_poll(struct pollfd* fds, nfds_t n, int timeout_ms) { return upoll(fds, n, timeout_ms, NULL, 0); }

/* ── eventfd ──────────────────────────────────────────────────────── */

int eventfd(unsigned int initval, int flags) {
    if (flags & ~(EFD_SEMAPHORE | EFD_CLOEXEC | EFD_NONBLOCK)) { errno = EINVAL; return -1; }
    uobj_t* o = new_obj(U_EVENT, (flags & EFD_NONBLOCK) != 0);
    if (!o) return -1;
    o->count = initval;
    o->semaphore = (flags & EFD_SEMAPHORE) != 0;
    lock();
    int fd = slot_for(o, 0);
    unlock();
    if (fd < 0) free(o);
    return fd;
}

int eventfd_read(int fd, eventfd_t* value) { return read(fd, value, sizeof(*value)) == (ssize_t)sizeof(*value) ? 0 : -1; }
int eventfd_write(int fd, eventfd_t value) { return write(fd, &value, sizeof(value)) == (ssize_t)sizeof(value) ? 0 : -1; }

/* ── epoll ────────────────────────────────────────────────────────── */

int epoll_create1(int flags) {
    if (flags & ~EPOLL_CLOEXEC) { errno = EINVAL; return -1; }
    uobj_t* o = new_obj(U_EPOLL, 0);
    if (!o) return -1;
    lock();
    int fd = slot_for(o, 0);
    if (fd >= 0) g_nepoll++;
    unlock();
    if (fd < 0) free(o);
    return fd;
}

int epoll_create(int size) {
    if (size <= 0) { errno = EINVAL; return -1; }
    return epoll_create1(0);
}

static int find_ent(uobj_t* e, int fd) {
    for (int i = 0; i < e->nents; i++) if (e->ents[i].fd == fd) return i;
    return -1;
}

int epoll_ctl(int ep, int op, int fd, struct epoll_event* ev) {
    if (fd < 0) { errno = EBADF; return -1; }
    if (!get(fd) && (op == EPOLL_CTL_ADD || op == EPOLL_CTL_MOD)) {    /* one of the system's: open? */
        struct pollfd pf = { fd, 0, 0 };
        if (__kpoll(&pf, 1, 0) < 0 || (pf.revents & POLLNVAL)) { errno = EBADF; return -1; }
    }
    lock();
    uobj_t* e = get(ep);
    int rc = -1;
    if (!e) errno = EBADF;
    else if (e->type != U_EPOLL || fd == ep) errno = EINVAL;
    else if (op != EPOLL_CTL_DEL && !ev) errno = EFAULT;
    else {
        int i = find_ent(e, fd);
        if (op == EPOLL_CTL_ADD) {
            uent_t* grown = e->ents;
            if (i < 0 && e->nents == e->cap) {
                grown = realloc(e->ents, (size_t)(e->cap ? e->cap * 2 : 16) * sizeof(uent_t));
                if (grown) { e->ents = grown; e->cap = e->cap ? e->cap * 2 : 16; }
            }
            if (i >= 0) errno = EEXIST;
            else if (!grown) errno = ENOMEM;
            else {
                uent_t* x = &e->ents[e->nents++];
                x->fd = fd;
                x->events = ev->events;
                x->data = ev->data.u64;
                x->off = 0;
                rc = 0;
            }
        } else if (op == EPOLL_CTL_MOD) {
            if (i < 0) errno = ENOENT;
            else { e->ents[i].events = ev->events; e->ents[i].data = ev->data.u64; e->ents[i].off = 0; rc = 0; }
        } else if (op == EPOLL_CTL_DEL) {
            if (i < 0) errno = ENOENT;
            else { e->ents[i] = e->ents[--e->nents]; rc = 0; }
        } else errno = EINVAL;
        if (rc == 0) { e->gen++; changed(); }
    }
    unlock();
    return rc;
}

/* level-triggered (EPOLLET is taken as that too); EPOLLONESHOT as Linux */
int epoll_wait(int ep, struct epoll_event* evs, int max, int timeout_ms) {
    if (max <= 0 || !evs) { errno = EINVAL; return -1; }
    lock();
    uobj_t* e = get(ep);
    if (!e || e->type != U_EPOLL) { unlock(); errno = e ? EINVAL : EBADF; return -1; }
    e->refs++;
    unsigned start = __banana->ticks_ms();
    uent_t* snap = NULL;
    struct pollfd* pf = NULL;
    int cap = 0, rc = 0;
    for (;;) {
        int n = 0, gen = e->gen;
        if (e->nents > cap) {
            cap = e->nents;
            free(snap);
            free(pf);
            snap = malloc((size_t)cap * sizeof(uent_t));
            pf = malloc((size_t)cap * sizeof(struct pollfd));
            if (!snap || !pf) { errno = ENOMEM; rc = -1; break; }
        }
        for (int i = 0; i < e->nents; i++) {
            if (e->ents[i].off) continue;
            snap[n] = e->ents[i];
            pf[n].fd = snap[n].fd;
            pf[n].events = (short)(snap[n].events & (EPOLLIN | EPOLLPRI | EPOLLOUT | EPOLLRDHUP));
            pf[n].revents = 0;
            n++;
        }
        unlock();
        int left = -1;
        if (timeout_ms >= 0) {
            unsigned took = __banana->ticks_ms() - start;
            left = took >= (unsigned)timeout_ms ? 0 : timeout_ms - (int)took;
        }
        int r = upoll(pf, (nfds_t)n, left, &e->gen, gen);
        lock();
        if (r < 0) { rc = -1; break; }
        int count = 0;
        if (r > 0) {
            for (int k = 0; k < n && count < max; k++) {
                int i = (e->rot + k) % n;
                int rev = pf[i].revents;
                if (!rev) continue;
                int at = find_ent(e, snap[i].fd);
                if (at < 0 || e->ents[at].off) continue;                   /* (taken out meanwhile) */
                if (rev & POLLNVAL) {                                     /* closed: no longer watched */
                    e->ents[at] = e->ents[--e->nents];
                    e->gen++;
                    continue;
                }
                uent_t* x = &e->ents[at];
                uint32_t got = (uint32_t)rev & (x->events | EPOLLERR | EPOLLHUP);
                if (!got) continue;
                if (x->events & EPOLLONESHOT) x->off = 1;
                evs[count].events = got;
                evs[count].data.u64 = x->data;
                count++;
            }
            e->rot++;
        }
        if (count) { rc = count; break; }
        if (timeout_ms >= 0 && __banana->ticks_ms() - start >= (unsigned)timeout_ms) { rc = 0; break; }
    }
    free(snap);
    free(pf);
    unref(e);
    unlock();
    return rc;
}

int epoll_pwait(int ep, struct epoll_event* evs, int max, int timeout_ms, const sigset_t* mask) {
    (void)mask;
    return epoll_wait(ep, evs, max, timeout_ms);
}

/* ── pipes and socket pairs ───────────────────────────────────────── */

static int make_pair(int fds[2], int sock, int nonblock) {
    uobj_t* a = new_obj(U_STREAM, nonblock);
    uobj_t* b = new_obj(U_STREAM, nonblock);
    upipe_t* p = new_pipe();
    upipe_t* q = sock ? new_pipe() : NULL;
    if (!a || !b || !p || (sock && !q)) { free(a); free(b); free(p); free(q); errno = ENOMEM; return -1; }
    a->is_sock = b->is_sock = sock;
    a->in = p;  b->out = p;               /* pipe: fds[1] writes, fds[0] reads */
    if (sock) { a->out = q; b->in = q; }  /* socket pair: both ways */
    lock();
    int x = slot_for(a, 0), y = x >= 0 ? slot_for(b, 0) : -1;
    if (y < 0) {
        if (x >= 0) { g_fd[x - UFD_BASE] = NULL; a->refs--; }
        unlock();
        free(a); free(b); free(p); free(q);
        return -1;
    }
    unlock();
    fds[0] = x;
    fds[1] = y;
    return 0;
}

int pipe2(int fds[2], int flags) {
    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) { errno = EINVAL; return -1; }
    return make_pair(fds, 0, (flags & O_NONBLOCK) != 0);
}
int pipe(int fds[2]) { return make_pair(fds, 0, 0); }

int socketpair(int family, int type, int proto, int fds[2]) {
    int base = type & ~(SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (family != AF_UNIX) { errno = EAFNOSUPPORT; return -1; }
    if (base != SOCK_STREAM || proto != 0) { errno = EOPNOTSUPP; return -1; }
    return make_pair(fds, 1, (type & SOCK_NONBLOCK) != 0);
}

int __ufd_shutdown(int fd, int how) {
    lock();
    uobj_t* o = get(fd);
    int rc = 0;
    if (!o) { errno = EBADF; rc = -1; }
    else if (o->type != U_STREAM || !o->is_sock) { errno = ENOTSOCK; rc = -1; }
    else if (how < SHUT_RD || how > SHUT_RDWR) { errno = EINVAL; rc = -1; }
    else {
        if (how != SHUT_WR) drop_in(o);
        if (how != SHUT_RD) drop_out(o);
        changed();
    }
    unlock();
    return rc;
}

/* ── dup, fcntl, fstat, ioctl ─────────────────────────────────────── */

static int dup_from(int fd, int from) {
    lock();
    uobj_t* o = get(fd);
    int r = o ? slot_for(o, from) : (errno = EBADF, -1);
    unlock();
    return r;
}

int dup(int fd) {
    if (__ufd_is(fd)) return dup_from(fd, 0);
    errno = EMFILE;                         /* (the system's descriptors are not shared) */
    return -1;
}

/* Ours onto ours: as Linux. Ours onto one of the system's sockets: that
 * socket is shut down for reading and for writing (one at a time: the
 * system's SHUT_RDWR drops the connection) and keeps its number until
 * close() - what
 * dup2 of a dead socket pair end over it is for (Java closes sockets so,
 * waking threads blocked on them). Onto a file: nothing to do. */
int dup2(int fd, int to) {
    if (fd == to) {
        if (__ufd_is(fd)) return to;
        struct pollfd pf = { fd, 0, 0 };
        if (fd >= 0 && (fd <= 2 || (__kpoll(&pf, 1, 0) >= 0 && !(pf.revents & POLLNVAL)))) return to;
        errno = EBADF;
        return -1;
    }
    if (!__ufd_is(fd)) { errno = EBADF; return -1; }
    if (to >= UFD_BASE && to < UFD_BASE + UFD_MAX) {
        lock();
        uobj_t* o = get(fd);
        if (!o) { unlock(); errno = EBADF; return -1; }
        uobj_t* old = g_fd[to - UFD_BASE];
        g_fd[to - UFD_BASE] = o;
        o->refs++;
        if (old) { forget(to); unref(old); }
        changed();
        unlock();
        return to;
    }
    if (to < 3) { errno = EBADF; return -1; }
    struct pollfd pf = { to, 0, 0 };
    if (__kpoll(&pf, 1, 0) < 0 || (pf.revents & POLLNVAL)) { errno = EBADF; return -1; }
    if (__banana->version >= 18 && __banana->sock_ctl(to, 2, 0) >= 0) {
        __banana->sock_shutdown(to, SHUT_RD);
        __banana->sock_shutdown(to, SHUT_WR);
    }
    return to;
}

int dup3(int fd, int to, int flags) {
    if (fd == to || (flags & ~O_CLOEXEC)) { errno = EINVAL; return -1; }
    return dup2(fd, to);
}

int __ufd_fcntl(int fd, int cmd, long arg) {
    lock();
    uobj_t* o = get(fd);
    if (!o) { unlock(); errno = EBADF; return -1; }
    int r = 0;
    if (cmd == F_GETFL) r = O_RDWR | (o->nonblock ? O_NONBLOCK : 0);
    else if (cmd == F_SETFL) o->nonblock = (arg & O_NONBLOCK) != 0;
    else if (cmd == F_DUPFD || cmd == F_DUPFD_CLOEXEC) { unlock(); return dup_from(fd, (int)arg); }
    unlock();
    return r;
}

int __ufd_fstat(int fd, struct stat* st) {
    lock();
    uobj_t* o = get(fd);
    if (!o) { unlock(); errno = EBADF; return -1; }
    memset(st, 0, sizeof(*st));
    st->st_mode = o->type != U_STREAM ? 0600 : o->is_sock ? S_IFSOCK | 0777 : S_IFIFO | 0600;
    st->st_nlink = 1;
    st->st_uid = st->st_gid = 1000;
    st->st_ino = (ino_t)fd;
    st->st_blksize = 4096;
    unlock();
    return 0;
}

int __ufd_ioctl(int fd, unsigned long req, void* arg) {
    lock();
    uobj_t* o = get(fd);
    int rc = 0;
    if (!o) { errno = EBADF; rc = -1; }
    else if (req == FIONBIO) o->nonblock = *(int*)arg != 0;
    else if (req == FIONREAD && o->type == U_STREAM) *(int*)arg = o->in ? (int)o->in->len : 0;
    else { errno = ENOTTY; rc = -1; }
    unlock();
    return rc;
}
