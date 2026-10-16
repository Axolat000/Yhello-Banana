#ifndef _ALLOCA_H
#define _ALLOCA_H
#include <stddef.h>
/* memory on the stack, freed when the function returns (the compiler's own;
 * C++ also gets the declaration, for "using ::alloca") */
#ifdef __cplusplus
extern "C" void* alloca(size_t n);
#endif
#ifndef alloca
#define alloca(n) __builtin_alloca(n)
#endif
#endif
