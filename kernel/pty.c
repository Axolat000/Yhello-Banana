/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pty.h"
#include "kfile.h"
#include "kheap.h"
#include "kstring.h"
#include "spinlock.h"

/* termios flags (Linux's values) */
#define IGNCR   0000200
#define ICRNL   0000400
#define INLCR   0000100
#define ISTRIP  0000040
#define IXON    0002000
#define IXANY   0004000
#define IUTF8   0040000
#define OPOST   0000001
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define XTABS   0014000
#define ISIG    0000001
#define ICANON  0000002
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define ECHOCTL 0001000
#define ECHOKE  0004000
#define IEXTEN  0100000
#define CS8     0000060
#define CREAD   0000200
#define HUPCL   0002000
#define B38400  0000017
enum { VINTR, VQUIT, VERASE, VKILL, VEOF, VTIME, VMIN, VSWTC, VSTART, VSTOP, VSUSP, VEOL, VREPRINT,
       VDISCARD, VWERASE, VLNEXT, VEOL2 };

#define INQ  8192                  /* typed bytes the programs may read */
#define LINE 4096                  /* the line being edited (canonical mode) */
#define OUTQ 65536                 /* the programs' output, for the master */
#define RECS 512                   /* complete lines waiting (canonical mode) */

struct pty {
    int           used, index;
    int           masters, slaves, slave_seen;
    int           holds;           /* processes it is the controlling terminal of (they keep it) */
    int           locked, hung;
    int           sid, fg;
    pty_termios_t tio;
    pty_winsize_t ws;
    uint8_t       in[INQ];         /* ready input: a ring */
    uint32_t      in_head, in_len;
    uint32_t      rec[RECS];       /* canonical: the lengths of the lines in it (0: an end of file) */
    int           rec_head, nrec;
    uint8_t       line[LINE];      /* the line being edited */
    uint32_t      llen;
    int           lnext;           /* ^V: the next character as it is */
    uint8_t*      out;             /* output: a ring of OUTQ bytes */
    uint32_t      out_head, out_len;
    uint32_t      column;          /* (of the output: tabs, ONOCR) */
    int           stopped;         /* ^S */
    int           events;
};

static spinlock_t g_lock;
static pty_t**    g_ptys;
static int        g_nptys, g_cap;

static void defaults(pty_t* t) {
    memset(&t->tio, 0, sizeof(t->tio));
    t->tio.c_iflag = ICRNL | IXON | IUTF8;
    t->tio.c_oflag = OPOST | ONLCR;
    t->tio.c_cflag = B38400 | CS8 | CREAD | HUPCL;
    t->tio.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    uint8_t* cc = t->tio.c_cc;
    cc[VINTR] = 3; cc[VQUIT] = 034; cc[VERASE] = 0177; cc[VKILL] = 025; cc[VEOF] = 4;
    cc[VTIME] = 0; cc[VMIN] = 1; cc[VSTART] = 021; cc[VSTOP] = 023; cc[VSUSP] = 032;
    cc[VREPRINT] = 022; cc[VDISCARD] = 017; cc[VWERASE] = 027; cc[VLNEXT] = 026;
    t->tio.c_ispeed = t->tio.c_ospeed = B38400;
    t->ws.ws_row = 24;
    t->ws.ws_col = 80;
}

pty_t* pty_new(void) {
    pty_t* t = (pty_t*)kzalloc(sizeof(pty_t));
    uint8_t* out = (uint8_t*)kmalloc(OUTQ);
    if (!t || !out) { kfree(t); kfree(out); return NULL; }
    uintptr_t fl = spin_lock_irq(&g_lock);
    int slot = -1;
    for (int i = 0; i < g_nptys; i++) if (!g_ptys[i]) { slot = i; break; }
    if (slot < 0) {
        if (g_nptys == g_cap) {
            int cap = g_cap ? g_cap * 2 : 16;
            spin_unlock_irq(&g_lock, fl);
            pty_t** n = (pty_t**)kzalloc((size_t)cap * sizeof(pty_t*));
            fl = spin_lock_irq(&g_lock);
            if (!n) { spin_unlock_irq(&g_lock, fl); kfree(t); kfree(out); return NULL; }
            if (g_nptys == g_cap) {
                if (g_nptys) memcpy(n, g_ptys, (size_t)g_nptys * sizeof(pty_t*));
                pty_t** old = g_ptys;
                g_ptys = n;
                g_cap = cap;
                n = old;
            }
            spin_unlock_irq(&g_lock, fl);
            kfree(n);
            fl = spin_lock_irq(&g_lock);
        }
        slot = g_nptys++;
    }
    t->used = 1;
    t->index = slot;
    t->masters = 1;
    t->locked = 1;                                  /* (unlockpt opens it) */
    t->out = out;
    defaults(t);
    g_ptys[slot] = t;
    spin_unlock_irq(&g_lock, fl);
    return t;
}

int pty_index(pty_t* t) { return t ? t->index : -1; }

pty_t* pty_find(int index) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    pty_t* t = index >= 0 && index < g_nptys ? g_ptys[index] : NULL;
    if (t && (!t->masters || t->hung)) t = NULL;
    spin_unlock_irq(&g_lock, fl);
    return t;
}

int pty_count(void) {
    int n = 0;
    uintptr_t fl = spin_lock_irq(&g_lock);
    for (int i = 0; i < g_nptys; i++) if (g_ptys[i]) n++;
    spin_unlock_irq(&g_lock, fl);
    return n;
}

/* both ends closed: it goes (lock held; freed by the caller) */
static pty_t* maybe_free(pty_t* t) {
    if (t->masters > 0 || t->slaves > 0 || t->holds > 0) return NULL;
    if (t->index >= 0 && t->index < g_nptys && g_ptys[t->index] == t) g_ptys[t->index] = NULL;
    return t;
}

int pty_master_ref(pty_t* t, int delta) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    t->masters += delta;
    if (t->masters <= 0 && !t->hung) {
        t->hung = 1;
        t->events |= PTY_EV_HANGUP;
    }
    pty_t* f = maybe_free(t);
    spin_unlock_irq(&g_lock, fl);
    if (f) { kfree(f->out); kfree(f); }
    kfile_changed();
    return f != NULL;
}

void pty_slave_ref(pty_t* t, int delta) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    t->slaves += delta;
    if (delta > 0) t->slave_seen = 1;
    pty_t* f = maybe_free(t);
    spin_unlock_irq(&g_lock, fl);
    if (f) { kfree(f->out); kfree(f); }
    kfile_changed();
}

void pty_hold(pty_t* t, int delta) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    t->holds += delta;
    pty_t* f = maybe_free(t);
    spin_unlock_irq(&g_lock, fl);
    if (f) { kfree(f->out); kfree(f); }
}

int  pty_locked(pty_t* t) { return t->locked; }
void pty_set_locked(pty_t* t, int locked) { t->locked = locked; }
int  pty_hung_up(pty_t* t) { return t->hung; }
int  pty_session(pty_t* t) { return t->sid; }
void pty_set_session(pty_t* t, int sid) { t->sid = sid; }
int  pty_fg(pty_t* t) { return t->fg; }
void pty_set_fg(pty_t* t, int pgrp) { t->fg = pgrp; }

int pty_events(pty_t* t) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    int e = t->events;
    t->events = 0;
    spin_unlock_irq(&g_lock, fl);
    return e;
}

/* ── output: the programs' bytes (and the echo) for the master ───────── */

static void out_put(pty_t* t, uint8_t c) {
    if (t->out_len >= OUTQ) return;                         /* (full: an echo is lost, as on a real line) */
    t->out[(t->out_head + t->out_len) % OUTQ] = c;
    t->out_len++;
}

/* one byte through OPOST; how many bytes it took in the queue (0: no room) */
static int out_char(pty_t* t, uint8_t c) {
    uint32_t of = t->tio.c_oflag;
    if (OUTQ - t->out_len < 8) return 0;
    if (!(of & OPOST)) { out_put(t, c); return 1; }
    switch (c) {
    case '\n':
        if (of & ONLCR) { out_put(t, '\r'); out_put(t, '\n'); t->column = 0; return 2; }
        if (of & ONLRET) t->column = 0;
        out_put(t, '\n');
        return 1;
    case '\r':
        if ((of & ONOCR) && t->column == 0) return 1;
        if (of & OCRNL) { out_put(t, '\n'); if (of & ONLRET) t->column = 0; return 1; }
        out_put(t, '\r');
        t->column = 0;
        return 1;
    case '\t':
        if ((of & XTABS) == XTABS) {
            int n = 8 - (int)(t->column % 8);
            for (int i = 0; i < n; i++) out_put(t, ' ');
            t->column += (uint32_t)n;
            return n;
        }
        t->column = (t->column | 7) + 1;
        out_put(t, c);
        return 1;
    case '\b':
        if (t->column) t->column--;
        out_put(t, c);
        return 1;
    }
    if (c >= 0x20 && (c & 0xC0) != 0x80 && c != 0x7F) t->column++;   /* (a UTF-8 character: one column) */
    out_put(t, c);
    return 1;
}

static void echo_char(pty_t* t, uint8_t c) {
    uint32_t lf = t->tio.c_lflag;
    if ((lf & ECHOCTL) && ((c < 0x20 && c != '\t' && c != '\n') || c == 0x7F)) {
        out_char(t, '^');
        out_char(t, c == 0x7F ? '?' : (uint8_t)(c + 0x40));
    } else {
        out_char(t, c);
    }
}

/* the columns the last character of the line took on screen */
static int last_width(pty_t* t, uint32_t at) {
    uint8_t c = t->line[at];
    if (c == '\t') return 1;
    if ((c < 0x20 || c == 0x7F) && (t->tio.c_lflag & ECHOCTL)) return 2;
    return 1;
}

/* the last character of the line goes (a whole UTF-8 sequence); echoes it away */
static int erase_one(pty_t* t) {
    if (!t->llen) return 0;
    uint32_t at = t->llen - 1;
    if (t->tio.c_iflag & IUTF8)
        while (at > 0 && (t->line[at] & 0xC0) == 0x80) at--;
    int w = last_width(t, at);
    t->llen = at;
    if ((t->tio.c_lflag & ECHO) && (t->tio.c_lflag & ECHOE))
        for (int i = 0; i < w; i++) { out_char(t, '\b'); out_char(t, ' '); out_char(t, '\b'); }
    return 1;
}

static int is_space(uint8_t c) { return c == ' ' || c == '\t'; }

/* ── input: what the master types ─────────────────────────────────── */

static uint32_t in_room(pty_t* t) { return INQ - t->in_len; }

static void in_put(pty_t* t, const uint8_t* s, uint32_t n) {
    for (uint32_t i = 0; i < n && t->in_len < INQ; i++) {
        t->in[(t->in_head + t->in_len) % INQ] = s[i];
        t->in_len++;
    }
}

/* the edited line becomes readable (with its end character, if any) */
static void line_done(pty_t* t) {
    if (t->nrec == RECS || in_room(t) < t->llen) { t->llen = 0; return; }   /* (no room: it is lost) */
    in_put(t, t->line, t->llen);
    t->rec[(t->rec_head + t->nrec) % RECS] = t->llen;
    t->nrec++;
    t->llen = 0;
}

static void flush_input(pty_t* t) {
    t->in_len = 0;
    t->in_head = 0;
    t->nrec = 0;
    t->rec_head = 0;
    t->llen = 0;
}

/* one typed byte; 0 if it could not be taken (raw mode, the queue full) */
static int type_char(pty_t* t, uint8_t c) {
    uint32_t lf = t->tio.c_lflag, iflag = t->tio.c_iflag;
    const uint8_t* cc = t->tio.c_cc;
    int canon = (lf & ICANON) != 0;
    if (t->lnext) {
        t->lnext = 0;
        if (canon) { if (t->llen < LINE - 1) t->line[t->llen++] = c; }
        else if (!in_room(t)) return 0;
        else in_put(t, &c, 1);
        if (lf & ECHO) echo_char(t, c);
        return 1;
    }
    if (iflag & ISTRIP) c &= 0x7F;
    if (c == '\r') {
        if (iflag & IGNCR) return 1;
        if (iflag & ICRNL) c = '\n';
    } else if (c == '\n' && (iflag & INLCR)) {
        c = '\r';
    }
    if (iflag & IXON) {
        if (c == cc[VSTOP] && cc[VSTOP]) { t->stopped = 1; return 1; }
        if (c == cc[VSTART] && cc[VSTART]) { t->stopped = 0; return 1; }
        if (iflag & IXANY) t->stopped = 0;
    }
    if (lf & ISIG) {
        int ev = c == cc[VINTR] && cc[VINTR] ? PTY_EV_SIGINT : c == cc[VQUIT] && cc[VQUIT] ? PTY_EV_SIGQUIT
               : c == cc[VSUSP] && cc[VSUSP] ? PTY_EV_SIGTSTP : 0;
        if (ev) {
            t->events |= ev;
            if (!(lf & NOFLSH)) { flush_input(t); t->out_len = 0; }
            if (lf & ECHO) echo_char(t, c);
            return 1;
        }
    }
    if (canon) {
        if ((lf & IEXTEN) && c == cc[VLNEXT] && cc[VLNEXT]) {
            t->lnext = 1;
            if ((lf & ECHO) && (lf & ECHOCTL)) { out_char(t, '^'); out_char(t, '\b'); }
            return 1;
        }
        if (c == cc[VERASE] && cc[VERASE]) { erase_one(t); return 1; }
        if ((lf & IEXTEN) && c == cc[VWERASE] && cc[VWERASE]) {
            while (t->llen && is_space(t->line[t->llen - 1])) erase_one(t);
            while (t->llen && !is_space(t->line[t->llen - 1])) erase_one(t);
            return 1;
        }
        if (c == cc[VKILL] && cc[VKILL]) {
            if ((lf & ECHO) && (lf & ECHOKE)) { while (t->llen) erase_one(t); }
            else {
                t->llen = 0;
                if ((lf & ECHO) && (lf & ECHOK)) out_char(t, '\n');
            }
            return 1;
        }
        if ((lf & IEXTEN) && c == cc[VREPRINT] && cc[VREPRINT]) {
            if (lf & ECHO) {
                echo_char(t, c);
                out_char(t, '\n');
                for (uint32_t i = 0; i < t->llen; i++) echo_char(t, t->line[i]);
            }
            return 1;
        }
        if (c == cc[VEOF] && cc[VEOF]) { line_done(t); return 1; }          /* (empty: an end of file) */
        int eol = c == '\n' || (c == cc[VEOL] && cc[VEOL]) || (c == cc[VEOL2] && cc[VEOL2]);
        if (t->llen < LINE - 1 || (eol && t->llen < LINE)) t->line[t->llen++] = c;
        if ((lf & ECHO) || (c == '\n' && (lf & ECHONL))) echo_char(t, c);
        if (eol) line_done(t);
        return 1;
    }
    if (!in_room(t)) return 0;
    in_put(t, &c, 1);
    if (lf & ECHO) echo_char(t, c);
    return 1;
}

long pty_master_write(pty_t* t, const uint8_t* buf, unsigned long n) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    unsigned long i = 0;
    for (; i < n; i++) if (!type_char(t, buf[i])) break;
    spin_unlock_irq(&g_lock, fl);
    if (i) kfile_changed();
    return i ? (long)i : (n ? -11 : 0);
}

long pty_master_read(pty_t* t, uint8_t* buf, unsigned long n) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    if (!t->out_len) {
        int gone = t->slave_seen && !t->slaves;
        spin_unlock_irq(&g_lock, fl);
        return gone ? -5 : -11;                          /* (every program closed it: EIO, as Linux) */
    }
    uint32_t take = t->out_len < n ? t->out_len : (uint32_t)n;
    for (uint32_t i = 0; i < take; i++) buf[i] = t->out[(t->out_head + i) % OUTQ];
    t->out_head = (t->out_head + take) % OUTQ;
    t->out_len -= take;
    spin_unlock_irq(&g_lock, fl);
    kfile_changed();
    return (long)take;
}

long pty_slave_read(pty_t* t, uint8_t* buf, unsigned long n) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    long r;
    if (t->tio.c_lflag & ICANON) {
        if (!t->nrec) r = t->hung ? 0 : -11;
        else {
            uint32_t* len = &t->rec[t->rec_head];
            if (!*len) {                                     /* ^D on an empty line: the end */
                t->rec_head = (t->rec_head + 1) % RECS;
                t->nrec--;
                r = 0;
            } else {
                uint32_t take = *len < n ? *len : (uint32_t)n;
                for (uint32_t i = 0; i < take; i++) buf[i] = t->in[(t->in_head + i) % INQ];
                t->in_head = (t->in_head + take) % INQ;
                t->in_len -= take;
                *len -= take;
                if (!*len) { t->rec_head = (t->rec_head + 1) % RECS; t->nrec--; }
                r = (long)take;
            }
        }
    } else {
        if (!t->in_len) r = t->hung ? 0 : -11;
        else {
            uint32_t take = t->in_len < n ? t->in_len : (uint32_t)n;
            for (uint32_t i = 0; i < take; i++) buf[i] = t->in[(t->in_head + i) % INQ];
            t->in_head = (t->in_head + take) % INQ;
            t->in_len -= take;
            r = (long)take;
        }
    }
    spin_unlock_irq(&g_lock, fl);
    if (r > 0) kfile_changed();
    return r;
}

long pty_slave_write(pty_t* t, const uint8_t* buf, unsigned long n) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    if (t->hung) { spin_unlock_irq(&g_lock, fl); return -5; }
    if (t->stopped) { spin_unlock_irq(&g_lock, fl); return -11; }
    unsigned long i = 0;
    for (; i < n; i++) if (!out_char(t, buf[i])) break;
    spin_unlock_irq(&g_lock, fl);
    if (i) kfile_changed();
    return i ? (long)i : (n ? -11 : 0);
}

int pty_poll(pty_t* t, int master, int events) {
    int r = 0;
    uintptr_t fl = spin_lock_irq(&g_lock);
    if (master) {
        if (t->out_len) r |= KF_POLLIN;
        else if (t->slave_seen && !t->slaves) r |= KF_POLLIN | KF_POLLHUP;
        if (in_room(t)) r |= KF_POLLOUT;
    } else {
        int ready = (t->tio.c_lflag & ICANON) ? t->nrec > 0 : t->in_len > 0;
        if (ready) r |= KF_POLLIN;
        if (t->hung) r |= KF_POLLIN | KF_POLLHUP;
        else if (!t->stopped && OUTQ - t->out_len >= 8) r |= KF_POLLOUT;
    }
    spin_unlock_irq(&g_lock, fl);
    return r & (events | KF_POLLERR | KF_POLLHUP);
}

int pty_nread(pty_t* t, int master) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    int n = master ? (int)t->out_len : (t->tio.c_lflag & ICANON) ? (t->nrec ? (int)t->rec[t->rec_head] : 0) : (int)t->in_len;
    spin_unlock_irq(&g_lock, fl);
    return n;
}

void pty_flush(pty_t* t, int queue) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    if (queue == 0 || queue == 2) flush_input(t);
    if (queue == 1 || queue == 2) { t->out_len = 0; t->out_head = 0; }
    spin_unlock_irq(&g_lock, fl);
    kfile_changed();
}

void pty_get_termios(pty_t* t, pty_termios_t* out) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    *out = t->tio;
    spin_unlock_irq(&g_lock, fl);
}

void pty_set_termios(pty_t* t, const pty_termios_t* in, int flush) {
    uintptr_t fl = spin_lock_irq(&g_lock);
    int was = (t->tio.c_lflag & ICANON) != 0, now = (in->c_lflag & ICANON) != 0;
    t->tio = *in;
    if (flush) flush_input(t);
    if (was && !now) {
        /* the lines typed before: readable as they are - an end of file
         * among them (^D typed before the program turned line editing off:
         * readline, as bash comes back to its prompt) becomes the ^D
         * character itself, as Linux has it, not lost; then the line being
         * typed */
        uint8_t eof = t->tio.c_cc[VEOF] ? t->tio.c_cc[VEOF] : 4;
        int eofs = 0;
        for (int k = 0; k < t->nrec; k++) if (!t->rec[(t->rec_head + k) % RECS]) eofs++;
        if (eofs && t->in_len + (uint32_t)eofs <= INQ) {
            static uint8_t tmp[INQ];                     /* (under g_lock: one user at a time) */
            {
                uint32_t n = 0, at = t->in_head;
                for (int k = 0; k < t->nrec; k++) {
                    uint32_t len = t->rec[(t->rec_head + k) % RECS];
                    if (!len) { tmp[n++] = eof; continue; }
                    for (uint32_t i = 0; i < len; i++) tmp[n++] = t->in[(at + i) % INQ];
                    at = (at + len) % INQ;
                }
                t->in_head = 0;
                t->in_len = 0;
                in_put(t, tmp, n);
            }
        }
        in_put(t, t->line, t->llen);
        t->llen = 0;
        t->nrec = 0;
        t->rec_head = 0;
    } else if (!was && now && t->in_len) {               /* what was typed: one line */
        t->nrec = 1;
        t->rec_head = 0;
        t->rec[0] = t->in_len;
    }
    if (!(in->c_iflag & IXON)) t->stopped = 0;
    spin_unlock_irq(&g_lock, fl);
    kfile_changed();
}

void pty_get_winsize(pty_t* t, pty_winsize_t* out) { *out = t->ws; }

void pty_set_winsize(pty_t* t, const pty_winsize_t* in) {
    if (!memcmp(&t->ws, in, sizeof(*in))) return;
    t->ws = *in;
    t->events |= PTY_EV_SIGWINCH;
}

void pty_read_mode(pty_t* t, int* canon, int* vmin, int* vtime) {
    *canon = (t->tio.c_lflag & ICANON) != 0;
    *vmin = t->tio.c_cc[VMIN];
    *vtime = t->tio.c_cc[VTIME];
}

/* ── tests ─────────────────────────────────────────────────────────── */

void pty_tests(void (*report)(const char*, int, const char*)) {
#define CHECK(c) report("pty: " #c, (c) != 0, NULL)
    int before = pty_count();
    pty_t* t = pty_new();
    CHECK(t != NULL);
    if (!t) return;
    pty_slave_ref(t, 1);
    uint8_t b[256];
    long n;
    /* canonical: echo, a line at a time, erase, \r -> \n, \n -> \r\n */
    CHECK(pty_master_write(t, (const uint8_t*)"ab\x7f" "c\r", 5) == 5);
    CHECK(pty_master_read(t, b, sizeof(b)) == 8 && !memcmp(b, "ab\b \bc\r\n", 8));
    CHECK(pty_slave_read(t, b, sizeof(b)) == 3 && !memcmp(b, "ac\n", 3));
    CHECK(pty_slave_read(t, b, sizeof(b)) == -11);
    CHECK(pty_master_write(t, (const uint8_t*)"x\x15y\n", 4) == 4);      /* ^U kills the line */
    CHECK(pty_slave_read(t, b, sizeof(b)) == 2 && !memcmp(b, "y\n", 2));
    pty_master_read(t, b, sizeof(b));
    /* ^D: on a line, what was typed; on an empty one, the end */
    pty_master_write(t, (const uint8_t*)"ok\x04\x04", 4);
    CHECK(pty_slave_read(t, b, sizeof(b)) == 2 && !memcmp(b, "ok", 2));
    CHECK(pty_slave_read(t, b, sizeof(b)) == 0);
    /* ^C: SIGINT for the foreground, echoed ^C, the line dropped */
    pty_master_read(t, b, sizeof(b));
    pty_master_write(t, (const uint8_t*)"zz\x03", 3);
    CHECK(pty_events(t) == PTY_EV_SIGINT);
    n = pty_master_read(t, b, sizeof(b));
    CHECK(n == 2 && !memcmp(b, "^C", 2));
    CHECK(pty_slave_read(t, b, sizeof(b)) == -11);
    /* the programs' output: \n -> \r\n */
    CHECK(pty_slave_write(t, (const uint8_t*)"hi\n", 3) == 3);
    CHECK(pty_master_read(t, b, sizeof(b)) == 4 && !memcmp(b, "hi\r\n", 4));
    /* raw mode (a readline, an editor): every byte at once, no echo */
    pty_termios_t tio;
    pty_get_termios(t, &tio);
    tio.c_lflag &= ~(uint32_t)(ICANON | ECHO);
    pty_set_termios(t, &tio, 0);
    pty_master_write(t, (const uint8_t*)"\x1b[A", 3);
    CHECK(pty_master_read(t, b, sizeof(b)) == -11);
    CHECK(pty_slave_read(t, b, 2) == 2 && pty_slave_read(t, b + 2, 8) == 1 && !memcmp(b, "\x1b[A", 3));
    /* ^D typed while lines are still edited, then line editing off (bash's
     * readline taking the terminal back): the lines, then the ^D itself */
    tio.c_lflag |= ICANON | ECHO;
    pty_set_termios(t, &tio, 0);
    pty_master_write(t, (const uint8_t*)"ls\r\x04", 4);
    pty_master_read(t, b, sizeof(b));
    tio.c_lflag &= ~(uint32_t)(ICANON | ECHO);
    pty_set_termios(t, &tio, 0);
    CHECK(pty_poll(t, 0, KF_POLLIN) & KF_POLLIN);
    CHECK(pty_slave_read(t, b, sizeof(b)) == 4 && !memcmp(b, "ls\n\x04", 4));
    /* the window size: a SIGWINCH when it changes */
    pty_winsize_t ws = { 50, 132, 0, 0 };
    pty_set_winsize(t, &ws);
    CHECK(pty_events(t) == PTY_EV_SIGWINCH);
    /* the master closes: the slave's end, writes EIO; the slave too: gone */
    pty_master_ref(t, -1);
    CHECK(pty_events(t) == PTY_EV_HANGUP);
    CHECK(pty_slave_read(t, b, sizeof(b)) == 0 && pty_slave_write(t, (const uint8_t*)"x", 1) == -5);
    pty_slave_ref(t, -1);
    CHECK(pty_count() == before);
#undef CHECK
}
