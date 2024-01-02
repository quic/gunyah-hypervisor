// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>
#include <stddef.h>
#include <stdint.h>
#define USE_ELF64
#include <elf.h>
#include <reloc.h>

#include "arch/reloc.h"

// We must disable stack protection for this function, because the compiler
// might use a relocated absolute pointer to load the stack cookie in the
// function prologue, which will crash because this function hasn't run yet.
__attribute__((no_stack_protector)) void
boot_rel_fixup(Elf_Dyn *dyni, Elf_Addr addr_offset, Elf_Addr rel_offset)
{
	Elf_Xword dyn[DT_CNT];
	Elf_Rel	 *rel	   = NULL;
	Elf_Rel	 *rel_end  = NULL;
	Elf_Rela *rela	   = NULL;
	Elf_Rela *rela_end = NULL;

	// We avoid zeroing the dyn array with an initialiser list as the
	// compiler may optimise it to a memset, which may perform cache zeroing
	// operations that are not supported when the MMU is disabled.
	for (index_t i = 0; i < DT_CNT; i++) {
		dyn[i] = 0;
	}

	for (; dyni->d_tag != DT_NULL; dyni += 1) {
		if (dyni->d_tag < DT_CNT) {
			dyn[dyni->d_tag] = (Elf_Xword)dyni->d_un.d_ptr;
		}
	}

	rel	= (Elf_Rel *)(dyn[DT_REL] + addr_offset);
	rel_end = (Elf_Rel *)(dyn[DT_REL] + addr_offset + dyn[DT_RELSZ]);
	for (; rel < rel_end; rel++) {
		if (!ARCH_CAN_PATCH(rel->r_info)) {
			continue;
		}
		Elf_Addr *r = (Elf_Addr *)(rel->r_offset + addr_offset);
		*r += rel_offset;
	}

	rela	 = (Elf_Rela *)(dyn[DT_RELA] + addr_offset);
	rela_end = (Elf_Rela *)(dyn[DT_RELA] + addr_offset + dyn[DT_RELASZ]);
	for (; rela < rela_end; rela++) {
		if (!ARCH_CAN_PATCH(rela->r_info)) {
			continue;
		}
		Elf_Addr *r = (Elf_Addr *)(rela->r_offset + addr_offset);
		*r	    = rel_offset + (Elf_Addr)rela->r_addend;
	}
}
