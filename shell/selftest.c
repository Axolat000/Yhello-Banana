#include "selftest.h"
#include "../kernel/bootmenu_test.h"
#include "../kernel/bootmem_test.h"
#include "../kernel/acpi_sleep.h"
#include "../kernel/sysinfo.h"
#include "../kernel/kheap.h"
#include "../kernel/mouse_capture.h"
#include "../kernel/gui.h"
#include "../kernel/browser_test.h"
#include "ipv6test.h"
#include "../net/ipv6.h"
#include "../kernel/fs.h"
#include "../kernel/kstring.h"
#include "../kernel/terminal.h"
#include "../kernel/timer.h"
#include "../kernel/task.h"
#include "../kernel/app.h"
#include "../kernel/pkg.h"
#include "../kernel/hmem.h"
#include "../kernel/smp.h"
#include "../kernel/zip.h"
#include "../kernel/clipboard.h"
#include "../kernel/builtin_apps.h"
#include "../kernel/zswap.h"
#include "../kernel/deflate.h"
#include "../kernel/tz.h"
#include "../kernel/keyboard.h"
#include "../kernel/rtc.h"
#include "datecmd.h"

/*
 * selftest [ring3 | memory | limits | cores] - checks the system features
 * from inside Banana OS, one ok / FAIL line each, and a summary. What must
 * run as an app is done by systest (sdk/examples/systest), installed from
 * the copy built into the kernel: the exit code of each of its modes is
 * the result. (Filesystems: fstest.)
 */

/* a built-in example package (kernel/builtin_apps.c: in RAM on the live CD,
 * a file on the disk once installed) written to path: 0, or -1 */
static int write_ex(const char* path, const char* file) {
    char* d;
    uint32_t n;
    if (builtin_package(file, &d, &n) != 0) return -1;
    int rc = fs_write_path(path, d, n) < 0 ? -1 : 0;
    kfree(d);
    return rc;
}

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
    if (write_ex(tmp, "systest.bpk") < 0 || pkg_install(tmp, msg, sizeof(msg)) != 0) {
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

static void t_malloc(void) {
    section("malloc in the app (build 190)");
    int rc = run("malloc", NULL, NULL);
    if (rc < 100) { report("random blocks, realloc and alignment", 0, "a block changed, out of memory or a crash"); return; }
    report("random blocks, realloc and alignment", 1, "");
#ifdef __x86_64__
    char note[96];
    ksnprintf(note, sizeof(note), "%dx as fast as the kernel's allocator (times above)", rc - 100);
    report("faster than a system call per malloc", rc - 100 >= 2, note);
#else
    skip("faster than a system call per malloc", "the 32-bit kernel keeps the kernel's allocator");
#endif
}

/* Install the matching built-in test, even if an older package is on disk. */
static void t_opencl(void) {
    section("OpenCL barriers, shared memory and vectors");
    const char* tmp = "/tmp/cltest.bpk";
    char msg[160] = { 0 };
    if (write_ex(tmp, "cltest.bpk") < 0 ||
        pkg_install(tmp, msg, sizeof(msg)) != 0) {
        fs_delete(tmp, 0);
        report("install cltest", 0, msg);
        return;
    }
    fs_delete(tmp, 0);
    char* args[] = { "cltest" };
    int rc = pkg_run("cltest", 1, args, 0, msg, sizeof(msg));
    report("OpenCL kernel tests", rc == 0, rc < 0 ? msg : "see individual checks above");
}

/* posixtest (the POSIX layer for ported programs), from the kernel's copy */
static void t_posix(const char* part) {
    section("the POSIX layer: mmap (162), pthreads (163), sockets (164), signals (165), dlopen (166), epoll (172)");
    const char* tmp = "/tmp/posixtest.bpk";
    char msg[160] = { 0 };
    if (write_ex(tmp, "posixtest.bpk") < 0 ||
        pkg_install(tmp, msg, sizeof(msg)) != 0) {
        fs_delete(tmp, 0);
        report("install posixtest", 0, msg);
        return;
    }
    fs_delete(tmp, 0);
    char* args[] = { "posixtest", (char*)part };
    int rc = pkg_run("posixtest", part ? 2 : 1, args, 0, msg, sizeof(msg));
    report("POSIX checks", rc == 0, rc < 0 ? msg : "see the checks above");
}

static void mouse_demo(void) {
    if(!gui_is_enabled()) { report("mouse capture demo",0,"startx first, then selftest mouse demo in a terminal");return; }
    const char* tmp="/tmp/mousetest.bpk";char msg[160]={0};
    if(write_ex(tmp, "mousetest.bpk") < 0 || pkg_install(tmp,msg,sizeof(msg))) {
        fs_delete(tmp,0);report("install mouse demo",0,msg);return;
    }
    fs_delete(tmp,0);char* args[]={"mousetest"};
    int rc=pkg_run("mousetest",1,args,1,msg,sizeof(msg));
    report("mouse demo launched",rc>=0,rc<0?msg:"click to look around; Escape releases; Q quits (interactive check)");
}

static void t_ram(void) {
    section("firmware RAM reporting (build 169)");
    bootmem_tests_run(report);
    uint32_t kb=sysinfo_get()->mem_kb;
    uint64_t managed=(hmem_total_bytes()>>10)+(kheap_total_bytes()>>10);
    char note[128];
    ksnprintf(note,sizeof(note),"%u MiB usable RAM; %llu MiB above 4 GiB managed for apps",
              kb/1024u,hmem_total_bytes()>>20);
    report("live firmware total covers the managed RAM",kb>0 && kb>=managed,note);
}
static void sleep_unlink(const char* path) { if(fs_find_file(path)>=0)fs_delete(path,0); }
static void t_sleep(int cycle) {
    section("ACPI S3 sleep (QEMU PC)");acpi_sleep_tests(report);
    char reason[160];int available=!acpi_s3_status(reason,sizeof(reason));
    if(!available){skip("ACPI S3 capability",reason);if(cycle)report("S3 sleep/resume cycle",0,reason);return;}
    report("ACPI S3 capability",1,reason);
    if(cycle) {
        unsigned before=acpi_s3_resumes();
        char worker[64],ready[80],stop[80],err[96]="test app did not become ready";
        ksnprintf(worker,sizeof(worker),"/tmp/s3-worker-%d",task_current_pid());
        ksnprintf(ready,sizeof(ready),"%s.ready",worker);ksnprintf(stop,sizeof(stop),"%s.stop",worker);
        sleep_unlink(worker);sleep_unlink(ready);sleep_unlink(stop);
        int started=0;
        if(!install()) {
            char* argv[]={"systest","suspend",worker};
            if(app_spawn("/apps/systest/app-" BANANA_ARCH,3,argv,err,sizeof(err))>=0) {
                uint32_t t=timer_ms();
                while(fs_find_file(ready)<0 && timer_ms()-t<3000)task_sleep_ms(10);
                int f=fs_find_file(ready);char text[8]={0};
                started=f>=0 && fs_read(f,0,text,5)==5 && !memcmp(text,"ready",5);
            }
        }
        report("user threads are live before suspend",started,started?"":err);
        volatile uint32_t marker[64];
        for(unsigned i=0;i<64;i++)marker[i]=i*2654435761u^0x53335241u;
        terminal_writeln("Sleeping for five seconds; the RTC will wake this VM...");
        int rc=acpi_s3_suspend(5,reason,sizeof(reason));
        report("S3 sleep/resume cycle",!rc && acpi_s3_resumes()==before+1,reason);
        int intact=1;for(unsigned i=0;i<64;i++)if(marker[i]!=(i*2654435761u^0x53335241u))intact=0;
        report("memory on the suspended stack survives",intact,"");
        uint32_t ticks=timer_irq_count(),ms=timer_ms();timer_sleep_ms(20);
        report("timer interrupts and monotonic time resume",timer_irq_count()!=ticks && timer_ms()-ms>=20,"");
        fs_write_path(stop,"stop",4);uint32_t until=timer_ms();
        while(fs_find_file(worker)<0 && timer_ms()-until<5000)task_sleep_ms(10);
        int f=fs_find_file(worker);char text[8]={0};
        report("user threads retain their stacks and floating-point state",started && f>=0 && fs_read(f,0,text,2)==2 && !memcmp(text,"ok",2),"");
        sleep_unlink(worker);sleep_unlink(ready);sleep_unlink(stop);
    }
}

/* ── ZIP archives (kernel/zip.c, kernel/deflate.c) ─────────────────── */

static int same_file(const char* a, const char* b) {
    int ia = fs_find_file(a), ib = fs_find_file(b);
    if (ia < 0 || ib < 0) return 0;
    fs_file_t* fa = fs_get_file(ia);
    uint32_t na = fa ? fa->size : 0;
    char* copy = (char*)kmalloc(na ? na : 1);
    if (!copy) return 0;
    if (na) memcpy(copy, fa->content, na);
    fs_file_t* fb = fs_get_file(ib);
    int same = fb && fb->size == na && (!na || memcmp(copy, fb->content, na) == 0);
    kfree(copy);
    return same;
}

/* a stored archive of one entry named name, by hand (names zip_create would never write) */
static uint32_t raw_zip(uint8_t* z, const char* name, const char* data) {
    uint32_t nl = (uint32_t)strlen(name), dl = (uint32_t)strlen(data), crc = crc32_update(0, data, dl), p = 0;
    uint8_t h[46];
#define P16(o, v) (h[o] = (uint8_t)(v), h[(o) + 1] = (uint8_t)((v) >> 8))
#define P32(o, v) (P16(o, (v) & 0xFFFF), P16((o) + 2, (v) >> 16))
    memset(h, 0, 46); P32(0, 0x04034b50u); P16(4, 20); P32(14, crc); P32(18, dl); P32(22, dl); P16(26, nl);
    memcpy(z + p, h, 30); p += 30; memcpy(z + p, name, nl); p += nl; memcpy(z + p, data, dl); p += dl;
    uint32_t cd = p;
    memset(h, 0, 46); P32(0, 0x02014b50u); P16(4, 20); P16(6, 20); P32(16, crc); P32(20, dl); P32(24, dl); P16(28, nl);
    memcpy(z + p, h, 46); p += 46; memcpy(z + p, name, nl); p += nl;
    memset(h, 0, 46); P32(0, 0x06054b50u); P16(8, 1); P16(10, 1); P32(12, p - cd); P32(16, cd);
    memcpy(z + p, h, 22); p += 22;
#undef P16
#undef P32
    return p;
}

static void t_zip(void) {
    section("ZIP archives: zip / unzip, Files' Extract and Compress");
    if (fs_find_dir("/tmp/zt") >= 0) fs_delete("/tmp/zt", 1);
    fs_mkdir_p("/tmp/zt/src/sub/deep");
    /* text (compresses well), pseudo-random bytes (does not), an empty file */
    uint32_t tn = 60000, bn = 20000;
    char* t = (char*)kmalloc(tn);
    uint8_t* b = (uint8_t*)kmalloc(bn);
    if (!t || !b) { kfree(t); kfree(b); report("ZIP checks", 0, "out of memory"); return; }
    for (uint32_t i = 0; i < tn; i++) t[i] = "the quick brown banana jumps over the lazy kernel\n"[i % 50];
    uint32_t r = 12345;
    for (uint32_t i = 0; i < bn; i++) { r = r * 1103515245u + 12345u; b[i] = (uint8_t)(r >> 16); }
    fs_write_path("/tmp/zt/src/a.txt", t, tn);
    fs_write_path("/tmp/zt/src/b.bin", b, bn);
    fs_write_path("/tmp/zt/src/empty.txt", "", 0);
    fs_write_path("/tmp/zt/src/sub/c.txt", "hello from a folder\n", 20);
    fs_write_path("/tmp/zt/src/sub/deep/d.txt", t, 777);
    kfree(t);
    kfree(b);

    char err[FS_PATH_LEN + 80], note[160];
    zip_stats_t st;
    const char* in[] = { "/tmp/zt/src" };
    int rc = zip_create("/tmp/zt/t.zip", in, 1, 0, NULL, NULL, &st, err, sizeof(err));
    ksnprintf(note, sizeof(note), "%d files, %d folders, %u -> %u bytes%s%s", st.files, st.dirs, st.bytes, st.zbytes, rc ? ": " : "", rc ? err : "");
    report("zip a folder (5 files, 3 folders), compressed", rc == 0 && st.files == 5 && st.dirs == 3 && st.zbytes < st.bytes / 2, note);
    int n = zip_list("/tmp/zt/t.zip", NULL, NULL, err, sizeof(err));
    ksnprintf(note, sizeof(note), "%d entries", n);
    report("list it", n == 8, note);
    rc = zip_extract("/tmp/zt/t.zip", "/tmp/zt/out", 0, NULL, NULL, &st, err, sizeof(err));
    int same = same_file("/tmp/zt/src/a.txt", "/tmp/zt/out/src/a.txt") && same_file("/tmp/zt/src/b.bin", "/tmp/zt/out/src/b.bin") &&
               same_file("/tmp/zt/src/empty.txt", "/tmp/zt/out/src/empty.txt") && same_file("/tmp/zt/src/sub/c.txt", "/tmp/zt/out/src/sub/c.txt") &&
               same_file("/tmp/zt/src/sub/deep/d.txt", "/tmp/zt/out/src/sub/deep/d.txt");
    ksnprintf(note, sizeof(note), "%d files, %d failed%s%s", st.files, st.failed, err[0] ? ": " : "", err);
    report("extract it: every file the same, byte for byte", rc == 0 && st.files == 5 && !st.failed && same, note);
    rc = zip_extract("/tmp/zt/t.zip", "/tmp/zt/out", 0, NULL, NULL, &st, err, sizeof(err));
    int kept = st.skipped == 5 && st.files == 0;
    rc |= zip_extract("/tmp/zt/t.zip", "/tmp/zt/out", ZIP_OVERWRITE, NULL, NULL, &st, err, sizeof(err));
    report("again: files there are kept, or replaced with -o", rc == 0 && kept && st.files == 5, "");

    /* names that would land outside the folder */
    uint8_t* z = (uint8_t*)kmalloc(512);
    if (z) {
        uint32_t zl = raw_zip(z, "../evil.txt", "pwned");
        fs_write_path("/tmp/zt/evil1.zip", z, zl);
        zl = raw_zip(z, "/tmp/zt/abs.txt", "pwned");
        fs_write_path("/tmp/zt/evil2.zip", z, zl);
        zip_stats_t s1, s2;
        zip_extract("/tmp/zt/evil1.zip", "/tmp/zt/x/y", 0, NULL, NULL, &s1, err, sizeof(err));
        zip_extract("/tmp/zt/evil2.zip", "/tmp/zt/x/y", 0, NULL, NULL, &s2, err, sizeof(err));
        report("\"../\" and absolute names are refused", s1.skipped == 1 && s2.skipped == 1 && fs_find_file("/tmp/zt/x/evil.txt") < 0 &&
               fs_find_file("/tmp/zt/abs.txt") < 0 && fs_find_file("/tmp/zt/evil.txt") < 0, "");
        zl = raw_zip(z, "ok.txt", "fine data");
        fs_write_path("/tmp/zt/good.zip", z, zl);
        zip_extract("/tmp/zt/good.zip", "/tmp/zt/g", 0, NULL, NULL, &s1, err, sizeof(err));
        z[30 + 6] ^= 0x20;                                  /* "fine data" -> "Fine data": the CRC no longer matches */
        fs_write_path("/tmp/zt/bad.zip", z, zl);
        zip_extract("/tmp/zt/bad.zip", "/tmp/zt/b", 0, NULL, NULL, &s2, err, sizeof(err));
        report("a damaged file is caught by its CRC", s1.files == 1 && s2.failed == 1 && fs_find_file("/tmp/zt/b/ok.txt") < 0, err);
        kfree(z);
    }
    fs_write_path("/tmp/zt/not.zip", "just some text, not an archive", 30);
    report("a file that is not a ZIP is refused", zip_list("/tmp/zt/not.zip", NULL, NULL, err, sizeof(err)) < 0, err);
    fs_delete("/tmp/zt", 1);
}

/* ── the clipboard history (Win+V) ──────────────────────────────────── */

/* the entry holding s: its index, or -1 */
static int clip_find(const char* s) {
    char b[64];
    for (int i = 0; i < clipboard_hist_count(); i++)
        if (clipboard_hist_peek(i, b, sizeof(b), NULL, NULL) == 0 && !strcmp(b, s)) return i;
    return -1;
}

/* ── compressed memory: swap in RAM (kernel/zswap.c) ───────────────── */

#ifdef __x86_64__
static volatile uint32_t g_sw_pages, g_sw_done, g_sw_disk;
static volatile uint64_t g_sw_before, g_sw_after;

static uint64_t ram_used(void) { return (uint64_t)kheap_used_bytes() + hmem_used_bytes(); }

/* while systest waits: its pages compressed, and how much RAM that gave back */
static void swap_helper(void) {
    task_sleep_ms(2500);
    g_sw_before = ram_used();
    g_sw_pages = zswap_compress_now();
    g_sw_after = ram_used();
    g_sw_disk = zswap_disk_bytes() ? zswap_writeback(0xFFFFFFFFu) : 0;   /* (installed: on to the disk) */
    g_sw_done = 1;
}
#endif

/* each layout typed key by key: letters, Shift, AltGr, the dead accents */
#define KS 0x2A                     /* Shift down / up */
#define KSU 0xAA
#define KG 0xE0, 0x38               /* AltGr down / up */
#define KGU 0xE0, 0xB8
#define KEND 0xFF
static void t_keyboard(void) {
    section("keyboard layouts");
    static const struct { const char* layout; uint8_t k[24]; const char* want; } C[] = {
        { "EN (Default)", { 0x10, 0x11, 0x1A, KS, 0x03, KSU, KEND }, "qw[@" },
        { "FR", { 0x10, 0x11, 0x02, 0x03, 0x0B, 0x32, 0x33, 0x34, 0x35, 0x28, KEND }, "az&\xC3\xA9\xC3\xA0,;:!\xC3\xB9" },
        { "FR", { KS, 0x02, 0x03, 0x0A, 0x0B, KSU, KG, 0x0B, 0x12, 0x06, KGU, KEND }, "1290@\xE2\x82\xAC[" },
        { "FR", { 0x1A, 0x12, KS, 0x1A, KSU, 0x17, 0x1A, 0x39, KEND }, "\xC3\xAA\xC3\xAF^" },
        { "BE", { 0x07, 0x09, 0x35, KG, 0x03, KGU, 0x10, KEND }, "\xC2\xA7!=@a" },
        { "DE", { 0x15, 0x2C, 0x0C, 0x27, 0x28, 0x1A, KS, 0x04, KSU, KG, 0x10, KGU, KEND }, "zy\xC3\x9F\xC3\xB6\xC3\xA4\xC3\xBC\xC2\xA7@" },
        { "DE", { 0x0D, 0x12, KS, 0x0D, KSU, 0x1E, KEND }, "\xC3\xA9\xC3\xA0" },
        { "UK", { KS, 0x03, 0x04, 0x28, KSU, 0x2B, 0x56, KEND }, "\"\xC2\xA3@#\\" },
        { "US-Intl", { 0x28, 0x12, 0x28, 0x39, 0x28, 0x2E, KS, 0x07, KSU, 0x18, KEND }, "\xC3\xA9'\xC3\xA7\xC3\xB4" },
        { "ES", { 0x27, 0x28, 0x1E, 0x0D, KS, 0x0D, KSU, KEND }, "\xC3\xB1\xC3\xA1\xC2\xA1\xC2\xBF" },
        { "IT", { 0x1A, KS, 0x1A, KSU, KG, 0x27, KGU, 0x2B, KEND }, "\xC3\xA8\xC3\xA9@\xC3\xB9" },
        { "PT", { 0x27, 0x2B, 0x1E, 0x0D, KEND }, "\xC3\xA7\xC3\xA3\xC2\xAB" },
        { "fr_CH", { 0x15, 0x1A, KS, 0x1A, KSU, KG, 0x03, KGU, KEND }, "z\xC3\xA8\xC3\xBC@" },
        { "de_CH", { 0x1A, 0x27, 0x28, KEND }, "\xC3\xBC\xC3\xB6\xC3\xA4" },
        { "BEPO", { 0x10, 0x11, 0x12, 0x13, 0x1E, 0x1F, 0x20, 0x21, KG, 0x1E, KGU, KEND }, "b\xC3\xA9poauie\xC3\xA6" },
        { "FR", { 0x1D, 0x10, 0x9D, KEND }, "\x01" },                    /* Ctrl+A: by the letter on the key */
    };
    int n = (int)(sizeof(C) / sizeof(C[0])), bad = 0;
    char note[160] = "";
    for (int i = 0; i < n; i++) {
        int len = 0;
        while (len < 24 && C[i].k[len] != KEND) len++;
        char got[64];
        int rc = keyboard_test_type(C[i].layout, C[i].k, len, got, sizeof(got));
        if ((rc != 0 || strcmp(got, C[i].want)) && !bad++)
            ksnprintf(note, sizeof(note), "%s: got \"%s\", want \"%s\"", C[i].layout, rc ? "(no such layout)" : got, C[i].want);
    }
    if (!bad) ksnprintf(note, sizeof(note), "%d sequences on %d layouts", n, keyboard_layout_count());
    report("every layout types what its keys show", bad == 0, note);
    int all = keyboard_layout_count() >= 12;
    for (int i = 0; i < keyboard_layout_count(); i++) {
        const char* name = keyboard_layout_at(i, NULL);
        char got[8];
        if (keyboard_test_type(name, (const uint8_t[]){ 0x39 }, 1, got, sizeof(got)) != 0 || strcmp(got, " ")) all = 0;
    }
    report("each layout can be chosen by its name", all, keyboard_layouts_help());
}

/* the time zones' rules on known dates, local <-> UTC, the clock set by hand */
static void t_time(void) {
    section("time zones and the clock");
    char note[128];
    struct { const char* zone; int y, mo, d, h, mi, s; int want; } C[] = {
        { "Europe/Paris",      2026, 3, 29, 0, 59, 59, 3600 },   /* (UTC) the last second of winter */
        { "Europe/Paris",      2026, 3, 29, 1, 0, 0, 7200 },     /* summer from 01:00 UTC */
        { "Europe/Paris",      2026, 10, 25, 0, 59, 59, 7200 },
        { "Europe/Paris",      2026, 10, 25, 1, 0, 0, 3600 },
        { "America/New_York",  2026, 3, 8, 6, 59, 59, -18000 },  /* 02:00 EST */
        { "America/New_York",  2026, 3, 8, 7, 0, 0, -14400 },
        { "America/New_York",  2026, 11, 1, 5, 59, 59, -14400 }, /* 02:00 EDT */
        { "America/New_York",  2026, 11, 1, 6, 0, 0, -18000 },
        { "Australia/Sydney",  2026, 4, 4, 15, 59, 59, 39600 },  /* 03:00 AEDT, 5 April */
        { "Australia/Sydney",  2026, 4, 4, 16, 0, 0, 36000 },
        { "Australia/Sydney",  2026, 10, 3, 16, 0, 0, 39600 },   /* 02:00 AEST, 4 October */
        { "Pacific/Auckland",  2026, 9, 26, 14, 0, 0, 46800 },   /* 02:00 NZST, 27 September */
        { "America/Santiago",  2026, 7, 1, 12, 0, 0, -14400 },
        { "America/Santiago",  2026, 12, 1, 12, 0, 0, -10800 },
        { "Asia/Kolkata",      2026, 7, 1, 12, 0, 0, 19800 },
        { "Asia/Kathmandu",    2026, 1, 1, 12, 0, 0, 20700 },
    };
    int bad = 0, n = (int)(sizeof(C) / sizeof(C[0]));
    for (int k = 0; k < n; k++) {
        int z = tz_find(C[k].zone);
        int got = z < 0 ? -1 : tz_offset_at(z, rtc_make_time(C[k].y, C[k].mo, C[k].d, C[k].h, C[k].mi, C[k].s));
        if (got != C[k].want && !bad++)
            ksnprintf(note, sizeof(note), "%s %04d-%02d-%02d %02d:%02d: %d s, want %d", C[k].zone, C[k].y, C[k].mo, C[k].d, C[k].h, C[k].mi, got, C[k].want);
    }
    if (!bad) ksnprintf(note, sizeof(note), "%d instants in %d zones", n, 7);
    report("summer time starts and ends on the right second", bad == 0, note);

    int was = tz_use(tz_find("Europe/Paris"));
    uint32_t a = tz_to_utc(rtc_make_time(2026, 7, 1, 12, 0, 0));
    uint32_t g = tz_to_utc(rtc_make_time(2026, 3, 29, 2, 30, 0));     /* (does not exist: read as winter) */
    uint32_t o = tz_to_utc(rtc_make_time(2026, 10, 25, 2, 30, 0));    /* (twice: the first, summer's) */
    int ok = a == rtc_make_time(2026, 7, 1, 10, 0, 0) && g == rtc_make_time(2026, 3, 29, 1, 30, 0) &&
             o == rtc_make_time(2026, 10, 25, 0, 30, 0) && tz_to_local(a) == rtc_make_time(2026, 7, 1, 12, 0, 0);
    report("local time to UTC and back (summer, the skipped and the repeated hour)", ok, "");
    uint32_t u = rtc_now_utc(), l = rtc_now();
    int d = (int)(l - u) - tz_offset_at(tz_current(), u);
    ksnprintf(note, sizeof(note), "local %u, UTC %u", l, u);
    report("the clock: local time = UTC + the zone's offset", u && d >= -1 && d <= 1, note);

    /* by hand: a day on, then back (the clock chip itself is written) */
    uint32_t t0 = rtc_now_utc();
    int set = rtc_set_utc(t0 + 86400u);
    uint32_t t1 = rtc_now_utc();
    rtc_set_utc(t0 + (t1 - t0 - 86400u));
    ksnprintf(note, sizeof(note), "%d s on", (int)(t1 - t0));
    report("the clock set by hand, and back", set == 0 && t1 - t0 >= 86400u && t1 - t0 <= 86401u, note);
    char err[96];
    int r1 = date_set_text("30/02/2026", NULL, err, sizeof(err)), r2 = date_set_text(NULL, "25:00", err, sizeof(err));
    report("dates and times that do not exist are refused", r1 != 0 && r2 != 0, "");
    tz_use(was);
}

static void t_swap(void) {
    section("compressed memory: swap in RAM (build 182)");
#ifndef __x86_64__
    skip("compressed memory", "the 64-bit kernel's apps only (their mmap memory)");
#else
    zswap_stats_t a, b;
    zswap_stats(&a);
    g_sw_done = 0;
    task_create("swaptest", swap_helper);
    int rc = run("swap", "32", "5000");
    while (!g_sw_done) task_sleep_ms(50);
    zswap_stats(&b);
    char note[128];
    ksnprintf(note, sizeof(note), "%u pages of 8192 compressed", g_sw_pages);
    report("an app's idle pages compressed", g_sw_pages >= 6000, note);
    uint64_t back = g_sw_before > g_sw_after ? g_sw_before - g_sw_after : 0;
    ksnprintf(note, sizeof(note), "%u KiB of RAM given back", (uint32_t)(back >> 10));
    report("their RAM goes back to the system", back >= (16u << 20), note);
    ksnprintf(note, sizeof(note), "%u pages brought back", (uint32_t)(b.pages_in - a.pages_in));
    report("touched again: decompressed, every byte right", rc == 0 && b.pages_in - a.pages_in >= 6000, note);
    report("nothing left compressed once it unmapped", b.pages == a.pages, "");
    if (!zswap_disk_bytes()) skip("the swap area on the disk", "none on the live CD (an installed system has one)");
    else {
        ksnprintf(note, sizeof(note), "%u pages written, %u read back", g_sw_disk, (uint32_t)(b.disk_in - a.disk_in));
        report("compressed pages to the disk and back, every byte right", rc == 0 && g_sw_disk >= 3000 && b.disk_in - a.disk_in >= 3000, note);
        report("nothing left on the disk once it unmapped", b.disk_pages == a.disk_pages && b.disk_used == a.disk_used, "");
    }
#endif
}

static void t_clip(void) {
    section("clipboard history (Win+V)");
    /* (the user's own entries and clipboard are left as they were) */
    uint32_t keep_len = 0;
    const char* cur = clipboard_get(&keep_len);
    char* keep = (char*)kmalloc(keep_len + 1);
    if (keep) memcpy(keep, cur, keep_len);
    static const char* const T[3] = { "selftest clip one", "selftest clip two", "selftest clip three" };
    int before = clipboard_hist_count();
    for (int i = 0; i < 3; i++) clipboard_set(T[i], (uint32_t)strlen(T[i]));
    report("copies go to the top, the newest first", clip_find(T[2]) == 0 && clip_find(T[1]) == 1 && clip_find(T[0]) == 2, "");
    int n = clipboard_hist_count();
    clipboard_set(T[0], (uint32_t)strlen(T[0]));
    report("copying one again moves it up (no double)", clip_find(T[0]) == 0 && clipboard_hist_count() == n, "");
    char note[64];
    ksnprintf(note, sizeof(note), "%d entries before, %d now (25 at most)", before, clipboard_hist_count());
    report("the history keeps them", clipboard_hist_count() >= 3 && clipboard_hist_count() <= CLIP_HIST_MAX, note);
    int p = 0, i1 = clip_find(T[1]);
    clipboard_hist_pin(i1, 1);
    clipboard_hist_peek(clip_find(T[1]), NULL, 0, NULL, &p);
    int saved = fs_find_file("/etc/clipboard-pinned") >= 0;
    clipboard_hist_pin(clip_find(T[1]), 0);
    report("an entry can be pinned (and is saved)", p == 1 && saved, "");
    uint32_t len = 0;
    clipboard_hist_use(clip_find(T[2]));
    const char* c = clipboard_get(&len);
    report("choosing one makes it the clipboard", len == strlen(T[2]) && !memcmp(c, T[2], len) && clip_find(T[2]) == 0, "");
    for (int i = 0; i < 3; i++) clipboard_hist_delete(clip_find(T[i]));
    report("entries can be deleted", clip_find(T[0]) < 0 && clip_find(T[1]) < 0 && clip_find(T[2]) < 0, "");
    if (keep) {
        clipboard_set(keep, keep_len);                   /* the user's clipboard back */
        kfree(keep);
    }
}

int selftest_cmd(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : NULL;
    if (only && strcmp(only, "ring3") && strcmp(only, "memory") && strcmp(only, "ram") && strcmp(only, "sleep") && strcmp(only, "limits") && strcmp(only, "cores") && strcmp(only, "malloc") && strcmp(only, "ipv6") && strcmp(only, "opencl") && strcmp(only, "posix") && strcmp(only, "browser") && strcmp(only, "mouse") && strcmp(only, "boot") && strcmp(only, "zip") && strcmp(only, "clipboard") && strcmp(only, "swap") && strcmp(only, "time") && strcmp(only, "keyboard")) {
        terminal_writeln("usage: selftest [ring3 | memory | ram | sleep [cycle] | limits | cores | malloc | opencl | browser [swm] | mouse [demo] | boot | posix [part] | zip | clipboard | swap | time | keyboard | ipv6 [host [port]]]   (filesystems: fstest <mount point>)");
        return 1;
    }
    if(only && !strcmp(only,"mouse") && (argc>3 || (argc==3 && strcmp(argv[2],"demo")))) {
        terminal_writeln("usage: selftest mouse [demo]");return 1;
    }
    if(only && !strcmp(only,"boot") && argc!=2) { terminal_writeln("usage: selftest boot");return 1; }
    if(only && !strcmp(only,"ram") && argc!=2) { terminal_writeln("usage: selftest ram");return 1; }
    if(only && !strcmp(only,"sleep") && (argc>3 || (argc==3 && strcmp(argv[2],"cycle")))) { terminal_writeln("usage: selftest sleep [cycle]");return 1; }
    uint16_t ipv6_port = 80;
    if(only && !strcmp(only,"browser") && (argc>3 || (argc==3 && strcmp(argv[2],"swm")))) {
        terminal_writeln("usage: selftest browser [swm]   (swm: live https://swm.mxtpasontop.fr/)");return 1;
    }
    if (only && !strcmp(only, "ipv6") && argc > 3) {
        uint32_t n;
        int used = k_parse_u32(argv[3], &n);
        if (!used || argv[3][used] || !n || n > 65535 || argc > 4) {
            terminal_writeln("usage: selftest ipv6 [host [port]]"); return 1;
        }
        ipv6_port = (uint16_t)n;
    }
    g_ok = g_fail = 0;
    uint32_t t0 = timer_ms();
    terminal_writeln(only && !strcmp(only, "browser") ? (argc==3?"selftest: checking live swm.mxtpasontop.fr":"selftest: checking browser frameworks (offline)") :
                     only && !strcmp(only, "opencl") ? "selftest: checking OpenCL" :
                     only && !strcmp(only, "ipv6") ? "selftest: checking IPv6" :
                     only && !strcmp(only, "posix") ? "selftest: checking the POSIX layer" :
                     only && !strcmp(only, "ram") ? "selftest: checking RAM reporting" :
                     only && !strcmp(only, "zip") ? "selftest: checking ZIP archives" :
                     only && !strcmp(only, "clipboard") ? "selftest: checking the clipboard history" :
                     only && !strcmp(only, "swap") ? "selftest: checking compressed memory" :
                     only && !strcmp(only, "time") ? "selftest: checking time zones and the clock" :
                     only && !strcmp(only, "keyboard") ? "selftest: checking the keyboard layouts" :
                     only && !strcmp(only, "sleep") ? "selftest: checking ACPI S3" :
                     only && !strcmp(only, "malloc") ? "selftest: checking the apps' heap" :
                     "selftest: checking the system (it runs the systest app a few times)");
    if(only && !strcmp(only,"sleep")) {
        t_sleep(argc==3);
    } else if(only && !strcmp(only,"ram")) {
        t_ram();
    } else if (only && !strcmp(only, "zip")) {
        t_zip();
    } else if (only && !strcmp(only, "clipboard")) {
        t_clip();
    } else if (only && !strcmp(only, "keyboard")) {
        t_keyboard();
    } else if (only && !strcmp(only, "time")) {
        t_time();
    } else if (only && !strcmp(only, "swap")) {
        if (install() == 0) t_swap();
    } else if(only && !strcmp(only,"boot")) {
        section("graphical boot menu rendering");bootmenu_tests_run(report);
    } else if(only && !strcmp(only,"mouse")) {
        section("mouse capture and relative movement");mouse_capture_tests(report);
        if(argc==3)mouse_demo();
    } else if (only && !strcmp(only, "browser")) {
        if(argc==3) {
            section("SuperWebManager live website checks");
            browser_site_test_run(report);
        } else {
            section("Tailwind CSS, React, Vue, Alpine, Astro and DOM observers");
            browser_test_run(report);
        }
    } else if (only && !strcmp(only, "opencl")) {
        t_opencl();
    } else if (only && !strcmp(only, "posix")) {
        t_posix(argc > 2 ? argv[2] : NULL);
    } else if (only && !strcmp(only, "ipv6")) {
        section("IPv6 TCP, UDP and toggle checks");
        ipv6test_run(argc > 2 ? argv[2] : NULL, ipv6_port, report);
    } else if (install() == 0) {
        if (!only || !strcmp(only, "ring3")) t_ring3();
        if (!only || !strcmp(only, "memory")) t_memory();
        if (!only || !strcmp(only, "limits")) t_limits();
        if (!only || !strcmp(only, "cores")) t_cores();
        if (!only || !strcmp(only, "malloc")) t_malloc();
        if (!only) {
            t_opencl();
            browser_test_run(report);
            mouse_capture_tests(report);
            bootmenu_tests_run(report);
            t_ram();
            t_sleep(0);
            t_posix(NULL);
            t_zip();
            t_clip();
            t_swap();
            t_time();
            t_keyboard();
            if (ipv6_enabled()) ipv6test_run(NULL, 80, report);
            else skip("IPv6", "off; selftest ipv6 runs temporary local checks");
        }
    }
    char line[96];
    ksnprintf(line, sizeof(line), "selftest: %d passed, %d failed (%u ms)", g_ok, g_fail, timer_ms() - t0);
    terminal_write_color(line, g_fail ? VGA_COLOR_LIGHT_RED : VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    terminal_putchar('\n');
    return 1;
}
