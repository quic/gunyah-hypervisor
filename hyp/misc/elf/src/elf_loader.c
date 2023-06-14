// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hyp_aspace.h>

#if ARCH_IS_64BIT
#define USE_ELF64
#endif
#include <compiler.h>
#include <elf.h>
#include <elf_loader.h>
#include <log.h>
#include <trace.h>
#include <util.h>

static const unsigned char *elf_ident = (unsigned char *)EI_MAG_STR;

// Simple unoptimized non-terminated string comparison
static bool
str_equal(const unsigned char *s1, const unsigned char *s2, size_t n)
{
	bool	ret = true;
	index_t i;

	assert(n > 0);

	for (i = 0; i < n; i++) {
		if (s1[i] != s2[i]) {
			ret = false;
			break;
		}
	}

	return ret;
}

bool
elf_valid(void *elf_file, size_t max_size)
{
	index_t	  i;
	bool	  ret	   = false;
	uintptr_t area_end = (uintptr_t)elf_file + max_size;

	Elf_Ehdr *ehdr = (Elf_Ehdr *)(uintptr_t)elf_file;

	if ((area_end < (uintptr_t)elf_file) ||
	    util_add_overflows((uintptr_t)ehdr, sizeof(ehdr)) ||
	    (((uintptr_t)ehdr + sizeof(ehdr)) > area_end)) {
		goto out;
	}

	if (!str_equal(ehdr->e_ident, elf_ident, EI_MAG_SIZE)) {
		goto out;
	}
	if (ehdr->e_ident[EI_CLASS] != ELF_CLASS) {
		goto out;
	}
	if (ehdr->e_ident[EI_DATA] != ELF_DATA_2LSB) {
		goto out;
	}
	if (ehdr->e_ident[EI_VERSION] != EV_CURRENT) {
		goto out;
	}
	if (ehdr->e_ident[EI_OSABI] != 0U) {
		goto out;
	}
	if (ehdr->e_ident[EI_ABIVERSION] != 0U) {
		goto out;
	}
	if (ehdr->e_type != ET_DYN) {
		goto out;
	}
#if defined(ARCH_ARM)
	if (ehdr->e_machine != EM_AARCH64) {
		goto out;
	}
#else
#error unimplemented
#endif

	if (util_add_overflows((uintptr_t)elf_file, ehdr->e_phoff)) {
		goto out;
	}

	Elf_Phdr *phdr	= (Elf_Phdr *)((uintptr_t)elf_file + ehdr->e_phoff);
	Elf_Half  phnum = ehdr->e_phnum;

	if (ehdr->e_phentsize != sizeof(Elf_Phdr)) {
		goto out;
	}
	if (util_add_overflows((uintptr_t)phdr, sizeof(phdr) * phnum) ||
	    ((((uintptr_t)phdr + (sizeof(phdr) * phnum)) > area_end))) {
		goto out;
	}

	// Ensure there is at least one load segment
	for (i = 0; i < phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {
			ret = true;
			break;
		}
	}

out:
	return ret;
}

Elf_Addr
elf_get_entry(void *elf_file)
{
	Elf_Ehdr *ehdr = (Elf_Ehdr *)(uintptr_t)elf_file;

	return ehdr->e_entry;
}

count_t
elf_get_num_phdrs(void *elf_file)
{
	Elf_Ehdr *ehdr = (Elf_Ehdr *)(uintptr_t)elf_file;

	return ehdr->e_phnum;
}

Elf_Phdr *
elf_get_phdr(void *elf_file, count_t index)
{
	Elf_Ehdr *ehdr	= (Elf_Ehdr *)(uintptr_t)elf_file;
	Elf_Phdr *phdr	= (Elf_Phdr *)((uintptr_t)elf_file + ehdr->e_phoff);
	Elf_Half  phnum = ehdr->e_phnum;

	assert(index < phnum);

	return &phdr[index];
}

error_t
elf_load_phys(void *elf_file, size_t elf_max_size, paddr_t phys_base)
{
	error_t	  ret;
	index_t	  i;
	uintptr_t elf_base = (uintptr_t)elf_file;
	Elf_Ehdr *ehdr	   = (Elf_Ehdr *)(uintptr_t)elf_file;
	Elf_Phdr *phdr	   = (Elf_Phdr *)((uintptr_t)elf_file + ehdr->e_phoff);
	Elf_Half  phnum	   = ehdr->e_phnum;

	// Copy all segments to the requested memory addresses
	for (i = 0; i < phnum; i++) {
		Elf_Phdr *cur_phdr = &phdr[i];
		Elf_Word  type	   = cur_phdr->p_type;

		// FIXME:
		assert(type != PT_TLS);

		if (type != PT_LOAD) {
			continue;
		}

		if (cur_phdr->p_filesz > cur_phdr->p_memsz) {
			ret = ERROR_ARGUMENT_SIZE;
			goto out;
		}

		if (util_add_overflows(cur_phdr->p_paddr, cur_phdr->p_memsz) ||
		    util_add_overflows(phys_base,
				       cur_phdr->p_paddr + cur_phdr->p_memsz) ||
		    ((cur_phdr->p_offset + cur_phdr->p_filesz) >
		     elf_max_size)) {
			ret = ERROR_ARGUMENT_SIZE;
			goto out;
		}

		if (util_add_overflows(cur_phdr->p_offset, cur_phdr->p_memsz) ||
		    util_add_overflows(elf_base, cur_phdr->p_offset +
							 cur_phdr->p_memsz)) {
			ret = ERROR_ARGUMENT_SIZE;
			goto out;
		}

		uintptr_t seg_base = elf_base + cur_phdr->p_offset;
		paddr_t	  seg_dest = phys_base + cur_phdr->p_paddr;

		error_t err;

		paddr_t map_base = util_balign_down(
			seg_dest, (paddr_t)PGTABLE_HYP_PAGE_SIZE);
		size_t map_size =
			util_balign_up(seg_dest + cur_phdr->p_memsz,
				       (paddr_t)PGTABLE_HYP_PAGE_SIZE) -
			map_base;

		// FIXME: should we use phys_map and phys_access eventually?
		err = hyp_aspace_map_direct(map_base, map_size,
					    PGTABLE_ACCESS_RW,
					    PGTABLE_HYP_MEMTYPE_WRITETHROUGH,
					    VMSA_SHAREABILITY_INNER_SHAREABLE);
		assert(err == OK);

		// copy elf segment data
		(void)memcpy((char *)seg_dest, (char *)seg_base,
			     cur_phdr->p_filesz);
		// zero bss
		size_t bss_size = cur_phdr->p_memsz - cur_phdr->p_filesz;
		(void)memset_s((char *)(seg_dest + cur_phdr->p_filesz),
			       bss_size, 0, bss_size);

		LOG(DEBUG, INFO, "Elf copied from {:#x} to {:#x} - size {:#x}",
		    seg_base, seg_dest, cur_phdr->p_filesz);
		err = hyp_aspace_unmap_direct(map_base, map_size);
		assert(err == OK);
	}

	ret = OK;
out:
	return ret;
}
