/* SPDX-License-Identifier: BSD-2-Clause */
/* Banana OS SDK: terminals - termios, the controlling terminal's foreground
 * group, pseudo-terminals (posix_openpt, openpty, forkpty), ttyname. The
 * system's ptys honour every mode (API version 27, kernel/pty.c); the
 * console keeps its own (lines, echo) and only remembers what was set. */
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include "banana_api.h"

extern const banana_api_t* __banana;

#define HAS_TTY BANANA_HAS(__banana, tty_ioctl)

/* what fd is: BANANA_FD_*, -1 not open; *num a pty's number */
static int fd_kind(int fd, int* num) {
    banana_fdinfo_t fi;
    if (!BANANA_HAS(__banana, fd_info)) return fd >= 0 && fd <= 2 ? BANANA_FD_TERM : -1;
    if (__banana->fd_info(fd, &fi) != 0) return -1;
    if (num) *num = (int)fi.size;
    return fi.type;
}

/* the system's ioctl on a terminal: 0 / -1 with errno */
int __tty_ioctl(int fd, unsigned long req, void* arg) {
    if (!HAS_TTY) { errno = ENOTTY; return -1; }
    int r = __banana->tty_ioctl(fd, req, arg);
    if (r < 0) { errno = -r; return -1; }
    return r;
}

int isatty(int fd) {
    int k = fd_kind(fd, NULL);
    if (k == BANANA_FD_TERM || k == BANANA_FD_PTS || k == BANANA_FD_PTM) return 1;
    errno = k < 0 ? EBADF : ENOTTY;
    return 0;
}

/* ── termios ──────────────────────────────────────────────────────── */

/* the console's modes: lines and echo, whatever was asked (remembered) */
static struct termios g_cons;
static int g_cons_set;

static void cons_defaults(struct termios* t) {
    memset(t, 0, sizeof(*t));
    t->c_iflag = ICRNL | IXON | IUTF8;
    t->c_oflag = OPOST | ONLCR;
    t->c_cflag = B38400 | CS8 | CREAD | HUPCL;
    t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
    t->c_cc[VINTR] = 3; t->c_cc[VQUIT] = 034; t->c_cc[VERASE] = 0177; t->c_cc[VKILL] = 025;
    t->c_cc[VEOF] = 4; t->c_cc[VMIN] = 1; t->c_cc[VSTART] = 021; t->c_cc[VSTOP] = 023;
    t->c_cc[VSUSP] = 032; t->c_cc[VREPRINT] = 022; t->c_cc[VWERASE] = 027; t->c_cc[VLNEXT] = 026;
    t->c_ispeed = t->c_ospeed = B38400;
}

int tcgetattr(int fd, struct termios* t) {
    int k = fd_kind(fd, NULL);
    if (k == BANANA_FD_PTS || k == BANANA_FD_PTM) return __tty_ioctl(fd, TCGETS, t);
    if (k != BANANA_FD_TERM) { errno = k < 0 ? EBADF : ENOTTY; return -1; }
    if (!g_cons_set) { cons_defaults(&g_cons); g_cons_set = 1; }
    *t = g_cons;
    return 0;
}

int tcsetattr(int fd, int when, const struct termios* t) {
    if (when < TCSANOW || when > TCSAFLUSH) { errno = EINVAL; return -1; }
    int k = fd_kind(fd, NULL);
    if (k == BANANA_FD_PTS || k == BANANA_FD_PTM)
        return __tty_ioctl(fd, when == TCSANOW ? TCSETS : when == TCSADRAIN ? TCSETSW : TCSETSF, (void*)t);
    if (k != BANANA_FD_TERM) { errno = k < 0 ? EBADF : ENOTTY; return -1; }
    g_cons = *t;
    g_cons_set = 1;
    return 0;
}

int tcsendbreak(int fd, int duration) { (void)duration; return isatty(fd) ? 0 : -1; }
int tcdrain(int fd) { return isatty(fd) ? 0 : -1; }

int tcflush(int fd, int queue) {
    if (queue < TCIFLUSH || queue > TCIOFLUSH) { errno = EINVAL; return -1; }
    int k = fd_kind(fd, NULL);
    if (k == BANANA_FD_PTS || k == BANANA_FD_PTM) return __tty_ioctl(fd, TCFLSH, (void*)(long)queue);
    return isatty(fd) ? 0 : -1;
}

int tcflow(int fd, int action) {
    if (action < TCOOFF || action > TCION) { errno = EINVAL; return -1; }
    return isatty(fd) ? 0 : -1;
}

pid_t tcgetpgrp(int fd) {
    int k = fd_kind(fd, NULL), pg = 0;
    if (k == BANANA_FD_TERM) return getpgrp();             /* (the console: its own group) */
    if (__tty_ioctl(fd, TIOCGPGRP, &pg) < 0) return -1;
    return pg;
}

int tcsetpgrp(int fd, pid_t pgrp) {
    int k = fd_kind(fd, NULL);
    if (k == BANANA_FD_TERM) return 0;
    int pg = pgrp;
    return __tty_ioctl(fd, TIOCSPGRP, &pg);
}

pid_t tcgetsid(int fd) {
    int k = fd_kind(fd, NULL), sid = 0;
    if (k == BANANA_FD_TERM) return getsid(0);
    if (__tty_ioctl(fd, TIOCGSID, &sid) < 0) return -1;
    return sid;
}

void cfmakeraw(struct termios* t) {
    t->c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~(tcflag_t)OPOST;
    t->c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(tcflag_t)(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

speed_t cfgetispeed(const struct termios* t) { return t->c_ispeed; }
speed_t cfgetospeed(const struct termios* t) { return t->c_ospeed; }
int cfsetispeed(struct termios* t, speed_t s) { t->c_ispeed = s; return 0; }
int cfsetospeed(struct termios* t, speed_t s) {
    t->c_ospeed = s;
    t->c_cflag = (t->c_cflag & ~(tcflag_t)CBAUD) | (s & CBAUD);
    return 0;
}
int cfsetspeed(struct termios* t, speed_t s) { cfsetispeed(t, s); return cfsetospeed(t, s); }

/* ── pseudo-terminals ─────────────────────────────────────────────── */

int posix_openpt(int flags) {
    if (flags & ~(O_RDWR | O_NOCTTY | O_CLOEXEC)) { errno = EINVAL; return -1; }
    return open("/dev/ptmx", flags);
}

int grantpt(int fd) {
    int k = fd_kind(fd, NULL);
    if (k != BANANA_FD_PTM) { errno = k < 0 ? EBADF : EINVAL; return -1; }
    return 0;                                               /* (one user: the slave is already ours) */
}

int unlockpt(int fd) {
    int zero = 0;
    return __tty_ioctl(fd, TIOCSPTLCK, &zero);
}

int ptsname_r(int fd, char* buf, size_t len) {
    int n;
    if (__tty_ioctl(fd, TIOCGPTN, &n) < 0) return errno;
    if ((size_t)snprintf(buf, len, "/dev/pts/%d", n) >= len) return ERANGE;
    return 0;
}

char* ptsname(int fd) {
    static char name[32];
    int e = ptsname_r(fd, name, sizeof(name));
    if (e) { errno = e; return NULL; }
    return name;
}

int openpty(int* amaster, int* aslave, char* name, const struct termios* tio, const struct winsize* ws) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -1;
    char path[32];
    int e = unlockpt(m) < 0 ? errno : ptsname_r(m, path, sizeof(path));
    int s = e ? -1 : open(path, O_RDWR | O_NOCTTY);
    if (s < 0) { if (!e) e = errno; close(m); errno = e; return -1; }
    if (tio) tcsetattr(s, TCSANOW, tio);
    if (ws) ioctl(s, TIOCSWINSZ, ws);
    if (name) strcpy(name, path);
    *amaster = m;
    *aslave = s;
    return 0;
}

/* the calling process: a new session, fd its controlling terminal and
 * its 0, 1, 2 */
int login_tty(int fd) {
    setsid();
    if (ioctl(fd, TIOCSCTTY, 0) < 0) return -1;
    for (int i = 0; i <= 2; i++) if (fd != i && dup2(fd, i) < 0) return -1;
    if (fd > 2) close(fd);
    return 0;
}

pid_t forkpty(int* amaster, char* name, const struct termios* tio, const struct winsize* ws) {
    int m, s;
    if (openpty(&m, &s, name, tio, ws) < 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(m); close(s); return -1; }
    if (pid == 0) {
        close(m);
        if (login_tty(s) < 0) _exit(1);
        return 0;
    }
    close(s);
    *amaster = m;
    return pid;
}

/* ── names ────────────────────────────────────────────────────────── */

int ttyname_r(int fd, char* buf, size_t len) {
    int n = 0, k = fd_kind(fd, &n);
    const char* s;
    char tmp[32];
    if (k < 0) return EBADF;
    if (k == BANANA_FD_PTS) { snprintf(tmp, sizeof(tmp), "/dev/pts/%d", n); s = tmp; }
    else if (k == BANANA_FD_PTM) s = "/dev/ptmx";
    else if (k == BANANA_FD_TERM) s = "/dev/console";
    else return ENOTTY;
    if (strlen(s) >= len) return ERANGE;
    strcpy(buf, s);
    return 0;
}

char* ttyname(int fd) {
    static char name[32];
    int e = ttyname_r(fd, name, sizeof(name));
    if (e) { errno = e; return NULL; }
    return name;
}

/* a /dev device's stat (posix.c: stat): 0, or -1 if path names none */
int __dev_stat(const char* path, struct stat* st) {
    static const struct { const char* name; unsigned mode, major, minor; } devs[] = {
        { "null", S_IFCHR | 0666, 1, 3 }, { "zero", S_IFCHR | 0666, 1, 5 }, { "full", S_IFCHR | 0666, 1, 7 },
        { "random", S_IFCHR | 0666, 1, 8 }, { "urandom", S_IFCHR | 0666, 1, 9 }, { "tty", S_IFCHR | 0666, 5, 0 },
        { "console", S_IFCHR | 0600, 5, 1 }, { "ptmx", S_IFCHR | 0666, 5, 2 }, { "pts", S_IFDIR | 0755, 0, 0 },
        { "stdin", S_IFCHR | 0620, 5, 1 }, { "stdout", S_IFCHR | 0620, 5, 1 }, { "stderr", S_IFCHR | 0620, 5, 1 },
        { "fd", S_IFDIR | 0755, 0, 0 },
    };
    if (strncmp(path, "/dev/", 5)) return -1;
    const char* n = path + 5;
    memset(st, 0, sizeof(*st));
    st->st_nlink = 1;
    st->st_blksize = 4096;
    for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++) {
        if (strcmp(n, devs[i].name)) continue;
        st->st_mode = devs[i].mode;
        st->st_rdev = (devs[i].major << 8) | devs[i].minor;
        st->st_ino = 1000 + i;
        return 0;
    }
    if (!strncmp(n, "pts/", 4) && n[4] >= '0' && n[4] <= '9') {
        int fd = open(path, O_RDWR | O_NOCTTY);              /* (there, and not locked) */
        if (fd < 0) return -1;
        close(fd);
        int num = atoi(n + 4);
        st->st_mode = S_IFCHR | 0620;
        st->st_rdev = (136u << 8) | (unsigned)num;
        st->st_ino = 2000 + (unsigned)num;
        st->st_uid = st->st_gid = 1000;
        return 0;
    }
    return -1;
}
