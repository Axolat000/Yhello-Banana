/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TTY_H
#define TTY_H

#include "types.h"

/*
 * Remote terminals (SSH sessions).
 *
 * A tty is a virtual terminal (vt) plus a shell task bound to it, whose
 * keyboard is a byte queue filled by the network and whose screen output
 * is also copied, as a plain ANSI byte stream, to a queue the network
 * drains - the same way the COM1 console mirrors vt0. The shell code
 * itself does not know the difference: keyboard_try_getchar() reads the
 * calling task's tty, gui_focused_vt() says a tty's own vt has focus.
 */

#define TTY_MAX 2

int  tty_create(void);                 /* allocates a vt; tty id or -1 */
int  tty_vt(int t);
void tty_bind(int t);                  /* the calling task is t's shell */
int  tty_current(void);                /* tty of the calling task, or -1 */

/* server side */
void     tty_attach(int t, const char* user, const char* exec_cmd);
void     tty_hangup(int t);            /* client gone: the shell ends its session */
void     tty_detach(int t);            /* session over, tty free for the next client */
int      tty_in_use(int t);
int      tty_finished(int t);          /* the shell ended the session (exit) */
void     tty_input(int t, const uint8_t* d, uint32_t n);
uint32_t tty_output(int t, uint8_t* out, uint32_t max);
uint32_t tty_output_pending(int t);

/* shell side */
int         tty_active(int t);         /* attached and not hung up */
const char* tty_exec_cmd(int t);       /* "" for an interactive shell */
const char* tty_user(int t);
void        tty_finish(int t);         /* `exit` */
char        tty_getkey(int t);         /* 0 if nothing; Ctrl+C (3) once hung up */
void        tty_write(int t, const char* s);

/* for a program on a pty instead (64-bit: bash, shell/shell.c): the bytes
 * as they came (-1: the session is gone), out as they are; the client's
 * window size (1 if it changed since the last look) */
int  tty_read(int t, uint8_t* out, uint32_t max);
void tty_write_n(int t, const uint8_t* s, uint32_t n);
void tty_set_size(int t, int cols, int rows);
void tty_set_exit(int t, int code);        /* the session's exit status (SSH tells the client) */
int  tty_exit_status(int t);
int  tty_take_size(int t, int* cols, int* rows);

/* hooks from the terminal: output written to `vt` */
void tty_mirror(int vt, char c);
void tty_clear_screen(int vt);

#endif
