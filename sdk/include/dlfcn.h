#ifndef _DLFCN_H
#define _DLFCN_H
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Shared libraries (Banana OS SDK): dlopen loads an ELF shared object
 * built for the app's CPU (gcc -shared -fPIC, as on Linux), with the
 * libraries it needs (DT_NEEDED: next to it, in the app's folder, /lib,
 * /usr/lib), resolves its symbols - its own, the libraries opened
 * RTLD_GLOBAL, then the app's: the SDK's C library and the app's own
 * functions - and runs its constructors. Every library is bound at once
 * (RTLD_NOW). Not there: thread-local variables in a library, symbol
 * versions, lazy binding.
 */
#define RTLD_LAZY     0x0001
#define RTLD_NOW      0x0002
#define RTLD_NOLOAD   0x0004
#define RTLD_NODELETE 0x1000
#define RTLD_GLOBAL   0x0100
#define RTLD_LOCAL    0
#define RTLD_DEFAULT  ((void*)0)
#define RTLD_NEXT     ((void*)-1)

typedef struct {
    const char* dli_fname;     /* the library (the app's name for the app itself) */
    void*       dli_fbase;     /* where it was loaded */
    const char* dli_sname;     /* the nearest symbol at or below the address */
    void*       dli_saddr;
} Dl_info;

void* dlopen(const char* path, int flags);
void* dlsym(void* handle, const char* name);
int   dlclose(void* handle);
char* dlerror(void);
int   dladdr(const void* addr, Dl_info* info);
/* dlinfo(RTLD_DI_LINKMAP): the struct link_map of <link.h>; dlvsym: no versions here (dlsym) */
#define RTLD_DI_LINKMAP 2
int   dlinfo(void* handle, int request, void* out);
void* dlvsym(void* handle, const char* name, const char* version);
#ifdef __cplusplus
}
#endif
#endif
