/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PTY_H
#define PTY_H

#include "types.h"

/*
 * Pseudo-terminals (/dev/ptmx, /dev/pts/N) and their line discipline:
 * what a terminal emulator writes into the master is "typed" - edited into
 * lines (ICANON), echoed (ECHO), turned into signals (ISIG: ^C, ^\, ^Z) -
 * and read by the programs on the slave; what they write comes out of the
 * master, newlines made \r\n (OPOST, ONLCR). Modes are Linux's termios
 * (the SDK's struct termios, the same flags and control characters), the
 * window size Linux's struct winsize.
 *
 * As many as memory allows (indexes reused once both ends closed). This
 * file has the queues and the editing; kernel/app.c the descriptors, the
 * waits and the processes (who is in the foreground, the signals sent to
 * a process group, SIGHUP when the master goes) through pty_events().
 *
 * Everything here runs on the boot core (system calls go there), under the
 * pty's lock where the queues change.
 */

#define PTY_NCCS 32

/* the SDK's struct termios (Linux's glibc layout) */
typedef struct {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[PTY_NCCS];
    uint32_t c_ispeed, c_ospeed;
} pty_termios_t;

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } pty_winsize_t;

typedef struct pty pty_t;

/* things the line discipline asks of the processes (pty_events) */
#define PTY_EV_SIGINT   0x01
#define PTY_EV_SIGQUIT  0x02
#define PTY_EV_SIGTSTP  0x04
#define PTY_EV_SIGWINCH 0x08
#define PTY_EV_HANGUP   0x10    /* the master closed: SIGHUP (+ SIGCONT) to the session */

pty_t*   pty_new(void);                         /* a master: refs 1 master, 0 slaves; NULL if none */
int      pty_index(pty_t* t);                   /* N of /dev/pts/N */
pty_t*   pty_find(int index);                   /* a pty whose master is open, NULL */
int      pty_master_ref(pty_t* t, int delta);   /* +1 / -1 master descriptions; the last: hangup. 1: it is gone (nobody else held it) */
void     pty_slave_ref(pty_t* t, int delta);    /* +1 / -1 slave descriptions */
void     pty_hold(pty_t* t, int delta);         /* +1 / -1: a process's controlling terminal (kept while it is) */
int      pty_locked(pty_t* t);
void     pty_set_locked(pty_t* t, int locked);
int      pty_hung_up(pty_t* t);
int      pty_count(void);                       /* ptys in use (tests) */

/* the controlling terminal side: the session it belongs to, its foreground group */
int      pty_session(pty_t* t);
void     pty_set_session(pty_t* t, int sid);
int      pty_fg(pty_t* t);
void     pty_set_fg(pty_t* t, int pgrp);

void     pty_get_termios(pty_t* t, pty_termios_t* out);
void     pty_set_termios(pty_t* t, const pty_termios_t* in, int flush_input);
void     pty_get_winsize(pty_t* t, pty_winsize_t* out);
void     pty_set_winsize(pty_t* t, const pty_winsize_t* in);    /* (changed: a SIGWINCH event) */

/* the master: typed bytes in (all taken: a full line buffer drops what
 * does not fit, like a real terminal); the programs' output out (0 if none) */
long     pty_master_write(pty_t* t, const uint8_t* buf, unsigned long n);
long     pty_master_read(pty_t* t, uint8_t* buf, unsigned long n);
/* the slave: a read (-11 when it would wait, 0 at the end: ^D or a hangup);
 * a write (what fits in the output queue, -11 if none; -5 hung up) */
long     pty_slave_read(pty_t* t, uint8_t* buf, unsigned long n);
long     pty_slave_write(pty_t* t, const uint8_t* buf, unsigned long n);
/* what is ready (KF_POLL* of kernel/kfile.h) for the master / slave side */
int      pty_poll(pty_t* t, int master, int events);
int      pty_nread(pty_t* t, int master);        /* FIONREAD */
void     pty_flush(pty_t* t, int queue);         /* TCFLSH: 0 input, 1 output, 2 both */
/* events (PTY_EV_*) raised since the last call, cleared */
int      pty_events(pty_t* t);
/* VMIN / VTIME of a non-canonical read: how many bytes it waits for, how
 * long (tenths of a second, 0: no limit once one came) */
void     pty_read_mode(pty_t* t, int* canon, int* vmin, int* vtime);

void     pty_tests(void (*report)(const char*, int, const char*));

#endif
