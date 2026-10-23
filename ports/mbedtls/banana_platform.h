#ifndef BANANA_TLS_PLATFORM_H
#define BANANA_TLS_PLATFORM_H
#include "types.h"
#include "kstring.h"
#include <time.h>
void* banana_tls_calloc(size_t count, size_t size);
void banana_tls_free(void* ptr);
int64_t banana_tls_time(int64_t* out);
/* Raw seed source, independent of the kernel's uncredited timing pool.
 * Returns failure on CPUs without RDSEED or after bounded retries. */
int banana_tls_seed(unsigned char* out, size_t len);
#endif
