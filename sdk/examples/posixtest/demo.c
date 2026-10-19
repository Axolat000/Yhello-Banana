/* SPDX-License-Identifier: BSD-2-Clause */
/* libdemo: a shared library posixtest opens with dlopen. It uses the C
 * library and calls back into the app (both through the app's exports),
 * has data and a constructor. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int posixtest_from_app(int x);            /* (in posixtest.c) */

int demo_counter = 5;
static int g_ctor_ran;

__attribute__((constructor)) static void demo_init(void) {
    g_ctor_ran = 1;
    demo_counter += 100;
}

int demo_add(int a, int b) { return a + b; }
int demo_ctor_ran(void) { return g_ctor_ran; }
int demo_call_app(int x) { return posixtest_from_app(x) * 2; }

char* demo_format(int v) {
    char* s = malloc(32);
    if (s) snprintf(s, 32, "value=%d len=%d", v, (int)strlen("abc"));
    return s;
}
