/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The Terminal app's screen (apps/terminal/vt.c: xterm's escape sequences
 * turned into cells), built into the kernel for its consoles
 * (kernel/vcons.c) - the C library calls it makes, on the kernel's own. */
#define VT_KERNEL 1
#include "types.h"
#include "kheap.h"
#include "kstring.h"
#include "utf8.h"

static void* vt_calloc(size_t n, size_t sz) { size_t b = n * sz; return kzalloc(b ? b : 1); }
static int vt_atoi(const char* s) {
    int n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n;
}

#define malloc(n)        kmalloc(n)
#define calloc(n, s)     vt_calloc(n, s)
#define realloc(p, n)    krealloc(p, n)
#define free(p)          kfree(p)
#define snprintf         ksnprintf
#define sprintf(b, ...)  ksnprintf(b, 32, __VA_ARGS__)    /* (vt_key_seq / vt_mouse_seq: short sequences) */
#define atoi(s)          vt_atoi(s)
#define wcwidth(c)       u8_wcwidth((uint32_t)(c))
typedef uint32_t wchar_t;

#include "../apps/terminal/vt.h"
#include "../apps/terminal/vt.c"
