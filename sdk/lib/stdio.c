/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK stdio: printf/scanf family, FILE streams over the
 * system's file calls, and the terminal as stdin/stdout/stderr. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <unistd.h>
#include <math.h>
#include <fcntl.h>
#include "banana_api.h"

extern const banana_api_t* __banana;
void __time_init(void);
void __pthread_init(void);

/* A stream: a descriptor with a buffer used for reading or for writing
 * (whichever came last), or a stream in memory. Every stream is on a list
 * so that fflush(NULL) and exit() write out what waits. */
#define F_READ    1
#define F_WRITE   2
#define F_APPEND  4
#define F_STATIC  8               /* stdin, stdout, stderr: never freed */
#define F_OWNBUF  16              /* buf is ours (malloc'd) */

struct _bfile {
    int  fd;
    int  flags;
    int  eof, err;
    int  mode, mode_set;      /* _IOFBF / _IOLBF / _IONBF; set by setvbuf (else chosen at first use) */
    int  last;                /* 'r' or 'w': what the buffer holds */
    unsigned char* buf;
    size_t size;
    size_t rpos, rlen;        /* reading: buf[rpos..rlen) not yet taken */
    size_t wlen;              /* writing: buf[0..wlen) not yet written */
    unsigned char back[8];    /* ungetc's bytes (when they do not fit back into buf) */
    int  nback;
    unsigned char one;        /* the buffer of an unbuffered stream */
    char* tmpname;            /* tmpfile(): removed at fclose */
    /* a stream in memory: 1 open_memstream (grows, *mout / *msize kept
     * up to date), 2 fmemopen (a fixed buffer) */
    int  mem;
    char* mbuf;
    size_t mcap, mlen, mpos;
    char** mout;
    size_t* msize;
    /* flockfile: a lock its owner may take again */
    volatile int lk;
    int  owner, depth;
    struct _bfile* next;
};

static struct _bfile g_std[3];
FILE* stdin  = &g_std[0];
FILE* stdout = &g_std[1];
FILE* stderr = &g_std[2];
static FILE* g_files;                 /* the other streams */
static volatile int g_files_lk;

void __libc_init(void) {
    for (int i = 0; i < 3; i++) {
        g_std[i].fd = i;
        g_std[i].flags = (i ? F_WRITE : F_READ) | F_STATIC;
    }
    g_std[2].mode = _IONBF;
    g_std[2].mode_set = 1;
    __time_init();
    __pthread_init();                   /* the main thread's record (and TLS) */
}

/* ── locks ───────────────────────────────────────────────────────── */

static int me(void) { return 1 + (__banana->version >= 3 ? __banana->thread_id() : 0); }
/* (__sync: TinyCC builds this file too, ports/tcc) */
static void spin(volatile int* l) { while (__sync_lock_test_and_set(l, 1)) __banana->yield(); }
static void unspin(volatile int* l) { __sync_lock_release(l); }

void flockfile(FILE* f) {
    int id = me();
    if (f->owner == id) { f->depth++; return; }
    spin(&f->lk);
    f->owner = id;
    f->depth = 1;
}
int ftrylockfile(FILE* f) {
    int id = me();
    if (f->owner == id) { f->depth++; return 0; }
    if (__sync_lock_test_and_set(&f->lk, 1)) return -1;
    f->owner = id;
    f->depth = 1;
    return 0;
}
void funlockfile(FILE* f) {
    if (--f->depth == 0) { f->owner = 0; unspin(&f->lk); }
}
#define LOCK(f)   flockfile(f)
#define UNLOCK(f) funlockfile(f)

/* ── the descriptor underneath ───────────────────────────────────── */

int __ufd_is(int fd);
ssize_t __ufd_write(int fd, const void* buf, size_t n, int dontwait);

/* a write of the system (write() below first writes out stdout / stderr) */
static long raw_write(int fd, const void* buf, size_t n) {
    if (__ufd_is(fd)) return __ufd_write(fd, buf, n, 0);
    long r = __banana->write(fd, buf, n);
    if (r < 0) { errno = r < -1 ? (int)-r : EIO; return -1; }     /* (a socket's: -errno) */
    return r;
}

ssize_t write(int fd, const void* buf, size_t n) {
    if (fd == 1 && stdout->wlen) fflush(stdout);
    if (fd == 2 && stderr->wlen) fflush(stderr);
    return raw_write(fd, buf, n);
}

static void mem_publish(FILE* f) {
    if (f->mem != 1) return;
    *f->mout = f->mbuf;
    *f->msize = f->mlen;
}

static int mem_write(FILE* f, const char* s, size_t n) {
    size_t end = f->mpos + n;
    if (f->mem == 1 && end + 1 > f->mcap) {             /* (+1: a 0 after the data) */
        size_t cap = f->mcap * 2 > end + 1 ? f->mcap * 2 : end + 1;
        char* b = realloc(f->mbuf, cap);
        if (!b) { f->err = 1; return -1; }
        f->mbuf = b;
        f->mcap = cap;
    }
    if (f->mem == 2 && end > f->mcap) n = f->mcap > f->mpos ? f->mcap - f->mpos : 0;
    memcpy(f->mbuf + f->mpos, s, n);
    f->mpos += n;
    if (f->mpos > f->mlen) f->mlen = f->mpos;
    if (f->mem == 1 || f->mlen < f->mcap) f->mbuf[f->mlen] = 0;
    mem_publish(f);
    return (int)n;
}

/* the buffer, and how it is used, at the stream's first use */
static void setup(FILE* f) {
    if (f->buf) return;
    if (!f->mode_set) {
        f->mode = isatty(f->fd) ? _IOLBF : _IOFBF;
        f->mode_set = 1;
    }
    if (f->mode != _IONBF) {
        f->buf = malloc(BUFSIZ);
        if (f->buf) { f->size = BUFSIZ; f->flags |= F_OWNBUF; }
    }
    if (!f->buf) { f->buf = &f->one; f->size = 1; f->mode = _IONBF; }
}

/* what waits to be written, written (0, or EOF) */
static int flush_out(FILE* f) {
    size_t done = 0;
    while (done < f->wlen) {
        long w = raw_write(f->fd, f->buf + done, f->wlen - done);
        if (w <= 0) {
            if (w < 0 && errno == EINTR) continue;
            f->err = 1;
            /* (what could not be written is dropped: the error is reported) */
            f->wlen = 0;
            return EOF;
        }
        done += (size_t)w;
    }
    f->wlen = 0;
    return 0;
}

/* the read-ahead given back: the descriptor moved to where the reader is */
static void drop_input(FILE* f) {
    size_t unread = f->rlen - f->rpos + (size_t)f->nback;
    if (unread && f->last == 'r') lseek(f->fd, -(off_t)unread, SEEK_CUR);
    f->rpos = f->rlen = 0;
    f->nback = 0;
}

static int flush_locked(FILE* f) {
    if (f->mem) return 0;
    if (f->last == 'w') return flush_out(f);
    if (f->last == 'r') drop_input(f);
    return 0;
}

int fflush(FILE* f) {
    if (!f) {
        int r = 0;
        for (int i = 1; i < 3; i++) if (g_std[i].wlen && fflush(&g_std[i])) r = EOF;
        spin(&g_files_lk);
        for (FILE* s = g_files; s; s = s->next) if (s->wlen && ftrylockfile(s) == 0) {
            if (flush_out(s)) r = EOF;
            funlockfile(s);
        }
        unspin(&g_files_lk);
        return r;
    }
    LOCK(f);
    int r = flush_locked(f);
    UNLOCK(f);
    return r;
}

void __stdio_flush_all(void) { fflush(NULL); }

/* bytes into the stream (it is locked) */
static int put_bytes(FILE* f, const char* s, size_t n) {
    if (!f) return -1;
    if (f->mem) return mem_write(f, s, n);
    if (!(f->flags & F_WRITE)) { f->err = 1; errno = EBADF; return -1; }
    setup(f);
    if (f->last == 'r') drop_input(f);
    f->last = 'w';
    if (f->mode == _IONBF || n >= f->size) {
        if (f->wlen && flush_out(f)) return -1;
        size_t done = 0;
        while (done < n) {
            long w = raw_write(f->fd, s + done, n - done);
            if (w <= 0) {
                if (w < 0 && errno == EINTR) continue;
                f->err = 1;
                return -1;
            }
            done += (size_t)w;
        }
        return (int)n;
    }
    int nl = 0;
    for (size_t i = 0; i < n; i++) {
        if (f->wlen == f->size && flush_out(f)) return -1;
        f->buf[f->wlen++] = (unsigned char)s[i];
        if (s[i] == '\n') nl = 1;
    }
    if (nl && f->mode == _IOLBF && flush_out(f)) return -1;
    return (int)n;
}

/* ── formatting ───────────────────────────────────────────────────── */

/* (to a stream: in pieces of tb - an unbuffered one gets one write a piece) */
typedef struct { char* buf; size_t cap, n; FILE* f; char tb[256]; int tn; } out_t;

static void out_flush(out_t* o) {
    if (o->tn) put_bytes(o->f, o->tb, (size_t)o->tn);
    o->tn = 0;
}

static void out_c(out_t* o, char c) {
    if (o->f) {
        o->tb[o->tn++] = c;
        if (o->tn == (int)sizeof(o->tb)) out_flush(o);
        o->n++;
        return;
    }
    if (o->n + 1 < o->cap) o->buf[o->n] = c;
    o->n++;
}

static void out_pad(out_t* o, char c, int n) { while (n-- > 0) out_c(o, c); }

/* ── floating point: %f %e %g ─────────────────────────────────────── */

/* the decimal digits of the whole number w (w >= 0, < 1e308) into d[], most significant first */
static int whole_digits(double w, char* d, int cap) {
    char tmp[320];
    int n = 0;
    if (w < 1) { d[0] = '0'; return 1; }
    while (w >= 1 && n < (int)sizeof(tmp)) {
        double q = floor(w / 10);
        int digit = (int)(w - q * 10);
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        tmp[n++] = (char)('0' + digit);
        w = q;
    }
    int k = 0;
    while (n && k < cap) d[k++] = tmp[--n];
    return k;
}

/* v with `prec` digits after the point into s (no exponent) */
static int fixed(char* s, int cap, double v, int prec, int alt) {
    double scale = pow(10, prec);
    double r = floor(v * scale + 0.5);             /* rounded, in units of 10^-prec */
    double w = floor(r / scale);
    double f = r - w * scale;
    int n = whole_digits(w, s, cap - prec - 2);
    if (prec > 0 || alt) s[n++] = '.';
    char fd[64];
    int fn = whole_digits(f, fd, sizeof(fd));
    for (int i = fn; i < prec; i++) s[n++] = '0';  /* leading zeros of the fraction */
    for (int i = 0; i < fn && fn <= prec; i++) s[n++] = fd[i];
    return n;
}

static int fmt_double(char* s, int cap, double v, int prec, char conv, int alt, int plus, int space) {
    int n = 0;
    if (v < 0 || (v == 0 && 1 / v < 0)) { s[n++] = '-'; v = -v; }
    else if (plus) s[n++] = '+';
    else if (space) s[n++] = ' ';
    int upper = conv == 'F' || conv == 'E' || conv == 'G';
    if (isnan(v) || isinf(v)) {
        const char* t = isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        while (*t) s[n++] = *t++;
        return n;
    }
    if (prec < 0) prec = 6;
    if (prec > 60) prec = 60;
    char c = conv | 32;                            /* f, e or g */
    int exp10 = 0;
    if (v != 0) {
        exp10 = (int)floor(log10(v));
        /* log10 can be one off near powers of ten */
        if (v / pow(10, exp10) >= 10) exp10++;
        if (v / pow(10, exp10) < 1) exp10--;
    }
    int strip = 0;
    if (c == 'g') {
        int p = prec ? prec : 1;
        /* rounding can carry into the next power of ten */
        double rounded = floor(v / pow(10, exp10 - p + 1) + 0.5) * pow(10, exp10 - p + 1);
        if (rounded != 0 && rounded >= pow(10, exp10 + 1)) exp10++;
        if (exp10 < -4 || exp10 >= p) { c = 'e'; prec = p - 1; }
        else { c = 'f'; prec = p - 1 - exp10; }
        strip = !alt;
    }
    if (c == 'f') {
        n += fixed(s + n, cap - n, v, prec, alt);
    } else {
        double m = v == 0 ? 0 : v / pow(10, exp10);
        if (floor(m * pow(10, prec) + 0.5) >= 10 * pow(10, prec)) { m /= 10; exp10++; }
        n += fixed(s + n, cap - n - 6, m, prec, alt);
    }
    if (strip && memchr(s, '.', (size_t)n)) {
        while (n > 0 && s[n - 1] == '0') n--;
        if (n > 0 && s[n - 1] == '.') n--;
    }
    if (c == 'e') {
        s[n++] = upper ? 'E' : 'e';
        s[n++] = exp10 < 0 ? '-' : '+';
        int ex = exp10 < 0 ? -exp10 : exp10;
        if (ex >= 100) s[n++] = (char)('0' + ex / 100);
        s[n++] = (char)('0' + ex / 10 % 10);
        s[n++] = (char)('0' + ex % 10);
    }
    return n;
}

static int vfmt(out_t* o, const char* fmt, va_list ap) {
    for (; *fmt; fmt++) {
        if (*fmt != '%') { out_c(o, *fmt); continue; }
        fmt++;
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        for (;; fmt++) {
            if (*fmt == '-') left = 1;
            else if (*fmt == '0') zero = 1;
            else if (*fmt == '+') plus = 1;
            else if (*fmt == ' ') space = 1;
            else if (*fmt == '#') alt = 1;
            else break;
        }
        int width = 0, prec = -1;
        if (*fmt == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } fmt++; }
        else while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
            else while (isdigit((unsigned char)*fmt)) prec = prec * 10 + (*fmt++ - '0');
        }
        int lng = 0;                      /* 1 long, 2 long long, 3 size_t */
        while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' || *fmt == 't') {
            if (*fmt == 'l') lng++;
            else if (*fmt == 'z' || *fmt == 't') lng = 3;
            else if (*fmt == 'j') lng = 2;
            fmt++;
        }
        char c = *fmt;
        if (!c) break;
        if (c == '%') { out_c(o, '%'); continue; }
        if (c == 'c') {
            if (!left) out_pad(o, ' ', width - 1);
            out_c(o, (char)va_arg(ap, int));
            if (left) out_pad(o, ' ', width - 1);
            continue;
        }
        if (c == 's') {
            const char* s = va_arg(ap, const char*);
            if (!s) s = "(null)";
            int l = (int)strlen(s);
            if (prec >= 0 && l > prec) l = prec;
            if (!left) out_pad(o, ' ', width - l);
            for (int i = 0; i < l; i++) out_c(o, s[i]);
            if (left) out_pad(o, ' ', width - l);
            continue;
        }
        if (c == 'n') { int* p = va_arg(ap, int*); if (p) *p = (int)o->n; continue; }
        if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
            char num[400];
            int n = fmt_double(num, sizeof(num), va_arg(ap, double), prec, c, alt, plus, space);
            int pad = width - n;
            if (zero && !left && pad > 0 && (num[0] == '-' || num[0] == '+' || num[0] == ' ')) {
                out_c(o, num[0]);
                out_pad(o, '0', pad);
                for (int i = 1; i < n; i++) out_c(o, num[i]);
                continue;
            }
            if (!left) out_pad(o, zero ? '0' : ' ', pad);
            for (int i = 0; i < n; i++) out_c(o, num[i]);
            if (left) out_pad(o, ' ', pad);
            continue;
        }
        /* integers */
        unsigned long long v;
        int neg = 0, base = 10, upper = 0;
        if (c == 'd' || c == 'i') {
            long long s;
            if (lng == 2) s = va_arg(ap, long long);
            else if (lng == 1) s = va_arg(ap, long);
            else if (lng == 3) s = (long long)va_arg(ap, ptrdiff_t);
            else s = va_arg(ap, int);
            neg = s < 0;
            v = neg ? (unsigned long long)(-(s + 1)) + 1 : (unsigned long long)s;
        } else {
            if (c == 'p') { v = (uintptr_t)va_arg(ap, void*); base = 16; alt = 1; }
            else if (lng == 2) v = va_arg(ap, unsigned long long);
            else if (lng == 1) v = va_arg(ap, unsigned long);
            else if (lng == 3) v = va_arg(ap, size_t);
            else v = va_arg(ap, unsigned int);
            if (c == 'x') base = 16;
            else if (c == 'X') { base = 16; upper = 1; }
            else if (c == 'o') base = 8;
            else if (c == 'b') base = 2;
            else if (c != 'u' && c != 'p') { out_c(o, '%'); out_c(o, c); continue; }
        }
        char digits[72];
        int nd = 0;
        do {
            int d = (int)(v % (unsigned)base);
            digits[nd++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
            v /= (unsigned)base;
        } while (v);
        if (prec == 0 && nd == 1 && digits[0] == '0') nd = 0;
        int zeros = prec > nd ? prec - nd : 0;
        char sign = neg ? '-' : plus ? '+' : space ? ' ' : 0;
        const char* prefix = (alt && base == 16) ? (upper ? "0X" : "0x") : (alt && base == 8) ? "0" : "";
        int total = nd + zeros + (sign ? 1 : 0) + (int)strlen(prefix);
        if (zero && !left && prec < 0) { zeros += width - total; total = width; }
        if (!left) out_pad(o, ' ', width - total);
        if (sign) out_c(o, sign);
        for (const char* p = prefix; *p; p++) out_c(o, *p);
        out_pad(o, '0', zeros);
        while (nd) out_c(o, digits[--nd]);
        if (left) out_pad(o, ' ', width - total);
    }
    if (o->f) out_flush(o);
    else if (o->cap) o->buf[o->n < o->cap ? o->n : o->cap - 1] = 0;
    return (int)o->n;
}

int vsnprintf(char* buf, size_t n, const char* fmt, va_list ap) { out_t o = { buf, n, 0, NULL, {0}, 0 }; return vfmt(&o, fmt, ap); }
int vsprintf(char* buf, const char* fmt, va_list ap) { return vsnprintf(buf, (size_t)-1 / 2, fmt, ap); }
int vfprintf(FILE* f, const char* fmt, va_list ap) {
    if (!f) return -1;
    out_t o = { NULL, 0, 0, f, {0}, 0 };
    LOCK(f);
    int err0 = f->err;
    int r = vfmt(&o, fmt, ap);
    if (f->err && !err0) r = -1;
    UNLOCK(f);
    return r;
}
int vprintf(const char* fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }
int snprintf(char* b, size_t n, const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vsnprintf(b, n, fmt, ap); va_end(ap); return r; }
int sprintf(char* b, const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vsprintf(b, fmt, ap); va_end(ap); return r; }
int fprintf(FILE* f, const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(f, fmt, ap); va_end(ap); return r; }
int printf(const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }

/* ── reading ──────────────────────────────────────────────────────── */

/* is descriptor 0 the console - whole lines, with echo and backspace, from
 * the system's line editor? Or a pipe, a file, a pseudo-terminal (read) */
static int stdin_console(void) {
    if (!BANANA_HAS(__banana, fd_info)) return 1;
    banana_fdinfo_t fi;
    return __banana->fd_info(0, &fi) == 0 && fi.type == BANANA_FD_TERM;
}

/* the console's line, handed out as reads of it */
static char g_line[1024];
static int  g_line_pos, g_line_len;

/* more to read into the buffer (the stream is locked): 0, or EOF at the end / an error */
static int refill(FILE* f) {
    if (!(f->flags & F_READ)) { f->err = 1; errno = EBADF; return EOF; }
    setup(f);
    if (f->last == 'w' && flush_out(f)) return EOF;
    f->last = 'r';
    /* (a prompt written to stdout is shown before the program waits for input) */
    if (f != stdout && stdout->wlen && stdout->mode != _IOFBF && ftrylockfile(stdout) == 0) {
        flush_out(stdout);
        funlockfile(stdout);
    }
    if (f == stdin && f->fd == 0 && stdin_console()) {
        if (g_line_pos >= g_line_len) {
            int n = __banana->readline(g_line, (int)sizeof(g_line) - 1);
            if (n < 0) { f->eof = 1; return EOF; }
            g_line[n] = '\n';
            g_line_len = n + 1;
            g_line_pos = 0;
        }
        size_t k = (size_t)(g_line_len - g_line_pos);
        if (k > f->size) k = f->size;
        memcpy(f->buf, g_line + g_line_pos, k);
        g_line_pos += (int)k;
        f->rpos = 0;
        f->rlen = k;
        return 0;
    }
    ssize_t r = read(f->fd, f->buf, f->size);
    if (r <= 0) {
        if (r < 0) f->err = 1; else f->eof = 1;
        f->rpos = f->rlen = 0;
        return EOF;
    }
    f->rpos = 0;
    f->rlen = (size_t)r;
    return 0;
}

static int getc_locked(FILE* f) {
    if (f->nback) return f->back[--f->nback];
    if (f->mem) {
        if (f->mpos >= f->mlen) { f->eof = 1; return EOF; }
        return (unsigned char)f->mbuf[f->mpos++];
    }
    if (f->last == 'r' && f->rpos < f->rlen) return f->buf[f->rpos++];
    if (refill(f)) return EOF;
    return f->buf[f->rpos++];
}

/* ── characters and lines ─────────────────────────────────────────── */

int fputc(int c, FILE* f) {
    char ch = (char)c;
    LOCK(f);
    int r = put_bytes(f, &ch, 1) == 1 ? (unsigned char)c : EOF;
    UNLOCK(f);
    return r;
}
int putc(int c, FILE* f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }
int fputs(const char* s, FILE* f) {
    LOCK(f);
    int r = put_bytes(f, s, strlen(s)) < 0 ? EOF : 0;
    UNLOCK(f);
    return r;
}
int puts(const char* s) {
    LOCK(stdout);
    int r = put_bytes(stdout, s, strlen(s)) < 0 || put_bytes(stdout, "\n", 1) < 0 ? EOF : 0;
    UNLOCK(stdout);
    return r;
}

int fgetc(FILE* f) {
    if (!f) return EOF;
    LOCK(f);
    int c = getc_locked(f);
    UNLOCK(f);
    return c;
}
int getc(FILE* f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE* f) {
    if (c == EOF || !f) return EOF;
    LOCK(f);
    int r = c & 255;
    /* (back into the buffer when it came from there: freadptr then sees it) */
    if (!f->mem && !f->nback && f->last == 'r' && f->rpos > 0) f->buf[--f->rpos] = (unsigned char)r;
    else if (f->nback < (int)sizeof(f->back)) f->back[f->nback++] = (unsigned char)r;
    else r = EOF;
    if (r != EOF) f->eof = 0;
    UNLOCK(f);
    return r;
}

char* fgets(char* buf, int n, FILE* f) {
    if (!f || n <= 0) return NULL;
    LOCK(f);
    int i = 0;
    while (i < n - 1) {
        /* (whole pieces of the buffer at a time) */
        if (!f->nback && !f->mem && f->last == 'r' && f->rpos < f->rlen) {
            size_t k = f->rlen - f->rpos;
            if (k > (size_t)(n - 1 - i)) k = (size_t)(n - 1 - i);
            unsigned char* nl = memchr(f->buf + f->rpos, '\n', k);
            if (nl) k = (size_t)(nl - (f->buf + f->rpos)) + 1;
            memcpy(buf + i, f->buf + f->rpos, k);
            f->rpos += k;
            i += (int)k;
            if (nl) break;
            continue;
        }
        int c = getc_locked(f);
        if (c == EOF) break;
        buf[i++] = (char)c;
        if (c == '\n') break;
    }
    UNLOCK(f);
    if (i == 0) return NULL;
    buf[i] = 0;
    return buf;
}

/* ── files ────────────────────────────────────────────────────────── */

/* a mode string: open()'s flags and the stream's; -1 if it is not one */
static int parse_mode(const char* mode, int* oflags, int* fflags) {
    int o, f;
    switch (mode[0]) {
    case 'r': o = O_RDONLY; f = F_READ; break;
    case 'w': o = O_WRONLY | O_CREAT | O_TRUNC; f = F_WRITE; break;
    case 'a': o = O_WRONLY | O_CREAT | O_APPEND; f = F_WRITE | F_APPEND; break;
    default: errno = EINVAL; return -1;
    }
    for (const char* m = mode + 1; *m; m++) {
        if (*m == '+') { o = (o & ~O_ACCMODE) | O_RDWR; f |= F_READ | F_WRITE; }
        else if (*m == 'x') o |= O_EXCL;
        else if (*m == 'e') o |= O_CLOEXEC;
    }
    *oflags = o;
    *fflags = f;
    return 0;
}

static FILE* new_file(int fd, int flags) {
    FILE* f = calloc(1, sizeof(FILE));
    if (!f) { errno = ENOMEM; return NULL; }
    f->fd = fd;
    f->flags = flags;
    spin(&g_files_lk);
    f->next = g_files;
    g_files = f;
    unspin(&g_files_lk);
    return f;
}

FILE* fopen(const char* path, const char* mode) {
    int o, fl;
    if (!path || !mode || parse_mode(mode, &o, &fl)) return NULL;
    int fd = open(path, o, 0666);
    if (fd < 0) return NULL;
    FILE* f = new_file(fd, fl);
    if (!f) close(fd);
    return f;
}

FILE* fdopen(int fd, const char* mode) {
    int o, fl;
    if (!mode || parse_mode(mode, &o, &fl)) return NULL;
    if (fd < 0) { errno = EBADF; return NULL; }
    return new_file(fd, fl);
}

FILE* freopen(const char* path, const char* mode, FILE* f) {
    int o, fl;
    if (!f || !mode || parse_mode(mode, &o, &fl)) return NULL;
    LOCK(f);
    flush_locked(f);
    if (!path) {                                    /* (the same file, another mode: as it is) */
        f->flags = (f->flags & ~(F_READ | F_WRITE | F_APPEND)) | fl;
        f->eof = f->err = 0;
        UNLOCK(f);
        return f;
    }
    int old = f->fd;
    if (old >= 0) close(old);
    int fd = open(path, o, 0666);
    if (fd >= 0 && old >= 0 && fd != old && (f->flags & F_STATIC)) {
        /* (stdin / stdout / stderr stay 0 / 1 / 2) */
        if (dup2(fd, old) == old) { close(fd); fd = old; }
    }
    f->fd = fd;
    f->flags = (f->flags & (F_STATIC | F_OWNBUF)) | fl;
    f->eof = f->err = 0;
    f->rpos = f->rlen = f->wlen = 0;
    f->nback = 0;
    f->last = 0;
    if (!(f->flags & F_OWNBUF) && f->buf != &f->one) { f->buf = NULL; f->size = 0; }
    f->mode_set = f == stderr;
    UNLOCK(f);
    if (fd < 0) { fclose(f); return NULL; }
    return f;
}

/* streams in memory */
FILE* open_memstream(char** buf, size_t* size) {
    if (!buf || !size) { errno = EINVAL; return NULL; }
    FILE* f = new_file(-1, F_READ | F_WRITE);
    char* b = malloc(64);
    if (!f || !b) { if (f) fclose(f); free(b); errno = ENOMEM; return NULL; }
    f->mem = 1;
    f->mbuf = b;
    f->mcap = 64;
    b[0] = 0;
    f->mout = buf;
    f->msize = size;
    mem_publish(f);
    return f;
}

FILE* fmemopen(void* buf, size_t size, const char* mode) {
    if (!size || !mode) { errno = EINVAL; return NULL; }
    FILE* f = new_file(-1, F_READ | F_WRITE);
    if (!f) return NULL;
    if (!buf) {
        buf = calloc(1, size);
        if (!buf) { fclose(f); errno = ENOMEM; return NULL; }
        f->mout = (char**)buf;                          /* (ours: freed at fclose) */
    }
    f->mem = 2;
    f->mbuf = buf;
    f->mcap = size;
    if (mode[0] == 'r') f->mlen = size;
    else if (mode[0] == 'a') f->mlen = f->mpos = strnlen(buf, size);
    else { f->mlen = 0; ((char*)buf)[0] = 0; }
    return f;
}

int fclose(FILE* f) {
    if (!f) return EOF;
    LOCK(f);
    int r = 0;
    if (f->mem) {
        if (f->mem == 1) mem_publish(f);
        else if (f->mout) free(f->mbuf);
        f->mem = 0;
    } else {
        if (f->last == 'w' && flush_out(f)) r = EOF;
        if (f->err && r == 0 && f->last == 'w') r = EOF;
        if (f->fd >= 0 && close(f->fd) < 0) r = EOF;
    }
    if (f->tmpname) { unlink(f->tmpname); free(f->tmpname); f->tmpname = NULL; }
    if (f->flags & F_OWNBUF) free(f->buf);
    if (f->flags & F_STATIC) {
        f->fd = -1;
        f->buf = NULL;
        f->size = 0;
        f->flags &= ~F_OWNBUF;
        f->rpos = f->rlen = f->wlen = 0;
        UNLOCK(f);
        return r;
    }
    spin(&g_files_lk);
    for (FILE** p = &g_files; *p; p = &(*p)->next) if (*p == f) { *p = f->next; break; }
    unspin(&g_files_lk);
    free(f);
    return r;
}

size_t fread(void* buf, size_t size, size_t n, FILE* f) {
    if (!f || !size || !n) return 0;
    LOCK(f);
    size_t want = size * n, got = 0;
    unsigned char* b = buf;
    while (got < want && f->nback) b[got++] = f->back[--f->nback];
    if (f->mem) {
        size_t k = f->mlen > f->mpos ? f->mlen - f->mpos : 0;
        if (k > want - got) k = want - got;
        memcpy(b + got, f->mbuf + f->mpos, k);
        f->mpos += k;
        got += k;
        if (got < want) f->eof = 1;
    } else {
        while (got < want) {
            if (f->last == 'r' && f->rpos < f->rlen) {
                size_t k = f->rlen - f->rpos;
                if (k > want - got) k = want - got;
                memcpy(b + got, f->buf + f->rpos, k);
                f->rpos += k;
                got += k;
                continue;
            }
            setup(f);
            if (want - got >= f->size && !(f == stdin && f->fd == 0 && stdin_console())) {
                /* (a big read: straight into the caller's memory) */
                if (!(f->flags & F_READ)) { f->err = 1; break; }
                if (f->last == 'w' && flush_out(f)) break;
                f->last = 'r';
                ssize_t r = read(f->fd, b + got, want - got);
                if (r <= 0) { if (r < 0) f->err = 1; else f->eof = 1; break; }
                got += (size_t)r;
                continue;
            }
            if (refill(f)) break;
        }
    }
    UNLOCK(f);
    return got / size;
}

size_t fwrite(const void* buf, size_t size, size_t n, FILE* f) {
    if (!size || !n || !f) return 0;
    LOCK(f);
    int r = put_bytes(f, buf, size * n);
    UNLOCK(f);
    return r < 0 ? 0 : n;
}

off_t ftello(FILE* f) {
    if (!f) return -1;
    LOCK(f);
    off_t r;
    if (f->mem) r = (off_t)f->mpos - f->nback;
    else {
        r = lseek(f->fd, 0, SEEK_CUR);
        if (r >= 0 && f->last == 'r') r -= (off_t)(f->rlen - f->rpos) + f->nback;
        if (r >= 0 && f->last == 'w') {
            if (f->flags & F_APPEND) { if (flush_out(f) == 0) r = lseek(f->fd, 0, SEEK_CUR); }
            else r += (off_t)f->wlen;
        }
    }
    UNLOCK(f);
    return r;
}

int fseeko(FILE* f, off_t off, int whence) {
    if (!f) return -1;
    LOCK(f);
    int r = 0;
    if (f->mem) {
        long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long)f->mpos - f->nback : (long)f->mlen;
        if (base + off < 0 || (f->mem == 2 && (size_t)(base + off) > f->mcap)) { errno = EINVAL; r = -1; }
        else f->mpos = (size_t)(base + off);
    } else {
        if (f->last == 'w' && flush_out(f)) r = -1;
        if (!r && whence == SEEK_CUR && f->last == 'r') off -= (off_t)(f->rlen - f->rpos) + f->nback;
        if (!r && lseek(f->fd, off, whence) < 0) r = -1;
        if (!r) { f->rpos = f->rlen = 0; f->last = 0; }
    }
    if (!r) { f->nback = 0; f->eof = 0; }
    UNLOCK(f);
    return r;
}

int fseek(FILE* f, long off, int whence) { return fseeko(f, (off_t)off, whence); }
long ftell(FILE* f) { return (long)ftello(f); }
int fgetpos(FILE* f, fpos_t* pos) { off_t p = ftello(f); if (p < 0) return -1; *pos = p; return 0; }
int fsetpos(FILE* f, const fpos_t* pos) { return fseeko(f, (off_t)*pos, SEEK_SET); }
void rewind(FILE* f) { fseeko(f, 0, SEEK_SET); if (f) f->err = 0; }
int feof(FILE* f) { return f ? f->eof : 1; }
int ferror(FILE* f) { return f ? f->err : 1; }
void clearerr(FILE* f) { if (f) f->eof = f->err = 0; }
void perror(const char* s) {
    if (s && *s) fprintf(stderr, "%s: %s\n", s, strerror(errno));
    else fprintf(stderr, "%s\n", strerror(errno));
}

/* ── scanf (integers, floats, strings, characters, %[...], %n, %p) ─── */

typedef struct { const char* s; FILE* f; int peek; long pos; } in_t;

static int in_get(in_t* in) {
    int c;
    if (in->peek != -2) { c = in->peek; in->peek = -2; }
    else if (in->s) c = *in->s ? (unsigned char)*in->s++ : EOF;
    else c = fgetc(in->f);
    if (c != EOF) in->pos++;
    return c;
}
static void in_unget(in_t* in, int c) { in->peek = c; if (c != EOF) in->pos--; }

/* an integer to its argument, by the size letters (hh h l ll j z t) */
static void scan_store(va_list* ap, int size, unsigned long long v) {
    switch (size) {
    case -2: *va_arg(*ap, signed char*) = (signed char)v; break;
    case -1: *va_arg(*ap, short*) = (short)v; break;
    case 1:  *va_arg(*ap, long*) = (long)v; break;
    case 2:  *va_arg(*ap, long long*) = (long long)v; break;
    default: *va_arg(*ap, int*) = (int)v; break;
    }
}

/* %[...]: is c in the set (fmt points after '['; *end gets the closing ']') */
static int in_set(const char* set, int c, const char** end) {
    int neg = 0, hit = 0;
    const char* p = set;
    if (*p == '^') { neg = 1; p++; }
    if (*p == ']') { hit |= c == ']'; p++; }
    for (; *p && *p != ']'; p++) {
        if (p[1] == '-' && p[2] && p[2] != ']') {
            hit |= c >= (unsigned char)p[0] && c <= (unsigned char)p[2];
            p += 2;
        } else hit |= c == (unsigned char)*p;
    }
    if (end) *end = p;
    return neg ? !hit : hit;
}

static int vscan(in_t* in, const char* fmt, va_list ap0) {
    va_list ap;
    va_copy(ap, ap0);
    int count = 0, any = 0;              /* any: a conversion was tried on input */
    for (; *fmt; fmt++) {
        if (isspace((unsigned char)*fmt)) {
            int c;
            while ((c = in_get(in)) != EOF && isspace(c)) ;
            in_unget(in, c);
            continue;
        }
        if (*fmt != '%') {
            int c = in_get(in);
            if (c != (unsigned char)*fmt) { in_unget(in, c); if (c == EOF && !any) count = EOF; break; }
            continue;
        }
        fmt++;
        int skip = 0, width = 0, size = 0;
        if (*fmt == '*') { skip = 1; fmt++; }
        while (isdigit((unsigned char)*fmt)) width = width * 10 + (*fmt++ - '0');
        for (;; fmt++) {
            if (*fmt == 'h') size--;
            else if (*fmt == 'l') size++;
            else if (*fmt == 'L' || *fmt == 'q') size = 2;
            else if (*fmt == 'j' || *fmt == 'z' || *fmt == 't') size = sizeof(long) == 8 ? 1 : 0;
            else break;
        }
        char conv = *fmt;
        int c;
        if (conv == '%') {
            while ((c = in_get(in)) != EOF && isspace(c)) ;
            if (c != '%') { in_unget(in, c); break; }
            continue;
        }
        if (conv == 'n') { if (!skip) scan_store(&ap, size, (unsigned long long)in->pos); continue; }
        if (conv != 'c' && conv != '[') { while ((c = in_get(in)) != EOF && isspace(c)) ; in_unget(in, c); }
        any = 1;
        if (conv == 'c') {
            int n = width ? width : 1;
            char* out = skip ? NULL : va_arg(ap, char*);
            for (int i = 0; i < n; i++) {
                c = in_get(in);
                if (c == EOF) { if (!i) { va_end(ap); return count ? count : EOF; } break; }
                if (out) out[i] = (char)c;
            }
            if (!skip) count++;
        } else if (conv == 's' || conv == '[') {
            const char* set = fmt + 1;
            const char* end = NULL;
            if (conv == '[') in_set(set, 0, &end);
            char* out = skip ? NULL : va_arg(ap, char*);
            int n = 0;
            while ((c = in_get(in)) != EOF && (!width || n < width) &&
                   (conv == 's' ? !isspace(c) : in_set(set, c, NULL))) {
                if (out) out[n] = (char)c;
                n++;
            }
            in_unget(in, c);
            if (conv == '[') fmt = *end ? end : end - 1;
            if (!n) { va_end(ap); return count ? count : (c == EOF ? EOF : 0); }
            if (out) { out[n] = 0; count++; }
        } else if (conv == 'd' || conv == 'i' || conv == 'u' || conv == 'x' || conv == 'X' || conv == 'o' || conv == 'p') {
            char num[72];
            int n = 0;
            int base = conv == 'x' || conv == 'X' || conv == 'p' ? 16 : conv == 'o' ? 8 : conv == 'i' ? 0 : 10;
            int lim = width && width < 70 ? width : 70;
            c = in_get(in);
            if (c == '-' || c == '+') { num[n++] = (char)c; c = in_get(in); }
            if (c == '0' && n < lim && (base == 16 || base == 0)) {         /* a 0x prefix (or octal) */
                num[n++] = (char)c;
                c = in_get(in);
                if ((c == 'x' || c == 'X') && n < lim) { num[n++] = (char)c; c = in_get(in); if (!base) base = 16; }
                else if (!base) base = 8;
            }
            if (!base) base = 10;
            while (c != EOF && n < lim &&
                   (base == 16 ? isxdigit(c) : base == 8 ? (c >= '0' && c <= '7') : isdigit(c))) {
                num[n++] = (char)c;
                c = in_get(in);
            }
            in_unget(in, c);
            num[n] = 0;
            char* e;
            unsigned long long v = conv == 'd' || conv == 'i' ? (unsigned long long)strtoll(num, &e, base) : strtoull(num, &e, base);
            if (e == num) { va_end(ap); return count ? count : (c == EOF ? EOF : 0); }
            if (!skip) {
                if (conv == 'p') *va_arg(ap, void**) = (void*)(uintptr_t)v;
                else scan_store(&ap, size, v);
                count++;
            }
        } else if (conv == 'f' || conv == 'e' || conv == 'g' || conv == 'E' || conv == 'G' || conv == 'a' || conv == 'F') {
            char num[72];
            int n = 0;
            int lim = width && width < 70 ? width : 70;
            c = in_get(in);
            while (c != EOF && n < lim &&
                   (isdigit(c) || c == '.' || ((c == '-' || c == '+') && (!n || num[n - 1] == 'e' || num[n - 1] == 'E')) ||
                    ((c == 'e' || c == 'E') && n))) {
                num[n++] = (char)c;
                c = in_get(in);
            }
            in_unget(in, c);
            num[n] = 0;
            char* e;
            double v = strtod(num, &e);
            if (e == num) { va_end(ap); return count ? count : (c == EOF ? EOF : 0); }
            if (!skip) {
                if (size >= 2) *va_arg(ap, long double*) = v;
                else if (size == 1) *va_arg(ap, double*) = v;
                else *va_arg(ap, float*) = (float)v;
                count++;
            }
        } else {
            break;
        }
    }
    va_end(ap);
    return count;
}

int sscanf(const char* s, const char* fmt, ...) {
    in_t in = { s, NULL, -2, 0 };
    va_list ap;
    va_start(ap, fmt);
    int r = vscan(&in, fmt, ap);
    va_end(ap);
    return r;
}

int vsscanf(const char* s, const char* fmt, va_list ap) {
    in_t in = { s, NULL, -2, 0 };
    return vscan(&in, fmt, ap);
}

int vfscanf(FILE* f, const char* fmt, va_list ap) {
    in_t in = { NULL, f, -2, 0 };
    int r = vscan(&in, fmt, ap);
    if (in.peek >= 0) ungetc(in.peek, f);
    return r;
}

int fscanf(FILE* f, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int r = vfscanf(f, fmt, ap);
    va_end(ap);
    return r;
}

int scanf(const char* fmt, ...) {
    in_t in = { NULL, stdin, -2, 0 };
    va_list ap;
    va_start(ap, fmt);
    int r = vscan(&in, fmt, ap);
    va_end(ap);
    if (in.peek >= 0) ungetc(in.peek, stdin);
    return r;
}

int fileno(FILE* f) { if (!f || f->mem) { errno = EBADF; return -1; } return f->fd; }

int setvbuf(FILE* f, char* buf, int mode, size_t size) {
    if (!f || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) { errno = EINVAL; return -1; }
    LOCK(f);
    flush_locked(f);
    f->mode = mode;
    f->mode_set = 1;
    if (mode == _IONBF || buf) {
        if (f->flags & F_OWNBUF) free(f->buf);
        f->flags &= ~F_OWNBUF;
        if (mode == _IONBF) { f->buf = &f->one; f->size = 1; }
        else { f->buf = (unsigned char*)buf; f->size = size ? size : 1; }
    }
    UNLOCK(f);
    return 0;
}
void setbuf(FILE* f, char* buf) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ); }
void setbuffer(FILE* f, char* buf, size_t size) { setvbuf(f, buf, buf ? _IOFBF : _IONBF, size); }
void setlinebuf(FILE* f) { setvbuf(f, NULL, _IOLBF, 0); }

/* a file that goes away when closed */
FILE* tmpfile(void) {
    char t[] = "/tmp/tmpfileXXXXXX";
    int fd = mkstemp(t);
    if (fd < 0) return NULL;
    FILE* f = new_file(fd, F_READ | F_WRITE);
    if (!f) { close(fd); unlink(t); return NULL; }
    f->tmpname = strdup(t);
    return f;
}

int vdprintf(int fd, const char* fmt, va_list ap) {
    char small[512];
    va_list aq;
    va_copy(aq, ap);
    int n = vsnprintf(small, sizeof(small), fmt, aq);
    va_end(aq);
    if (n < 0) return n;
    char* b = small;
    if ((size_t)n >= sizeof(small)) {
        b = malloc((size_t)n + 1);
        if (!b) { errno = ENOMEM; return -1; }
        vsnprintf(b, (size_t)n + 1, fmt, ap);
    }
    int done = 0;
    while (done < n) {
        ssize_t w = write(fd, b + done, (size_t)(n - done));
        if (w <= 0) { if (w < 0 && errno == EINTR) continue; done = -1; break; }
        done += (int)w;
    }
    if (b != small) free(b);
    return done < 0 ? -1 : n;
}
int dprintf(int fd, const char* fmt, ...) { va_list ap; va_start(ap, fmt); int r = vdprintf(fd, fmt, ap); va_end(ap); return r; }

/* the _unlocked calls: the caller holds the lock (flockfile), or does not share the stream */
int getc_unlocked(FILE* f) { return getc_locked(f); }
int getchar_unlocked(void) { return getc_locked(stdin); }
int fgetc_unlocked(FILE* f) { return getc_locked(f); }
int putc_unlocked(int c, FILE* f) { char ch = (char)c; return put_bytes(f, &ch, 1) == 1 ? (unsigned char)c : EOF; }
int fputc_unlocked(int c, FILE* f) { return putc_unlocked(c, f); }
int putchar_unlocked(int c) { return putc_unlocked(c, stdout); }
int fputs_unlocked(const char* s, FILE* f) { return put_bytes(f, s, strlen(s)) < 0 ? EOF : 0; }
size_t fwrite_unlocked(const void* b, size_t s, size_t n, FILE* f) { return fwrite(b, s, n, f); }
size_t fread_unlocked(void* b, size_t s, size_t n, FILE* f) { return fread(b, s, n, f); }
char* fgets_unlocked(char* b, int n, FILE* f) { return fgets(b, n, f); }
int fflush_unlocked(FILE* f) { return f ? flush_locked(f) : fflush(NULL); }
int feof_unlocked(FILE* f) { return feof(f); }
int ferror_unlocked(FILE* f) { return ferror(f); }
void clearerr_unlocked(FILE* f) { clearerr(f); }
int fileno_unlocked(FILE* f) { return fileno(f); }

/* <stdio_ext.h>: glibc's and musl's looks into a stream (gnulib uses them) */
size_t __fpending(FILE* f) { return f->last == 'w' ? f->wlen : 0; }
int __freading(FILE* f) { return (f->flags & (F_READ | F_WRITE)) == F_READ || f->last == 'r'; }
int __fwriting(FILE* f) { return (f->flags & (F_READ | F_WRITE)) == F_WRITE || f->last == 'w'; }
int __freadable(FILE* f) { return (f->flags & F_READ) != 0; }
int __fwritable(FILE* f) { return (f->flags & F_WRITE) != 0; }
int __flbf(FILE* f) { setup(f); return f->mode == _IOLBF; }
size_t __fbufsize(FILE* f) { setup(f); return f->size; }
void __fpurge(FILE* f) { f->rpos = f->rlen = f->wlen = 0; f->nback = 0; if (f->mem) f->mpos = f->mlen; }
int fpurge(FILE* f) { __fpurge(f); return 0; }
int __fsetlocking(FILE* f, int type) { (void)f; (void)type; return 1; }   /* FSETLOCKING_INTERNAL */
void _flushlbf(void) { fflush(NULL); }
size_t __freadahead(FILE* f) { return (f->last == 'r' ? f->rlen - f->rpos : 0) + (size_t)f->nback; }
const char* __freadptr(FILE* f, size_t* size) {
    if (f->nback || f->last != 'r' || f->rpos >= f->rlen) return NULL;
    *size = f->rlen - f->rpos;
    return (const char*)f->buf + f->rpos;
}
void __freadptrinc(FILE* f, size_t inc) { f->rpos += inc; }
void __fseterr(FILE* f) { f->err = 1; }
