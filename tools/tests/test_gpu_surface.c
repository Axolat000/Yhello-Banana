/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Hosted run of the exact surface implementation used by fb.c. */
#include "../../kernel/gpu_surface.h"
extern int printf(const char*, ...);
static int failures;
static void report(const char* name, int ok, const char* detail) {
    printf("%s %s %s\n", ok ? "ok" : "FAIL", name, detail);
    failures += !ok;
}
int main(void) { gpu_surface_tests(report); return failures ? 1 : 0; }
