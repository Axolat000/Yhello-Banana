/* SPDX-License-Identifier: GPL-3.0-or-later */
/* ps, pgrep, pkill, pidof - the processes, from the system's list
 * (banana_api_t's proc_list, API version 30): one program, its name says
 * which. ps takes the usual Unix and BSD forms: ps, ps -e / -A / ax,
 * -f, -l, -j, u / aux, -o FIELDS, -p PIDS, -C NAME, -t TTY, --no-headers.
 * TIME and %CPU: the main thread's. */
#include <banana.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static banana_procinfo_t* g_p;
static int g_n;

static int load(void) {
    if (!BANANA_HAS(__banana, proc_list)) {
        fprintf(stderr, "the system has no process list (Banana OS API 30 needed)\n");
        return -1;
    }
    g_p = calloc(1024, sizeof(*g_p));
    if (!g_p) return -1;
    g_n = __banana->proc_list(g_p, 1024);
    return g_n < 0 ? -1 : 0;
}

static const banana_procinfo_t* by_pid(int pid) {
    for (int i = 0; i < g_n; i++) if (g_p[i].pid == pid) return &g_p[i];
    return NULL;
}

static void tty_name(int t, char* out, size_t cap) {
    if (t >= 0) snprintf(out, cap, "pts/%d", t);
    else snprintf(out, cap, t == -2 ? "console" : "?");
}

/* the command's name: its first word's last part */
static void comm_of(const banana_procinfo_t* p, char* out, size_t cap) {
    const char* c = p->cmdline[0] ? p->cmdline : p->name;
    size_t l = strcspn(c, " ");
    const char* s = c;
    for (size_t i = 0; i < l; i++) if (c[i] == '/') s = c + i + 1;
    size_t n = (size_t)(c + l - s);
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

static void etime(unsigned long long start, char* out, size_t cap) {
    unsigned long long s = ((unsigned long long)banana_ticks() - start) / 1000;
    unsigned d = (unsigned)(s / 86400), h = (unsigned)(s / 3600 % 24), m = (unsigned)(s / 60 % 60), x = (unsigned)(s % 60);
    if (d) snprintf(out, cap, "%u-%02u:%02u:%02u", d, h, m, x);
    else if (h) snprintf(out, cap, "%02u:%02u:%02u", h, m, x);
    else snprintf(out, cap, "%02u:%02u", m, x);
}

/* ── ps ──────────────────────────────────────────────────────────────── */

/* a column: its name, header, width (negative: left-aligned) */
typedef struct { const char* key; const char* head; int w; } col_t;
static const col_t COLS[] = {
    { "pid", "PID", 7 }, { "ppid", "PPID", 7 }, { "pgid", "PGID", 7 }, { "pgrp", "PGRP", 7 }, { "sid", "SID", 7 },
    { "sess", "SESS", 7 }, { "tpgid", "TPGID", 7 }, { "tty", "TTY", -8 }, { "tt", "TT", -8 }, { "stat", "STAT", -4 },
    { "state", "S", -1 }, { "s", "S", -1 }, { "comm", "COMMAND", -15 }, { "ucomm", "COMMAND", -15 },
    { "cmd", "CMD", 0 }, { "args", "COMMAND", 0 }, { "command", "COMMAND", 0 }, { "wchan", "WCHAN", -12 },
    { "rss", "RSS", 6 }, { "rsz", "RSZ", 6 }, { "vsz", "VSZ", 7 }, { "sz", "SZ", 6 }, { "etime", "ELAPSED", 11 },
    { "time", "TIME", 8 }, { "cputime", "TIME", 8 }, { "user", "USER", -8 }, { "euser", "EUSER", -8 },
    { "uid", "UID", 5 }, { "nlwp", "NLWP", 4 }, { "thcount", "THCNT", 5 }, { "pcpu", "%CPU", 4 }, { "%cpu", "%CPU", 4 },
    { "pmem", "%MEM", 4 }, { "%mem", "%MEM", 4 }, { "ni", "NI", 3 }, { "nice", "NI", 3 }, { "pri", "PRI", 3 },
    { "f", "F", 1 }, { "c", "C", 2 }, { "stime", "STIME", -5 }, { "start", "START", -5 }, { "lstart", "STARTED", -24 },
    { "pending", "PENDING", 16 }, { "uip", "UIP", 10 }, { "sched", "T", 1 },
};

static const col_t* col_find(const char* k) {
    for (size_t i = 0; i < sizeof(COLS) / sizeof(COLS[0]); i++) if (!strcmp(COLS[i].key, k)) return &COLS[i];
    return NULL;
}

static unsigned long long g_ram;

static void field(const col_t* c, const banana_procinfo_t* p, char* out, size_t cap) {
    const char* k = c->key;
    if (!strcmp(k, "pid")) snprintf(out, cap, "%d", p->pid);
    else if (!strcmp(k, "ppid")) snprintf(out, cap, "%d", p->ppid);
    else if (!strcmp(k, "pgid") || !strcmp(k, "pgrp")) snprintf(out, cap, "%d", p->pgid);
    else if (!strcmp(k, "sid") || !strcmp(k, "sess")) snprintf(out, cap, "%d", p->sid);
    else if (!strcmp(k, "tpgid")) snprintf(out, cap, "%d", p->tty >= 0 ? p->tpgid : -1);
    else if (!strcmp(k, "tty") || !strcmp(k, "tt")) tty_name(p->tty, out, cap);
    else if (!strcmp(k, "stat")) {
        /* the BSD letters: s a session leader, + in its terminal's foreground, l threads */
        snprintf(out, cap, "%c%s%s%s", p->state, p->sid == p->pid ? "s" : "", p->threads > 1 ? "l" : "",
                 p->tty >= 0 && p->tpgid == p->pgid ? "+" : "");
    }
    else if (!strcmp(k, "state") || !strcmp(k, "s")) snprintf(out, cap, "%c", p->state);
    else if (!strcmp(k, "comm") || !strcmp(k, "ucomm")) comm_of(p, out, cap);
    else if (!strcmp(k, "cmd") || !strcmp(k, "args") || !strcmp(k, "command"))
        snprintf(out, cap, "%s%s", p->cmdline[0] ? p->cmdline : p->name, p->state == 'Z' ? " <defunct>" : "");
    else if (!strcmp(k, "wchan")) snprintf(out, cap, "%s", p->state == 'S' && p->wchan[0] ? p->wchan : "-");
    else if (!strcmp(k, "rss") || !strcmp(k, "rsz") || !strcmp(k, "vsz")) snprintf(out, cap, "%llu", p->mem / 1024);
    else if (!strcmp(k, "sz")) snprintf(out, cap, "%llu", p->mem / 4096);
    else if (!strcmp(k, "etime")) etime(p->start_ms, out, cap);
    else if (!strcmp(k, "time") || !strcmp(k, "cputime")) {
        unsigned long long sec = p->cpu_ms / 1000;
        snprintf(out, cap, "%02llu:%02llu:%02llu", sec / 3600, sec / 60 % 60, sec % 60);
    }
    else if (!strcmp(k, "user") || !strcmp(k, "euser")) snprintf(out, cap, "banana");
    else if (!strcmp(k, "uid")) snprintf(out, cap, "1000");
    else if (!strcmp(k, "nlwp") || !strcmp(k, "thcount")) snprintf(out, cap, "%u", p->threads);
    else if (!strcmp(k, "pcpu") || !strcmp(k, "%cpu")) snprintf(out, cap, "%u.0", p->cpu_pct);
    else if (!strcmp(k, "pmem") || !strcmp(k, "%mem")) snprintf(out, cap, "%.1f", g_ram ? 100.0 * (double)p->mem / (double)g_ram : 0.0);
    else if (!strcmp(k, "ni") || !strcmp(k, "nice")) snprintf(out, cap, "0");
    else if (!strcmp(k, "pri")) snprintf(out, cap, "80");
    else if (!strcmp(k, "f")) snprintf(out, cap, "0");
    else if (!strcmp(k, "c")) snprintf(out, cap, "0");
    else if (!strcmp(k, "stime") || !strcmp(k, "start") || !strcmp(k, "lstart")) {
        time_t t = time(NULL) - (time_t)(((unsigned long long)banana_ticks() - p->start_ms) / 1000);
        struct tm tm;
        localtime_r(&t, &tm);
        strftime(out, cap, !strcmp(k, "lstart") ? "%a %b %e %H:%M:%S %Y" : "%H:%M", &tm);
    }
    else if (!strcmp(k, "pending")) snprintf(out, cap, "%016llx", p->sigpending);
    else if (!strcmp(k, "uip")) snprintf(out, cap, "%llx", p->uip);
    else if (!strcmp(k, "sched")) snprintf(out, cap, "%c", p->sched ? p->sched : '-');
    else snprintf(out, cap, "?");
}

static int ps_main(int argc, char** argv) {
    int all = 0, notty_too = 0, headers = 1;
    const char* fmt = NULL;
    char fmtbuf[512] = "";
    int pids[64], npid = 0, sel_tty = -100;
    const char* names[16];
    int nname = 0;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (!strcmp(a, "--no-headers") || !strcmp(a, "--no-heading")) { headers = 0; continue; }
        if (!strcmp(a, "--help")) {
            printf("usage: ps [-e|-A] [-f] [-l] [-j] [-o FIELDS] [-p PIDS] [-C NAME] [-t TTY] [--no-headers]\n"
                   "       ps [a][x][u]   (BSD: aux, ax, u)\n");
            return 0;
        }
        if (a[0] == '-' && a[1] && a[1] != '-') {
            for (const char* c = a + 1; *c; c++) {
                const char* val = NULL;
                if (strchr("opCtUu", *c)) {
                    val = c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : NULL);
                    if (!val) { fprintf(stderr, "ps: -%c needs a value\n", *c); return 1; }
                }
                switch (*c) {
                case 'e': case 'A': all = 1; break;
                case 'a': all = 1; break;
                case 'x': notty_too = 1; break;
                case 'f': fmt = "uid,pid,ppid,c,stime,tty,time,cmd"; break;
                case 'l': fmt = "f,s,uid,pid,ppid,c,pri,ni,sz,wchan,tty,time,comm"; break;
                case 'j': fmt = "pid,pgid,sid,tty,time,comm"; break;
                case 'w': break;
                case 'H': break;
                case 'o':
                    if (strlen(fmtbuf) + strlen(val) + 2 < sizeof(fmtbuf)) {
                        if (fmtbuf[0]) strcat(fmtbuf, ",");
                        strcat(fmtbuf, val);
                    }
                    fmt = fmtbuf;
                    break;
                case 'p':
                    for (char* s = (char*)val; *s && npid < 64; ) { pids[npid++] = atoi(s); s += strcspn(s, ", "); s += strspn(s, ", "); }
                    break;
                case 'C': if (nname < 16) names[nname++] = val; break;
                case 't': sel_tty = !strncmp(val, "pts/", 4) ? atoi(val + 4) : atoi(val); break;
                case 'U': case 'u': all = 1; break;
                default: fprintf(stderr, "ps: unknown option -%c\n", *c); return 1;
                }
                if (val) break;
            }
            continue;
        }
        /* BSD style: aux, ax, u */
        for (const char* c = a; *c; c++) {
            if (*c == 'a') all = 1;
            else if (*c == 'x') notty_too = 1;
            else if (*c == 'u') fmt = "user,pid,pcpu,pmem,vsz,rss,tty,stat,start,time,command";
            else if (*c == 'j') fmt = "ppid,pid,pgid,sid,tty,tpgid,stat,uid,time,command";
            else if (*c == 'l') fmt = "f,uid,pid,ppid,pri,ni,vsz,rss,wchan,stat,tty,time,command";
            else if (*c == 'w' || *c == 'f' || *c == 'e') {}
            else { fprintf(stderr, "ps: unknown option %c\n", *c); return 1; }
        }
    }
    if (load()) return 1;
    g_ram = (unsigned long long)sysconf(_SC_PHYS_PAGES) * 4096ull;
    if (!fmt) fmt = "pid,tty,time,cmd";
    /* the columns */
    const col_t* cols[32];
    char heads[32][32];
    int nc = 0;
    char buf[512];
    snprintf(buf, sizeof buf, "%s", fmt);
    for (char* t = strtok(buf, ", "); t && nc < 32; t = strtok(NULL, ", ")) {
        char* eq = strchr(t, '=');
        if (eq) *eq = 0;
        const col_t* c = col_find(t);
        if (!c) { fprintf(stderr, "ps: unknown field: %s\n", t); return 1; }
        snprintf(heads[nc], sizeof heads[nc], "%s", eq ? eq + 1 : c->head);
        cols[nc++] = c;
    }
    /* which: mine (my terminal), or what was asked */
    const banana_procinfo_t* me = by_pid(getpid());
    int my_tty = me ? me->tty : -1;
    int show[1024], ns = 0;
    for (int i = 0; i < g_n; i++) {
        const banana_procinfo_t* p = &g_p[i];
        int ok;
        if (npid || nname || sel_tty != -100) {
            ok = 0;
            for (int k = 0; k < npid; k++) if (p->pid == pids[k]) ok = 1;
            char cm[64];
            comm_of(p, cm, sizeof cm);
            for (int k = 0; k < nname; k++) if (!strcmp(cm, names[k])) ok = 1;
            if (sel_tty != -100 && p->tty == sel_tty) ok = 1;
        } else if (all && notty_too) ok = 1;
        else if (all) ok = p->tty != -1 || fmt == NULL;
        else if (notty_too) ok = 1;
        else ok = p->tty == my_tty && (my_tty != -1 || p->sid == (me ? me->sid : -1));
        if (ok) show[ns++] = i;
    }
    /* -e / -A: everything (as all && x) */
    /* widths: the header's at least */
    int w[32];
    for (int c = 0; c < nc; c++) {
        int ww = cols[c]->w < 0 ? -cols[c]->w : cols[c]->w;
        int hl = (int)strlen(heads[c]);
        w[c] = ww > hl ? ww : hl;
    }
    for (int r = -1; r < ns; r++) {
        if (r < 0 && !headers) continue;
        char line[1024];
        size_t n = 0;
        for (int c = 0; c < nc; c++) {
            char v[200];
            if (r < 0) snprintf(v, sizeof v, "%s", heads[c]);
            else field(cols[c], &g_p[show[r]], v, sizeof v);
            int last = c == nc - 1;
            if (cols[c]->w == 0 || (last && cols[c]->w < 0)) n += (size_t)snprintf(line + n, sizeof line - n, "%s%s", v, last ? "" : " ");
            else if (cols[c]->w < 0) n += (size_t)snprintf(line + n, sizeof line - n, "%-*s ", w[c], v);
            else n += (size_t)snprintf(line + n, sizeof line - n, "%*s%s", w[c], v, last ? "" : " ");
            if (n >= sizeof line) n = sizeof line - 1;
        }
        while (n && line[n - 1] == ' ') line[--n] = 0;
        puts(line);
    }
    return 0;
}

/* ── pgrep, pkill, pidof ─────────────────────────────────────────────── */

static int sig_of(const char* s) {
    static const char* const N[] = { "", "HUP", "INT", "QUIT", "ILL", "TRAP", "ABRT", "BUS", "FPE", "KILL", "USR1", "SEGV",
        "USR2", "PIPE", "ALRM", "TERM", "STKFLT", "CHLD", "CONT", "STOP", "TSTP", "TTIN", "TTOU", "URG", "XCPU", "XFSZ",
        "VTALRM", "PROF", "WINCH", "IO", "PWR", "SYS" };
    if (isdigit((unsigned char)*s)) return atoi(s);
    if (!strncmp(s, "SIG", 3)) s += 3;
    for (int i = 1; i < 32; i++) if (!strcmp(s, N[i])) return i;
    return -1;
}

static int pgrep_main(int argc, char** argv, int kill_them) {
    int full = 0, exact = 0, list = 0, listfull = 0, newest = 0, oldest = 0, count = 0, inverse = 0, sig = SIGTERM;
    const char* delim = "\n";
    const char* pat = NULL;
    int parent = -1;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        if (a[0] == '-' && a[1]) {
            if (kill_them && (isdigit((unsigned char)a[1]) || isupper((unsigned char)a[1]))) {
                sig = sig_of(a + 1);
                if (sig < 0) { fprintf(stderr, "pkill: unknown signal %s\n", a + 1); return 2; }
                continue;
            }
            for (const char* c = a + 1; *c; c++) {
                switch (*c) {
                case 'f': full = 1; break;
                case 'x': exact = 1; break;
                case 'l': list = 1; break;
                case 'a': listfull = 1; break;
                case 'n': newest = 1; break;
                case 'o': oldest = 1; break;
                case 'c': count = 1; break;
                case 'v': inverse = 1; break;
                case 'd': delim = c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : "\n"); goto next;
                case 'P': parent = atoi(c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : "0")); goto next;
                case 's': if (kill_them) { sig = sig_of(c[1] ? c + 1 : (i + 1 < argc ? argv[++i] : "TERM")); goto next; }
                    /* fall through */
                default: fprintf(stderr, "%s: unknown option -%c\n", kill_them ? "pkill" : "pgrep", *c); return 2;
                }
            }
        next:
            continue;
        }
        pat = a;
    }
    if (!pat && parent < 0) { fprintf(stderr, "usage: %s [-flaxnocv] PATTERN\n", kill_them ? "pkill [-SIGNAL]" : "pgrep"); return 2; }
    if (load()) return 3;
    regex_t re;
    if (pat && regcomp(&re, pat, REG_EXTENDED | REG_NOSUB)) { fprintf(stderr, "bad pattern: %s\n", pat); return 2; }
    int me = getpid(), found = 0, first = 1;
    const banana_procinfo_t* pick = NULL;
    for (int i = 0; i < g_n; i++) {
        const banana_procinfo_t* p = &g_p[i];
        if (p->pid == me || p->state == 'Z') continue;
        char cm[64];
        comm_of(p, cm, sizeof cm);
        const char* subject = full ? p->cmdline : cm;
        int m = 1;
        if (pat) {
            m = exact ? (full ? !strcmp(p->cmdline, pat) : !strcmp(cm, pat)) : regexec(&re, subject, 0, NULL, 0) == 0;
            if (exact && !full && !m) {
                regmatch_t g;
                m = regexec(&re, subject, 1, &g, 0) == 0 && g.rm_so == 0 && subject[g.rm_eo] == 0;
            }
        }
        if (parent >= 0 && p->ppid != parent) m = 0;
        if (inverse) m = !m;
        if (!m) continue;
        if (newest || oldest) {
            if (!pick || (newest ? p->start_ms >= pick->start_ms : p->start_ms < pick->start_ms)) pick = p;
            continue;
        }
        found++;
        if (kill_them) { if (kill(p->pid, sig) && errno != ESRCH) fprintf(stderr, "pkill: killing %d: %s\n", p->pid, strerror(errno)); continue; }
        if (count) continue;
        printf("%s%d", first ? "" : delim, p->pid);
        if (listfull) printf(" %s", p->cmdline);
        else if (list) printf(" %s", cm);
        first = 0;
    }
    if (pick) {
        found = 1;
        if (kill_them) kill(pick->pid, sig);
        else if (!count) { printf("%d", pick->pid); if (listfull) printf(" %s", pick->cmdline); else if (list) { char cm[64]; comm_of(pick, cm, sizeof cm); printf(" %s", cm); } first = 0; }
    }
    if (count) printf("%d\n", found);
    else if (!first) printf("\n");
    return found ? 0 : 1;
}

static int pidof_main(int argc, char** argv) {
    int single = 0, first = 1, any = 0;
    if (load()) return 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-s")) { single = 1; continue; }
        for (int k = g_n - 1; k >= 0; k--) {
            char cm[64];
            comm_of(&g_p[k], cm, sizeof cm);
            const char* base = strrchr(argv[i], '/');
            if (strcmp(cm, base ? base + 1 : argv[i]) || g_p[k].state == 'Z') continue;
            printf("%s%d", first ? "" : " ", g_p[k].pid);
            first = 0;
            any = 1;
            if (single) break;
        }
    }
    if (any) printf("\n");
    return any ? 0 : 1;
}

int main(int argc, char** argv) {
    const char* me = strrchr(argv[0], '/');
    me = me ? me + 1 : argv[0];
    if (!strcmp(me, "pgrep")) return pgrep_main(argc, argv, 0);
    if (!strcmp(me, "pkill")) return pgrep_main(argc, argv, 1);
    if (!strcmp(me, "pidof")) return pidof_main(argc, argv);
    return ps_main(argc, argv);
}
