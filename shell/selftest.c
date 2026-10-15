#include "selftest.h"
#include "../kernel/fs.h"
#include "../kernel/kstring.h"
#include "../kernel/terminal.h"
#include "../kernel/timer.h"
#include "../kernel/task.h"
#include "../kernel/app.h"
#include "../kernel/pkg.h"
#include "../kernel/hmem.h"
#include "../kernel/smp.h"

/*
 * selftest [ring3 | memory | limits | cores] - checks the system features
 * from inside Banana OS, one ok / FAIL line each, and a summary. What must
 * run as an app is done by systest (sdk/examples/systest), installed from
 * the copy built into the kernel: the exit code of each of its modes is
 * the result. (Filesystems: fstest.)
 */

extern const uint8_t ex_systest[], ex_systest_end[];

static int g_fail, g_ok;

static void report(const char* what, int ok, const char* note) {
    terminal_write_color(ok ? "  ok    " : "  FAIL  ", ok ? VGA_COLOR_LIGHT_GREEN : VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK);
    char line[160];
    ksnprintf(line, sizeof(line), "%s%s%s", what, note && note[0] ? " - " : "", note ? note : "");
    terminal_writeln(line);
    if (ok) g_ok++; else g_fail++;
}

static void skip(const char* what, const char* why) {
    terminal_write_color("  skip  ", VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    char line[160];
    ksnprintf(line, sizeof(line), "%s - %s", what, why);
    terminal_writeln(line);
}

static void section(const char* name) {
    terminal_write_color(name, VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK);
    terminal_putchar('\n');
}

/* systest from the kernel's own copy (once a boot): 0, or -1 */
static int install(void) {
    static int done;
    if (done) return 0;
    const char* tmp = "/tmp/systest.bpk";
    char msg[160];
    if (fs_write_path(tmp, ex_systest, (uint32_t)(ex_systest_end - ex_systest)) < 0 || pkg_install(tmp, msg, sizeof(msg)) != 0) {
        report("install systest", 0, msg);
        return -1;
    }
    fs_delete(tmp, 0);
    done = 1;
    return 0;
}

/* runs `systest a b c` in this shell: its exit code (-1 if it did not start) */
static int run(const char* a, const char* b, const char* c) {
    char* argv[4] = { "systest", (char*)a, (char*)b, (char*)c };
    int argc = c ? 4 : b ? 3 : 2;
    char err[96];
    int rc = pkg_run("systest", argc, argv, 0, err, sizeof(err));
    if (rc < 0 && err[0]) { terminal_write("  (systest: "); terminal_write(err); terminal_writeln(")"); }
    return rc;
}

static void t_ring3(void) {
    section("apps in ring 3 (build 141)");
#ifdef __x86_64__
    report("an app runs in user mode", run("ring", NULL, NULL) == 0, "");
#else
    skip("an app runs in user mode", "the 32-bit kernel keeps apps in ring 0");
#endif
}

static void t_memory(void) {
    section("memory above 4 GiB (build 142)");
    uint64_t total = hmem_total_bytes();
    char note[96];
    if (total < (64ull << 20)) {
        skip("RAM above 4 GiB", "none here (QEMU: MEM=8G tools/dev.sh run)");
        report("an app gets 64 MiB and every byte holds", run("mem", "64", NULL) == 0, "");
        return;
    }
    ksnprintf(note, sizeof(note), "%u MiB found", (uint32_t)(total >> 20));
    report("RAM above 4 GiB", 1, note);
    uint64_t before = hmem_used_bytes();
    report("an app's 64 MiB come from above 4 GiB", run("mem", "64", "high") == 0, "");
    ksnprintf(note, sizeof(note), "%u MiB in use before, %u after", (uint32_t)(before >> 20), (uint32_t)(hmem_used_bytes() >> 20));
    report("it is given back when the app ends", hmem_used_bytes() <= before, note);
}

static void t_limits(void) {
    section("higher limits (build 143)");
    report("an app with 60 threads at once", run("threads", "60", NULL) == 0, "");
    report("an app with 60 files open at once", run("files", "60", NULL) == 0, "");
    /* 24 apps at once (8 used to be the most) */
    char prog[FS_PATH_LEN], err[96];
    ksnprintf(prog, sizeof(prog), "%s/systest/app-%s", PKG_DIR, BANANA_ARCH);
    char* argv[3] = { "systest", "idle", "3000" };
    int base = app_count(), started = 0;
    for (int i = 0; i < 24; i++) if (app_spawn(prog, 3, argv, err, sizeof(err)) == 0) started++;
    int peak = app_count() - base;
    uint32_t t0 = timer_ms();
    while (app_count() > base && timer_ms() - t0 < 30000) task_sleep_ms(100);
    char note[96];
    ksnprintf(note, sizeof(note), "%d started, %d running together, %s", started, peak,
              app_count() > base ? "some never ended" : "all ended");
    report("24 apps at once", started == 24 && peak >= 24 && app_count() == base, note);
}

static void t_cores(void) {
    section("system calls on every core (build 144)");
    int n = cpu_count();
    int rc = run("cores", NULL, NULL);
    char note[96];
    if (rc < 100) { report("threads calling malloc / free / a lock", 0, "wrong totals or a crash"); return; }
    int cores = rc - 100;
    ksnprintf(note, sizeof(note), "on %d of %d core%s", cores, n, n == 1 ? "" : "s");
    report("threads calling malloc / free / a lock", 1, note);
    if (n < 2) skip("they spread over the cores", "one core here (QEMU: SMP=4)");
    else report("they spread over the cores", cores >= 2, cores >= 2 ? "" : "all stayed on one core");
}

int selftest_cmd(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : NULL;
    if (only && strcmp(only, "ring3") && strcmp(only, "memory") && strcmp(only, "limits") && strcmp(only, "cores")) {
        terminal_writeln("usage: selftest [ring3 | memory | limits | cores]   (filesystems: fstest <mount point>)");
        return 1;
    }
    g_ok = g_fail = 0;
    uint32_t t0 = timer_ms();
    terminal_writeln("selftest: checking the system (it runs the systest app a few times)");
    if (install() == 0) {
        if (!only || !strcmp(only, "ring3")) t_ring3();
        if (!only || !strcmp(only, "memory")) t_memory();
        if (!only || !strcmp(only, "limits")) t_limits();
        if (!only || !strcmp(only, "cores")) t_cores();
    }
    char line[96];
    ksnprintf(line, sizeof(line), "selftest: %d passed, %d failed (%u ms)", g_ok, g_fail, timer_ms() - t0);
    terminal_write_color(line, g_fail ? VGA_COLOR_LIGHT_RED : VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_putchar('\n');
    return 1;
}
