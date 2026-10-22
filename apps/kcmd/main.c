/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The system's own commands as programs: startx, install, pkg, ifconfig,
 * theme, selftest... - each /bin/<name> is a link to this program, which
 * hands its name and arguments to the system (banana_api_t's kcmd, API 30).
 * The command runs there as it did in the old built-in shell, with this
 * program's input and output: a terminal gets its colours and cursor
 * moves as escapes, a pipe or a file the plain text.
 *   kcmd COMMAND [ARGS]       (the same, named) */
#include <banana.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

int main(int argc, char** argv) {
    const char* me = strrchr(argv[0], '/');
    me = me ? me + 1 : argv[0];
    int first = 0;
    if (!strcmp(me, "kcmd") || !strncmp(me, "app-", 4)) {
        if (argc < 2) { fprintf(stderr, "usage: kcmd COMMAND [ARGS...]  (a command of the system's)\n"); return 2; }
        me = argv[1];
        first = 1;
    }
    if (!BANANA_HAS(__banana, kcmd)) { fprintf(stderr, "%s: this system cannot run its commands for programs (API 30)\n", me); return 126; }
    /* the line: the name, then each argument - quoted when it has spaces
     * (the system's commands take "a b" as one word) */
    static char line[4096];
    size_t n = (size_t)snprintf(line, sizeof line, "%s", me);
    for (int i = first + 1; i < argc && n < sizeof line - 4; i++) {
        const char* a = argv[i];
        int q = !*a || strpbrk(a, " \t") != NULL;
        char qc = strchr(a, '"') ? '\'' : '"';
        if (q) n += (size_t)snprintf(line + n, sizeof line - n, " %c%s%c", qc, a, qc);
        else n += (size_t)snprintf(line + n, sizeof line - n, " %s", a);
    }
    if (n >= sizeof line) { fprintf(stderr, "%s: the line is too long\n", me); return 2; }
    fflush(stdout);
    /* a terminal: keys one by one, unechoed, ^C a key - the command reads
     * them itself (top, edit, a yes / no question) */
    struct termios old, raw;
    int tty = isatty(0) && tcgetattr(0, &old) == 0;
    if (tty) {
        raw = old;
        raw.c_lflag &= ~(tcflag_t)(ICANON | ECHO | ISIG | IEXTEN);
        raw.c_iflag &= ~(tcflag_t)(IXON);
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &raw);
    }
    int rc = __banana->kcmd(line);
    if (tty) tcsetattr(0, TCSANOW, &old);
    if (rc == 127) fprintf(stderr, "%s: not a command of this system\n", me);
    return rc < 0 ? 1 : rc;
}
