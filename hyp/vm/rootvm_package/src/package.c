// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hyp_aspace.h>

#if ARCH_IS_64BIT
#define USE_ELF64
#endif
#include <string.h>

#include <compiler.h>
#include <cspace.h>
#include <elf.h>
#include <elf_loader.h>
#include <log.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <prng.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <vcpu.h>

#include "event_handlers.h"

// The physical address symbol pointing to the package image
extern const char image_pkg_start;

static memextent_t *
create_memextent(partition_t *root_partition, cspace_t *root_cspace,
		 paddr_t phys_base, size_t size, cap_id_t *new_cap_id,
		 pgtable_access_t access)
{
	error_t		    ret;
	memextent_memtype_t memtype = MEMEXTENT_MEMTYPE_ANY;

	memextent_create_t     params_me = { .memextent		   = NULL,
					     .memextent_device_mem = false };
	memextent_ptr_result_t me_ret;
	me_ret = partition_allocate_memextent(root_partition, params_me);
	if (me_ret.e != OK) {
		panic("Failed creation of new mem extent");
	}
	memextent_t *me = me_ret.r;

	spinlock_acquire(&me->header.lock);
	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_access(&attrs, access);
	memextent_attrs_set_memtype(&attrs, memtype);
	ret = memextent_configure(me, phys_base, size, attrs);
	if (ret != OK) {
		panic("Failed configuration of new mem extent");
	}
	spinlock_release(&me->header.lock);

	// Create a master cap for the memextent
	object_ptr_t obj_ptr;
	obj_ptr.memextent	  = me;
	cap_id_result_t capid_ret = cspace_create_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_MEMEXTENT);
	if (capid_ret.e != OK) {
		panic("Error create memextent cap id.");
	}

	ret = object_activate_memextent(me);
	if (ret != OK) {
		panic("Failed activation of new mem extent");
	}

	*new_cap_id = capid_ret.r;

	return me;
}

static paddr_t
rootvm_package_load_elf(void *elf, addrspace_t *addrspace, vmaddr_t ipa_base,
			paddr_t phys_offset, memextent_t *me_rm)
{
	error_t err;
	count_t i;
	paddr_t limit  = 0;
	size_t	offset = phys_offset - PLATFORM_ROOTVM_LMA_BASE;

	paddr_t range_end =
		(paddr_t)PLATFORM_ROOTVM_LMA_BASE + PLATFORM_ROOTVM_LMA_SIZE;

	for (i = 0; i < elf_get_num_phdrs(elf); i++) {
		Elf_Phdr *phdr = elf_get_phdr(elf, i);
		assert(phdr != NULL);

		if (phdr->p_type != PT_LOAD) {
			continue;
		}
		// check all segments will fit within rootvm_mem area
		paddr_t seg_end = phdr->p_paddr + phdr->p_memsz;
		if (seg_end > limit) {
			limit = seg_end;
		}

		// Map segment in root VM address space using p_flags
		pgtable_access_t access = PGTABLE_ACCESS_R;
		assert((phdr->p_flags & PF_R) != 0U);
		if ((phdr->p_flags & PF_W) != 0U) {
			access |= PGTABLE_ACCESS_W;
		}
		if ((phdr->p_flags & PF_X) != 0U) {
			access |= PGTABLE_ACCESS_X;
		}

		// Derive extents from RM memory extent
		// FIXME: this may fail if ELF segments not page aligned
		size_t size =
			util_balign_up(phdr->p_memsz, PGTABLE_VM_PAGE_SIZE);

		memextent_ptr_result_t me_ret = memextent_derive(
			me_rm, offset, size, MEMEXTENT_MEMTYPE_ANY, access);
		if (me_ret.e != OK) {
			panic("Failed creation of derived mem extent");
		}

		memextent_mapping_attrs_t map_attrs =
			memextent_mapping_attrs_default();
		memextent_mapping_attrs_set_user_access(&map_attrs, access);
		memextent_mapping_attrs_set_kernel_access(&map_attrs, access);
		memextent_mapping_attrs_set_memtype(
			&map_attrs, PGTABLE_VM_MEMTYPE_NORMAL_WB);

		// Map the ELF segment
		if (memextent_map(me_ret.r, addrspace, ipa_base + offset,
				  map_attrs) != OK) {
			panic("Error mapping to root VM address space");
		}

		offset += size;
	}

	limit = limit + phys_offset;

	if (limit > range_end) {
		panic("ELF segment out of range");
	}

	err = elf_load_phys(elf, phys_offset);
	if (err != OK) {
		panic("Error loading ELF");
	}

	return util_balign_up(limit, (paddr_t)PGTABLE_HYP_PAGE_SIZE);
}

void
rootvm_package_handle_rootvm_init(partition_t *root_partition,
				  thread_t *root_thread, cspace_t *root_cspace,
				  boot_env_data_t *env_data)
{
	error_t ret;

	assert(root_partition != NULL);
	assert(root_thread != NULL);
	assert(root_cspace != NULL);
	assert(root_thread->addrspace != NULL);

	addrspace_t *addrspace = root_thread->addrspace;

	paddr_t map_base = (paddr_t)&image_pkg_start;
	// FIXME: we could read headers and map incrementally as needed using
	// segment rights. A single 512KiB mapping is sufficient for now!
	size_t map_size = 0x00080000;
	ret = hyp_aspace_map_direct(map_base, map_size, PGTABLE_ACCESS_R,
				    PGTABLE_HYP_MEMTYPE_WRITEBACK,
				    VMSA_SHAREABILITY_INNER_SHAREABLE);
	assert(ret == OK);

	rootvm_package_header_t *pkg_hdr = (rootvm_package_header_t *)map_base;

	if (pkg_hdr->ident != ROOTVM_PACKAGE_IDENT) {
		panic("RootVM package header not found!");
	}
	if (pkg_hdr->items >= (uint32_t)ROOTVM_PACKAGE_ITEMS_MAX) {
		panic("Invalid pkg_hdr");
	}

	paddr_t load_base = (paddr_t)PLATFORM_ROOTVM_LMA_BASE;
	paddr_t load_next = load_base;

	// Create memory extent for the RM with randomized base
	uint64_t rand;
#if !defined(DISABLE_ROOTVM_ASLR)
	uint64_result_t res = prng_get64();
	assert(res.e == OK);
	rand = res.r;
#else
	rand = 0x10000000U;
#endif

#if 0
	// FIXME: Root VM address space could be smaller
	// Currently limit usable address space to 1GiB
	vmaddr_t addr_limit = (vmaddr_t)1 << 30;

	vmaddr_t ipa = (vmaddr_t)rand % (addr_limit - PLATFORM_ROOTVM_LMA_SIZE -
					 PGTABLE_VM_PAGE_SIZE);
	ipa += PGTABLE_VM_PAGE_SIZE; // avoid use of the zero page
	ipa = util_balign_down(ipa, PGTABLE_VM_PAGE_SIZE);
#else
	(void)rand;
	vmaddr_t ipa = PLATFORM_ROOTVM_LMA_BASE;
#endif

	// Map the root_thread memory as RW by default. Elf segments will be
	// remapped with the required rights.
	cap_id_t     me_cap;
	memextent_t *me		 = create_memextent(root_partition, root_cspace,
					    PLATFORM_ROOTVM_LMA_BASE,
					    PLATFORM_ROOTVM_LMA_SIZE, &me_cap,
					    PGTABLE_ACCESS_RWX);
	vmaddr_t     runtime_ipa = 0U;
	vmaddr_t     app_ipa	 = 0U;
	size_t	     offset	 = 0U;

	index_t i;
	for (i = 0U; i < (index_t)pkg_hdr->items; i++) {
		rootvm_package_image_type_t t =
			(rootvm_package_image_type_t)pkg_hdr->list[i].type;

		switch (t) {
		case ROOTVM_PACKAGE_IMAGE_TYPE_RUNTIME:
		case ROOTVM_PACKAGE_IMAGE_TYPE_APPLICATION:
			LOG(DEBUG, INFO,
			    "Processing package image ({:d}) type={:d}", i, t);

			void *elf =
				(void *)(map_base + pkg_hdr->list[i].offset);

			if (!elf_valid(elf)) {
				panic("Invalid package ELF");
			}

			if (t == ROOTVM_PACKAGE_IMAGE_TYPE_RUNTIME) {
				runtime_ipa = ipa + offset;
				if (env_data->entry_ipa != 0U) {
					panic("Multiple RootVM runtime images");
				}
				env_data->entry_ipa =
					elf_get_entry(elf) + runtime_ipa;
			} else {
				app_ipa = ipa + offset;
			}

			load_next = rootvm_package_load_elf(elf, addrspace, ipa,
							    load_next, me);
			break;
		case ROOTVM_PACKAGE_IMAGE_TYPE_UNKNOWN:
		default:
			panic("Bad image type");
		}

		offset = load_next - PLATFORM_ROOTVM_LMA_BASE;
	}

	memextent_mapping_attrs_t map_attrs = memextent_mapping_attrs_default();
	memextent_mapping_attrs_set_user_access(&map_attrs, PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_kernel_access(&map_attrs,
						  PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_memtype(&map_attrs,
					    PGTABLE_VM_MEMTYPE_NORMAL_WB);

	// Map all the remaining root VM memory as RW
	if (memextent_map(me, addrspace, ipa, map_attrs) != OK) {
		panic("Error mapping to root VM address space");
	}

	vmaddr_t env_data_ipa = ipa + offset;
	size_t	 env_data_size =
		util_balign_up(sizeof(boot_env_data_t), PGTABLE_VM_PAGE_SIZE);
	offset += env_data_size;

	vmaddr_t app_heap_ipa  = ipa + offset;
	size_t	 app_heap_size = util_balign_down(
		  PLATFORM_ROOTVM_LMA_SIZE - offset, PGTABLE_VM_PAGE_SIZE);

	// Add info of the memory left in RM to boot_env_data, so that it can be
	// later used for the boot info structure for example.
	env_data->me_capid    = me_cap;
	env_data->me_ipa_base = ipa;
	env_data->me_size     = PLATFORM_ROOTVM_LMA_SIZE;
	env_data->env_ipa     = env_data_ipa;

	env_data->app_ipa     = app_ipa;
	env_data->runtime_ipa = runtime_ipa;
	env_data->ipa_offset  = ipa - PLATFORM_ROOTVM_LMA_BASE;

	// Set arguments for runtime.
	vcpu_gpr_write(root_thread, 1, app_ipa);
	vcpu_gpr_write(root_thread, 2, runtime_ipa);
	vcpu_gpr_write(root_thread, 3, app_heap_ipa);
	vcpu_gpr_write(root_thread, 4, app_heap_size);

	LOG(DEBUG, INFO, "runtime_ipa: {:#x}", runtime_ipa);
	LOG(DEBUG, INFO, "app_ipa: {:#x}", app_ipa);

	ret = hyp_aspace_unmap_direct(map_base, map_size);
	assert(ret == OK);

	// New code has been loaded, so we need to invalidate any physical
	// I-cache entries possibly prefetched
	__asm__ volatile("dsb ish; ic ialluis" ::: "memory");
}
