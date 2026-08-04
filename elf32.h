#ifndef ELF32_H
#define ELF32_H
#include <stdint.h>

/* limited ELF32 support for objects that work(ish) with existing tools */

typedef uint32_t Elf32_Addr;
typedef uint16_t Elf32_Half;
typedef uint32_t Elf32_Off;
typedef  int32_t Elf32_Sword;
typedef uint32_t Elf32_Word;

#define EI_NIDENT 16
typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf32_Half    e_type;
	Elf32_Half    e_machine;
	Elf32_Word    e_version;
	Elf32_Addr    e_entry;
	Elf32_Off     e_phoff;
	Elf32_Off     e_shoff;
	Elf32_Word    e_flags;
	Elf32_Half    e_ehsize;
	Elf32_Half    e_phentsize;
	Elf32_Half    e_phnum;
	Elf32_Half    e_shentsize;
	Elf32_Half    e_shnum;
	Elf32_Half    e_shstrndx;
} Elf32_Ehdr;

typedef struct {
	Elf32_Word sh_name;       /* index into .shstrtab */
	Elf32_Word sh_type;       /* see enum ELF_SHT */
	Elf32_Word sh_flags;      /* see enum ELF_SHF */
	Elf32_Addr sh_addr;       /* VMA : 0 for relocatables */
	Elf32_Off  sh_off;        /* offset within this very file */
	Elf32_Word sh_size;       /* size in bytes */
	Elf32_Word sh_link;       /* sec index of link, see below */
	Elf32_Word sh_info;       /*  */
	Elf32_Word sh_addralign;  /*  */
	Elf32_Word sh_entsize;    /*  */
} Elf32_Shdr;

typedef struct {
	Elf32_Word st_name;     /* index into symbol string table .strtab */
	Elf32_Addr st_value;    /* offset from section; absolute if *ABS* */
	Elf32_Word st_size;     /* 0 if unknown or unsized */
	unsigned char st_info;  /* use ELF32_ST_INFO(STB...,STT...) */
	unsigned char st_other; /* 0 */
	Elf32_Half st_shndx;    /* section in which symbol resides */
} Elf32_Sym;

typedef struct {
	Elf32_Addr r_offset;
	Elf32_Word r_info;
} Elf32_Rel;

typedef struct {
	Elf32_Addr  r_offset;
	Elf32_Word  r_info;
	Elf32_Sword r_addend;
} Elf32_Rela;

enum Ei_CLASS {
	ELFCLASSNONE,
	ELFCLASS32,
	ELFCLASS64,
};
enum Ei_DATA {
	ELFDATANONE,
	ELFDATA2LSB,
	ELFDATA2MSB,
};
enum Ei_VERSION {
	EV_NONE,
	EV_CURRENT,
};

enum Elf_TYPE {
	ET_NONE = 0, /* no file type */
	ET_REL,      /* relocatable file */
	ET_EXEC,     /* executable file */
	ET_DYN,      /* shared object file */
	ET_CORE,     /* core file */
	ET_LOPROC = 0xFF00U,
	ET_HIPROC = 0xFFFFU,
};
enum Elf_MACHINE {
	EM_URSA = 0xCEC6, /* only our little CPU */
};

enum Elf_SHTYPE {
	SHT_NULL,
	SHT_PROGBITS,
	SHT_SYMTAB,
	SHT_STRTAB,
	SHT_RELA,
	SHT_HASH,
	SHT_DYNAMIC,
	SHT_NOTE,
	SHT_NOBITS,
	SHT_REL,
	SHT_SHLIB,
	SHT_DYNSYM,
};

enum Elf_SHFLAGS {
	SHF_WRITE     = 0x1,
	SHF_ALLOC     = 0x2,
	SHF_EXECINSTR = 0x4,
};

enum Elf_SPECIALSECINDEX {
	SHN_UNDEF  = 0,
	SHN_ABS    = 0xFFF1U,
	SHN_COMMON = 0xFFF2U,
};

enum Elf_SYMBIND {
	STB_LOCAL,
	STB_GLOBAL,
	STB_WEAK,
	STB_LOPROC = 13,
	STB_HIPROC = 15,
};
enum Elf_SYMTYPE {
	STT_NOTYPE,
	STT_OBJECT,
	STT_FUNC,
	STT_SECTION,
	STT_FILE,
	STT_LOPROC = 13,
	STT_HIPROC = 15,
};
#define Elf32_ST_BIND(i)   ((i)>>4)
#define Elf32_ST_TYPE(i)   ((i)&0xf)
#define Elf32_ST_INFO(b,t) (((b)<<4) + ((t) & 0xF))

#define Elf32_R_SYM(i)      ((i)>>8)
#define Elf32_R_TYPE(i)     ((unsigned char)(i))
#define Elf32_R_INFO(s, t)  (((s)<<8) + (unsigned char)(t))

/* Link and Info for section headers ***********************************
 * sh_type      sh_link                sh_info
 * SHT_DYNAMIC  string table secindex  0
 * SHT_HASH     symbol table secindex  0
 * SHT_REL      symbol table secindex  target secindex
 * SHT_RELA     symbol table secindex  target secindex
 * SHT_SYMTAB   os-specific            os-specific
 * SHT_DYNSYM   os-specific            os-specific
 * other        SHN_UNDEF              0
 *
 * For SysV, symtab/dynsym link to a string table
 * with "one greater than index of last local symbol" as info
 * (locals MUST precede globals)
 */

#endif
