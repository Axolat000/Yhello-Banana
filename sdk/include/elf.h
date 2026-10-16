#ifndef _ELF_H
#define _ELF_H
#ifdef __cplusplus
extern "C" {
#endif
/* ELF file structures (what loaders and symbol decoders read) */
#include <stdint.h>

typedef uint16_t Elf32_Half;   typedef uint16_t Elf64_Half;
typedef uint32_t Elf32_Word;   typedef uint32_t Elf64_Word;
typedef int32_t  Elf32_Sword;  typedef int32_t  Elf64_Sword;
typedef uint64_t Elf32_Xword;  typedef uint64_t Elf64_Xword;
typedef int64_t  Elf32_Sxword; typedef int64_t  Elf64_Sxword;
typedef uint32_t Elf32_Addr;   typedef uint64_t Elf64_Addr;
typedef uint32_t Elf32_Off;    typedef uint64_t Elf64_Off;
typedef uint16_t Elf32_Section; typedef uint16_t Elf64_Section;

#define EI_NIDENT 16
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf32_Half e_type, e_machine; Elf32_Word e_version; Elf32_Addr e_entry; Elf32_Off e_phoff, e_shoff;
    Elf32_Word e_flags; Elf32_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf32_Ehdr;
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half e_type, e_machine; Elf64_Word e_version; Elf64_Addr e_entry; Elf64_Off e_phoff, e_shoff;
    Elf64_Word e_flags; Elf64_Half e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    Elf32_Word sh_name, sh_type, sh_flags; Elf32_Addr sh_addr; Elf32_Off sh_offset;
    Elf32_Word sh_size, sh_link, sh_info, sh_addralign, sh_entsize;
} Elf32_Shdr;
typedef struct {
    Elf64_Word sh_name, sh_type; Elf64_Xword sh_flags; Elf64_Addr sh_addr; Elf64_Off sh_offset;
    Elf64_Xword sh_size; Elf64_Word sh_link, sh_info; Elf64_Xword sh_addralign, sh_entsize;
} Elf64_Shdr;

typedef struct {
    Elf32_Word p_type; Elf32_Off p_offset; Elf32_Addr p_vaddr, p_paddr;
    Elf32_Word p_filesz, p_memsz, p_flags, p_align;
} Elf32_Phdr;
typedef struct {
    Elf64_Word p_type, p_flags; Elf64_Off p_offset; Elf64_Addr p_vaddr, p_paddr;
    Elf64_Xword p_filesz, p_memsz, p_align;
} Elf64_Phdr;

typedef struct { Elf32_Word st_name; Elf32_Addr st_value; Elf32_Word st_size; unsigned char st_info, st_other; Elf32_Section st_shndx; } Elf32_Sym;
typedef struct { Elf64_Word st_name; unsigned char st_info, st_other; Elf64_Section st_shndx; Elf64_Addr st_value; Elf64_Xword st_size; } Elf64_Sym;

typedef struct { Elf32_Sword d_tag; union { Elf32_Word d_val; Elf32_Addr d_ptr; } d_un; } Elf32_Dyn;
typedef struct { Elf64_Sxword d_tag; union { Elf64_Xword d_val; Elf64_Addr d_ptr; } d_un; } Elf64_Dyn;
typedef struct { Elf32_Addr r_offset; Elf32_Word r_info; } Elf32_Rel;
typedef struct { Elf64_Addr r_offset; Elf64_Xword r_info; } Elf64_Rel;
typedef struct { Elf32_Addr r_offset; Elf32_Word r_info; Elf32_Sword r_addend; } Elf32_Rela;
typedef struct { Elf64_Addr r_offset; Elf64_Xword r_info; Elf64_Sxword r_addend; } Elf64_Rela;

#define ELFMAG0 0x7f
#define ELFMAG1 'E'
#define ELFMAG2 'L'
#define ELFMAG3 'F'
#define ELFMAG  "\177ELF"
#define SELFMAG 4
#define EI_MAG0 0
#define EI_MAG1 1
#define EI_MAG2 2
#define EI_MAG3 3
#define EI_CLASS 4
#define EI_DATA 5
#define EI_VERSION 6
#define EI_OSABI 7
#define ELFCLASSNONE 0
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATANONE 0
#define ELFDATA2LSB 1
#define ELFDATA2MSB 2
#define EV_CURRENT 1
#define ET_NONE 0
#define ET_REL  1
#define ET_EXEC 2
#define ET_DYN  3
#define ET_CORE 4
#define EM_NONE    0
#define EM_386     3
#define EM_X86_64  62
#define EM_SPARC   2
#define EM_68K     4
#define EM_486     6
#define EM_MIPS    8
#define EM_MIPS_RS3_LE 10
#define EM_PARISC  15
#define EM_SPARC32PLUS 18
#define EM_PPC     20
#define EM_PPC64   21
#define EM_S390    22
#define EM_ARM     40
#define EM_SH      42
#define EM_SPARCV9 43
#define EM_IA_64   50
#define EM_RISCV   243
#define EM_LOONGARCH 258
#define EM_ALPHA   0x9026
#define EM_AARCH64 183

#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_HASH 5
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#define SHF_WRITE 1
#define SHF_ALLOC 2
#define SHF_EXECINSTR 4

#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#define PT_GNU_EH_FRAME 0x6474e550
#define PT_GNU_STACK 0x6474e551
#define PT_GNU_RELRO 0x6474e552
#define PF_X 1
#define PF_W 2
#define PF_R 4

#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define ELF32_ST_INFO(b, t) (((b) << 4) + ((t) & 0xf))
#define ELF64_ST_BIND(i) ELF32_ST_BIND(i)
#define ELF64_ST_TYPE(i) ELF32_ST_TYPE(i)
#define ELF64_ST_INFO(b, t) ELF32_ST_INFO(b, t)
#define ELF32_R_SYM(i) ((i) >> 8)
#define ELF32_R_TYPE(i) ((unsigned char)(i))
#define ELF64_R_SYM(i) ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffff)

#define DT_NULL 0
#define DT_NEEDED 1
#define DT_PLTRELSZ 2
#define DT_HASH 4
#define DT_STRTAB 5
#define DT_SYMTAB 6
#define DT_RELA 7
#define DT_RELASZ 8
#define DT_STRSZ 10
#define DT_SYMENT 11
#define DT_INIT 12
#define DT_FINI 13
#define DT_SONAME 14
#define DT_REL 17
#define DT_JMPREL 23
#define DT_DEBUG 21
#define DT_GNU_HASH 0x6ffffef5
#ifdef __cplusplus
}
#endif
#endif
