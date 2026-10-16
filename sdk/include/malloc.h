#ifndef _MALLOC_H
#define _MALLOC_H
#ifdef __cplusplus
extern "C" {
#endif
#include <stdlib.h>
size_t malloc_usable_size(void* p);
void*  memalign(size_t align, size_t size);
struct mallinfo { int arena, ordblks, smblks, hblks, hblkhd, usmblks, fsmblks, uordblks, fordblks, keepcost; };
struct mallinfo mallinfo(void);
int    malloc_trim(size_t pad);
#ifdef __cplusplus
}
#endif
#endif
