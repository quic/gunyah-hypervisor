// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

typedef uint16_t Elf_Half;
typedef uint32_t Elf_Word;

#if defined(USE_ELF64)

typedef int32_t	 Elf_Sword;
typedef int64_t	 Elf_Sxword;
typedef uint64_t Elf_Xword;
typedef uint64_t Elf_Addr;
typedef uint64_t Elf_Off;

#define R_TYPE(r_info) ((r_info)&0x7fffffffU)
#define R_SYM(r_info)  ((r_info) >> 32U)

#define ELF_CLASS ELF_CLASS_64

#elif defined(USE_ELF32)

#error unsupported USE_ELF32

#else
#error please define USE_ELF32 or USE_ELF64
#endif

#define EI_NIDENT 16

#define EI_MAG_STR                                                             \
	"\x7f"                                                                 \
	"ELF"
#define EI_MAG_SIZE 4

#define EI_CLASS      4
#define EI_DATA	      5
#define EI_VERSION    6
#define EI_OSABI      7
#define EI_ABIVERSION 8
#define EI_PAD	      9

#define ELF_CLASS_NONE 0U
#define ELF_CLASS_32   1U
#define ELF_CLASS_64   2U

#define ELF_DATA_NONE 0U
#define ELF_DATA_2LSB 1U
#define ELF_DATA_2MSB 2U

#define EV_NONE	   0U
#define EV_CURRENT 1U

#define ET_NONE 0U
#define ET_REL	1U
#define ET_EXEC 2U
#define ET_DYN	3U
#define ET_CORE 4U

#define EM_AARCH64 183U

#define PT_NULL	   0U
#define PT_LOAD	   1U
#define PT_DYNAMIC 2U
#define PT_INTERP  3U
#define PT_NOTE	   4U
#define PT_SHLIB   5U
#define PT_PHDR	   6U
#define PT_TLS	   7U
#define PT_NUM	   8U

#define PF_X 1U
#define PF_W 2U
#define PF_R 4U

#define DT_NULL	  0
#define DT_REL	  17
#define DT_RELSZ  18
#define DT_RELA	  7
#define DT_RELASZ 8
#define DT_CNT	  19

// Architecture relocation types
#define R_AARCH64_NONE	   0U
#define R_AARCH64_NULL	   256U
#define R_AARCH64_RELATIVE 1027U

typedef struct {
	Elf_Sxword d_tag;

	union {
		Elf_Xword d_val;
		Elf_Addr  d_ptr;
	} d_un;
} Elf_Dyn;

typedef struct {
	Elf_Addr  r_offset;
	Elf_Xword r_info;
} Elf_Rel;

typedef struct {
	Elf_Addr   r_offset;
	Elf_Xword  r_info;
	Elf_Sxword r_addend;
} Elf_Rela;

typedef struct {
	unsigned char e_ident[EI_NIDENT];

	Elf_Half e_type;
	Elf_Half e_machine;
	Elf_Word e_version;
	Elf_Addr e_entry;
	Elf_Off	 e_phoff;
	Elf_Off	 e_shoff;
	Elf_Word e_flags;

	Elf_Half e_ehsize;
	Elf_Half e_phentsize;
	Elf_Half e_phnum;
	Elf_Half e_shentsize;
	Elf_Half e_shnum;
	Elf_Half e_shstrndx;
} Elf_Ehdr;

typedef struct {
	Elf_Word  p_type;
	Elf_Word  p_flags;
	Elf_Off	  p_offset;
	Elf_Addr  p_vaddr;
	Elf_Addr  p_paddr;
	Elf_Xword p_filesz;
	Elf_Xword p_memsz;
	Elf_Xword p_align;
} Elf_Phdr;
