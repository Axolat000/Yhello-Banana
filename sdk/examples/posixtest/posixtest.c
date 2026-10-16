/* posixtest - checks the POSIX layer apps get from the SDK, for programs
 * ported from Linux (on the way to OpenJDK): one ok / FAIL line per check,
 * the exit code is how many failed. `selftest posix` runs it.
 *
 *   posixtest          every part
 *   posixtest mmap     mmap / munmap / mprotect
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <banana.h>

static int g_ok, g_fail;

static void check(const char* what, int ok, const char* note) {
    printf("  %s  %s%s%s\n", ok ? "ok  " : "FAIL", what, note && note[0] ? " - " : "", note ? note : "");
    if (ok) g_ok++; else g_fail++;
}

/* ── mmap ─────────────────────────────────────────────────────────── */

static int all_zero(const char* p, size_t n) {
    for (size_t i = 0; i < n; i++) if (p[i]) return 0;
    return 1;
}

#define TOUCH (4u << 20)
static char* g_area;

/* each thread fills its own 4 MiB of a fresh mapping, page by page */
static int toucher(void* arg) {
    long k = (long)arg;
    unsigned* p = (unsigned*)(g_area + (size_t)k * TOUCH);
    for (size_t i = 0; i < TOUCH / 4; i += 1024) p[i] = (unsigned)(k * 1000003u + i);
    for (size_t i = 0; i < TOUCH / 4; i += 1024) if (p[i] != (unsigned)(k * 1000003u + i)) return 1;
    return 0;
}

static void t_mmap(void) {
    printf("mmap / munmap / mprotect\n");
    int big = sizeof(void*) == 8;
    size_t reserve = big ? (1ul << 30) : (16ul << 20);
    char note[96];
    char* r = mmap(NULL, reserve, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    snprintf(note, sizeof(note), "%lu MiB at %p", (unsigned long)(reserve >> 20), (void*)r);
    check("reserve addresses (PROT_NONE)", r != MAP_FAILED, note);
    if (r == MAP_FAILED) return;

    size_t use = 8u << 20;
    check("mprotect 8 MiB of it read / write", mprotect(r, use, PROT_READ | PROT_WRITE) == 0, "");
    check("its pages read as zeros", all_zero(r, use), "");
    for (size_t i = 0; i < use; i += 4096) *(unsigned*)(r + i) = (unsigned)i * 2654435761u;
    int same = 1;
    for (size_t i = 0; i < use; i += 4096) if (*(unsigned*)(r + i) != (unsigned)i * 2654435761u) same = 0;
    check("written and read back, every page", same, "");

    /* a hole punched and mapped again (MAP_FIXED): fresh zero pages */
    char* mid = r + (2u << 20);
    memset(mid, 0xAB, 1u << 20);
    check("munmap 1 MiB in the middle", munmap(mid, 1u << 20) == 0, "");
    char* again = mmap(mid, 1u << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    check("MAP_FIXED maps it again, zeroed", again == mid && all_zero(mid, 1u << 20), "");
    check("the pages around it kept their data", *(unsigned*)(r + (1u << 20)) == (1u << 20) * 2654435761u &&
          *(unsigned*)(r + (3u << 20)) == (3u << 20) * 2654435761u, "");

    if (big) {
        /* the far end of the reservation: only what is touched gets memory */
        char* end = r + reserve - (1u << 20);
        int ok = mprotect(end, 1u << 20, PROT_READ | PROT_WRITE) == 0;
        if (ok) { end[(1u << 20) - 1] = 42; ok = end[(1u << 20) - 1] == 42; }
        check("the last page of 1 GiB, touched alone", ok, "");
    }

    /* system calls given mapped memory: a file written from it, read back into it */
    int fd = open("/tmp/posixtest.dat", O_CREAT | O_TRUNC | O_RDWR);
    int io = fd >= 0 && write(fd, r, 100000) == 100000 && lseek(fd, 0, SEEK_SET) == 0;
    char* back = r + (5u << 20);
    io = io && read(fd, back, 100000) == 100000 && memcmp(back, r, 100000) == 0;
    check("write() and read() with a mapped buffer", io, "");

    /* a file mapped: its bytes, then zeros past its end */
    char* fm = fd >= 0 ? mmap(NULL, 200000, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    check("mmap a file", fm != MAP_FAILED && memcmp(fm, r, 100000) == 0 && all_zero(fm + 100000, 100000), "");
    if (fm != MAP_FAILED) munmap(fm, 200000);
    if (fd >= 0) close(fd);
    unlink("/tmp/posixtest.dat");

    /* several threads faulting pages in at once (on every core) */
    int n = banana_cpus() < 2 ? 2 : banana_cpus() > 8 ? 8 : banana_cpus();
    g_area = mmap(NULL, (size_t)n * TOUCH, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    int ids[8], bad = g_area == MAP_FAILED;
    for (int k = 0; k < n && !bad; k++) ids[k] = banana_thread(toucher, (void*)(long)k);
    for (int k = 0; k < n && !bad; k++) if (ids[k] < 0 || banana_join(ids[k]) != 0) bad = 1;
    snprintf(note, sizeof(note), "%d threads, %d MiB", n, n * (int)(TOUCH >> 20));
    check("threads touching new pages at once", !bad, note);
    if (g_area != MAP_FAILED) munmap(g_area, (size_t)n * TOUCH);

    check("munmap the whole reservation", munmap(r, reserve) == 0, "");
}

int main(int argc, char** argv) {
    const char* only = argc > 1 ? argv[1] : NULL;
    if (!only || !strcmp(only, "mmap")) t_mmap();
    printf("posixtest: %d passed, %d failed\n", g_ok, g_fail);
    return g_fail;
}
