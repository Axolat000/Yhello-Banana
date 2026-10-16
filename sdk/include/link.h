#ifndef _LINK_H
#define _LINK_H
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
#define ElfW(t) ElfW_##t

struct dl_phdr_info {
    ElfW_Addr        dlpi_addr;    /* the load base */
    const char*      dlpi_name;
    const ElfW_Phdr* dlpi_phdr;
    uint16_t         dlpi_phnum;
};

int dl_iterate_phdr(int (*cb)(struct dl_phdr_info* info, size_t size, void* data), void* data);
#endif
