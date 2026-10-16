/* TinyCC (ports/tcc, LGPL 2.1) inside apps with USE_CL = 1: libtcc in one
 * unit, compiling OpenCL kernels into the app's memory and running them
 * (sdk/lib/cl.c). The source is a copy made by sdk/lib/mkcljit.sh. */
#ifdef __x86_64__
#define TCC_TARGET_X86_64 1
#else
#define TCC_TARGET_I386 1
#endif
#define ONE_SOURCE 1

/* compiled code goes in malloc'd memory, which apps can run as it is (sys/mman.h: mkcljit.sh) */
#define PAGESIZE 4096
#define freopen(path, mode, f) ((void)(path), (void)(mode), (f))

/* (TinyCC's own style; its warnings are not ours to fix) */
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wempty-body"
#pragma GCC diagnostic ignored "-Wclobbered"
#pragma GCC diagnostic ignored "-Wcast-function-type"
#pragma GCC diagnostic ignored "-Wmisleading-indentation"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#pragma GCC diagnostic ignored "-Wshift-negative-value"
#pragma GCC diagnostic ignored "-Wformat"
#pragma GCC diagnostic ignored "-Wformat-truncation"
#pragma GCC diagnostic ignored "-Wstringop-truncation"
#pragma GCC diagnostic ignored "-Wstringop-overflow"
#pragma GCC diagnostic ignored "-Warray-bounds"
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#pragma GCC diagnostic ignored "-Wuse-after-free"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wchar-subscripts"

#include "libtcc.c"
