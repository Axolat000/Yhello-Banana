#ifndef _LINK_H
#define _LINK_H
#ifdef __cplusplus
extern "C" {
#endif
/* the loaded libraries, one by one (dl_iterate_phdr), as on Linux */
#include <stddef.h>
#include <stdint.h>

#ifdef __x86_64__
typedef struct { uint32_t p_type, p_flags; uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align; } ElfW_Phdr;
typedef uint64_t ElfW_Addr;
#else
typedef struct { uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align; } ElfW_Phdr;
typedef uint32_t ElfW_Addr;
#endif
typedef struct { uint32_t n_namesz, n_descsz, n_type; } ElfW_Nhdr;
#define ElfW(t) ElfW_##t
#ifndef PT_LOAD
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_NOTE    4
#define PT_TLS     7
#define NT_GNU_BUILD_ID 3
#endif

struct dl_phdr_info {
    ElfW_Addr        dlpi_addr;    /* the load base */
    const char*      dlpi_name;
    const ElfW_Phdr* dlpi_phdr;
    uint16_t         dlpi_phnum;
};

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info* info, size_t size, void* data), void* data);

/* a loaded object (dlinfo RTLD_DI_LINKMAP): where it is and its file */
struct link_map { ElfW_Addr l_addr; char* l_name; void* l_ld; struct link_map* l_next; struct link_map* l_prev; };
#ifdef __cplusplus
}
#endif
#endif
