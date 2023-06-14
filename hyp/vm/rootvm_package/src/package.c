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
#include <cpulocal.h>
#include <cspace.h>
#include <elf.h>
#include <elf_loader.h>
#include <log.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
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
rootvm_package_load_elf(void *elf, size_t elf_max_size, addrspace_t *addrspace,
			vmaddr_t ipa_base, paddr_t phys_offset,
			memextent_t *me_rm)
{
	error_t err;
	count_t i;
	paddr_t limit = 0;

	assert(phys_offset >= PLATFORM_ROOTVM_LMA_BASE);
	size_t offset = phys_offset - PLATFORM_ROOTVM_LMA_BASE;

	paddr_t range_start = PLATFORM_ROOTVM_LMA_BASE;
	paddr_t range_end = PLATFORM_ROOTVM_LMA_BASE + PLATFORM_ROOTVM_LMA_SIZE;

	for (i = 0; i < elf_get_num_phdrs(elf); i++) {
		Elf_Phdr *phdr = elf_get_phdr(elf, i);
		assert(phdr != NULL);

		if (phdr->p_type != PT_LOAD) {
			continue;
		}
		uintptr_t seg_file_base = (uintptr_t)elf + phdr->p_offset;
		uintptr_t seg_file_end	= seg_file_base + phdr->p_filesz;

		// check all segments will fit within rootvm_mem area
		if (util_add_overflows(phdr->p_paddr, phdr->p_memsz)) {
			panic("ELF program header address + size overflow");
		}
		paddr_t seg_end = phdr->p_paddr + phdr->p_memsz;
		limit		= util_max(limit, seg_end);

		// sanity check input elf file does not overlap
		if (((uintptr_t)elf < range_end) &&
		    (seg_file_end > range_start)) {
			panic("ELF overlaps rootvm_mem area");
		}

		// Map segment in root VM address space using p_flags
		pgtable_access_t access = PGTABLE_ACCESS_R;
		assert((phdr->p_flags & PF_R) != 0U);
		if ((phdr->p_flags & PF_W) != 0U) {
			access = pgtable_access_combine(access,
							PGTABLE_ACCESS_W);
		}
		if ((phdr->p_flags & PF_X) != 0U) {
			access = pgtable_access_combine(access,
							PGTABLE_ACCESS_X);
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

	err = elf_load_phys(elf, elf_max_size, phys_offset);
	if (err != OK) {
		panic("Error loading ELF");
	}

	return util_balign_up(limit, (paddr_t)PGTABLE_HYP_PAGE_SIZE);
}

static void
update_cores_info(qcbor_enc_ctxt_t *qcbor_enc_ctxt) REQUIRE_PREEMPT_DISABLED
{
	cpu_index_t boot_core;
	uint64_t    usable_cores;

	assert(qcbor_enc_ctxt != NULL);

	boot_core = cpulocal_get_index();
	assert(PLATFORM_MAX_CORES > boot_core);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "boot_core", boot_core);

	usable_cores = PLATFORM_USABLE_CORES;
	assert((usable_cores & util_bit(boot_core)) != 0);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "usable_cores",
				   usable_cores);

	index_t max_idx = (index_t)((sizeof(usable_cores) * 8U) - 1U) -
			  compiler_clz(usable_cores);
	// can be a static assertion
	assert(max_idx < PLATFORM_MAX_CORES);
}

void
rootvm_package_handle_rootvm_init(partition_t *root_partition,
				  thread_t *root_thread, cspace_t *root_cspace,
				  hyp_env_data_t   *hyp_env,
				  qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	error_t ret;

	assert(qcbor_enc_ctxt != NULL);

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

	paddr_t load_base = PLATFORM_ROOTVM_LMA_BASE;
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
	// FIXME:
	// Root VM address space could be smaller
	// Currently limit usable address space to 1GiB
	vmaddr_t addr_limit = (vmaddr_t)util_bit(30);

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

			if (pkg_hdr->list[i].offset > map_size) {
				panic("ELF out of valid region");
			}

			size_t elf_max_size =
				map_size - pkg_hdr->list[i].offset;

			if (!elf_valid(elf, elf_max_size)) {
				panic("Invalid package ELF");
			}

			if (t == ROOTVM_PACKAGE_IMAGE_TYPE_RUNTIME) {
				runtime_ipa = ipa + offset;
				if (hyp_env->entry_ipa != 0U) {
					panic("Multiple RootVM runtime images");
				}
				hyp_env->entry_ipa =
					elf_get_entry(elf) + runtime_ipa;
			} else {
				app_ipa = ipa + offset;
			}

			load_next = rootvm_package_load_elf(elf, elf_max_size,
							    addrspace, ipa,
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

	assert(util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE));

	vmaddr_t env_data_ipa = ipa + offset;
	offset += util_balign_up(hyp_env->env_data_size, PGTABLE_VM_PAGE_SIZE);

	vmaddr_t app_heap_ipa  = ipa + offset;
	size_t	 app_heap_size = PLATFORM_ROOTVM_LMA_SIZE - offset;

	// The C runtime expects the heap to be page aligned.
	assert(util_is_baligned(app_heap_ipa, PGTABLE_VM_PAGE_SIZE));
	assert(util_is_baligned(app_heap_size, PGTABLE_VM_PAGE_SIZE));

	// Add info of the memory left in RM to hyp_env_data, so that it can be
	// later used for the boot info structure for example.
	hyp_env->me_ipa_base = ipa;
	hyp_env->env_ipa     = env_data_ipa;

	hyp_env->app_ipa       = app_ipa;
	hyp_env->runtime_ipa   = runtime_ipa;
	hyp_env->ipa_offset    = ipa - PLATFORM_ROOTVM_LMA_BASE;
	hyp_env->app_heap_ipa  = app_heap_ipa;
	hyp_env->app_heap_size = app_heap_size;

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "me_ipa_base",
				   hyp_env->me_ipa_base);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "ipa_offset",
				   hyp_env->ipa_offset);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "me_capid", me_cap);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "me_size",
				   PLATFORM_ROOTVM_LMA_SIZE);

	update_cores_info(qcbor_enc_ctxt);

	LOG(DEBUG, INFO, "runtime_ipa: {:#x}", runtime_ipa);
	LOG(DEBUG, INFO, "app_ipa: {:#x}", app_ipa);
	LOG(DEBUG, INFO, "env_data_ipa: {:#x}", env_data_ipa);
	LOG(DEBUG, INFO, "app_heap_ipa: {:#x}", app_heap_ipa);

	ret = hyp_aspace_unmap_direct(map_base, map_size);
	assert(ret == OK);

	// New code has been loaded, so we need to invalidate any physical
	// I-cache entries possibly prefetched
	__asm__ volatile("dsb ish; ic ialluis" ::: "memory");
}
