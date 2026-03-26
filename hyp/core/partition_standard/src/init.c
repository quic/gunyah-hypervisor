// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// This file defines partition_get_root().
#define ROOTVM_INIT 1

#include <assert.h>
#include <hyptypes.h>
#include <stdalign.h>
#include <string.h>

#include <allocator.h>
#include <atomic.h>
#include <attributes.h>
#include <bootmem.h>
#include <list.h>
#include <memdb.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <platform_mem.h>
#include <refcount.h>
#include <util.h>

#include <events/allocator.h>
#include <events/partition.h>

#include <asm/cpu.h>

#include "event_handlers.h"

static partition_t  partition_hyp;
static partition_t *partition_root;

extern const char image_virt_start;
extern const char image_virt_last;
extern const char image_phys_start;
extern const char image_phys_last;

static const uintptr_t virt_start = (uintptr_t)&image_virt_start;
static const uintptr_t virt_last  = (uintptr_t)&image_virt_last;
static const paddr_t   phys_start = (paddr_t)&image_phys_start;
static const paddr_t   phys_last  = (paddr_t)&image_phys_last;

#if defined(ARCH_ARM) && ARCH_IS_64BIT
// Ensure hypervisor is 2MiB page size aligned to use AArch64 2M block mappings
static_assert(((size_t)PLATFORM_RW_DATA_SIZE & 0x1fffffU) == 0U,
	      "PLATFORM_RW_DATA_SIZE must be 2MB aligned");
static_assert(((size_t)PLATFORM_HEAP_PRIVATE_SIZE & 0xfffU) == 0U,
	      "PLATFORM_HEAP_PRIVATE_SIZE must be 4KB aligned");
#endif

void
partition_standard_handle_boot_runtime_first_init(void)
{
	// Setup hyp partition's refcount, needed for idle thread init.
	refcount_init(&partition_hyp.header.refcount);

	partition_hyp.header.type = OBJECT_TYPE_PARTITION;
}

void NOINLINE
partition_standard_handle_boot_cold_init(void)
{
	// Set up the hyp partition's header.
	list_init(&partition_hyp.mapped_ranges);
	atomic_store_release(&partition_hyp.header.state, OBJECT_STATE_ACTIVE);

	// Add hypervisor memory as a mapped range.
	partition_mapped_range_t *mr = NULL;
	void_ptr_result_t	  alloc_ret =
		bootmem_allocate(sizeof(*mr), alignof(*mr));
	if (alloc_ret.e != OK) {
		panic("Failed bootmem allocate for mapped range");
	}

	(void)memset_s(alloc_ret.r, sizeof(*mr), 0, sizeof(*mr));
	mr = (partition_mapped_range_t *)alloc_ret.r;

	paddr_t hyp_heap_end =
		(phys_last + 1U) - ((size_t)PLATFORM_RW_DATA_SIZE -
				    (size_t)PLATFORM_HEAP_PRIVATE_SIZE);
	mr->virt = virt_start;
	mr->phys = phys_start;
	mr->size = (size_t)(hyp_heap_end - phys_start);

	// Set the default allocator memory attributes for the mapped range.
	// This isn't really the best location to track these attributes given
	// only part of this memory is used for allocations, and it assumes the
	// attributes are the same for the entire range. Consider an alternate
	// location for this in future.
	// FIXME: QC Gunyah issue #245
	mr->attr = allocator_memattr_default();

	list_insert_at_tail_release(&partition_hyp.mapped_ranges,
				    &mr->list_node);
	partition_hyp.mapped_count = 1U;

	// Allocate management structures for the hypervisor allocator.
	if (allocator_init(&partition_hyp.allocator) != OK) {
		panic("allocator_init() failed for hyp partition");
	}

	// Configure partition to be privileged
	partition_option_flags_set_privileged(&partition_hyp.options, true);

	// Get remaining boot memory and assign it to hypervisor allocator.
	size_t		  hyp_alloc_size;
	void_ptr_result_t ret = bootmem_allocate_remaining(&hyp_alloc_size);
	if (ret.e != OK) {
		panic("no boot mem");
	}

	paddr_t phys = partition_image_virt_to_phys((uintptr_t)ret.r);

	error_t err = trigger_allocator_add_ram_range_event(
		&partition_hyp, phys, (uintptr_t)ret.r, hyp_alloc_size,
		allocator_memattr_default());
	if (err != OK) {
		panic("Error moving bootmem to partition_hyp allocator");
	}
}

void NOINLINE
partition_standard_boot_add_private_heap(void)
{
	// Only the first 2MiB of RW data was mapped in the assembly mmu_init.
	// The remainder is mapped by hyp_aspace_handle_boot_cold_init. Because
	// of this, the additional memory if any needs to be added to the
	// partition_hyp allocator here.
	if ((size_t)PLATFORM_HEAP_PRIVATE_SIZE > 0x200000U) {
		size_t remaining_size =
			(size_t)PLATFORM_HEAP_PRIVATE_SIZE - 0x200000U;
		paddr_t remaining_phys =
			(phys_last + 1U) -
			((size_t)PLATFORM_RW_DATA_SIZE - 0x200000U);

		error_t err = partition_add_heap(&partition_hyp, remaining_phys,
						 remaining_size);
		if (err != OK) {
			panic("Error expanding partition_hyp allocator");
		}
	}
}

void NOINLINE
partition_standard_boot_create_root_partition(void)
{
	// Allocate root partition from the hypervisor allocator
	partition_ptr_result_t part_ret = partition_allocate_partition(
		&partition_hyp, (partition_create_t){ 0 });
	if (part_ret.e != OK) {
		panic("Error allocating root partition");
	}
	partition_root = (partition_t *)part_ret.r;

	partition_option_flags_set_privileged(&partition_root->options, true);

	if (object_activate_partition(partition_root) != OK) {
		panic("Error activating root partition");
	}

#if PLATFORM_HEAP_PRIVATE_SIZE < PLATFORM_RW_DATA_SIZE
	// Add the remainder of the RW data to the root partition
	size_t	root_heap_size = (size_t)PLATFORM_RW_DATA_SIZE -
				 (size_t)PLATFORM_HEAP_PRIVATE_SIZE;
	paddr_t root_phys      = (phys_last + 1U) - root_heap_size;
	bool	from_heap      = false;
#else
	// Allocate some minimal bootstrapping memory for the root partition
	const size_t	  root_heap_size = PGTABLE_HYP_PAGE_SIZE;
	void_ptr_result_t alloc_ret	 = partition_alloc(
		&partition_hyp, root_heap_size, PGTABLE_HYP_PAGE_SIZE);
	if (alloc_ret.e != OK) {
		panic("partition_alloc for bootstrapping heap failed");
	}
	paddr_t root_phys =
		partition_virt_to_phys(&partition_hyp, (uintptr_t)alloc_ret.r);
	bool from_heap = true;
#endif

	error_t err = partition_mem_donate(&partition_hyp, root_phys,
					   root_heap_size, partition_root,
					   from_heap);
	if (err != OK) {
		panic("partition_mem_donate to root partition failed");
	}

	err = partition_map_and_add_heap(partition_root, root_phys,
					 root_heap_size);
	if (err != OK) {
		panic("partition_map_and_add_heap to root partition failed");
	}
}

void
partition_standard_handle_boot_hypervisor_start(void)
{
	error_t err = platform_ram_probe();
	if (err != OK) {
		panic("Platform RAM probe failed");
	}

	platform_ram_info_t *ram_info = platform_get_ram_info();
	assert(ram_info != NULL);

	for (index_t i = 0U; i < ram_info->num_ranges; i++) {
		paddr_t rbase = ram_info->ram_range[i].base;
		size_t	rsize = ram_info->ram_range[i].size;

		assert(rsize != 0U);
		assert(!util_add_overflows(rbase, rsize - 1U));

		paddr_t rlast = rbase + (rsize - 1U);

		if ((phys_start > rbase) && (phys_start <= rlast)) {
			// Hyp image starts within the range; add the partial
			// range before the start of the hyp image
			err = partition_add_ram_range(
				partition_root, rbase,
				(size_t)(phys_start - rbase), false);
			if (err != OK) {
				goto out;
			}
		}

		if ((phys_last >= rbase) && (phys_last < rlast)) {
			// Hyp image ends within the range, add the partial
			// range after the end of the hyp image
			err = partition_add_ram_range(
				partition_root, phys_last + 1U,
				(size_t)(rlast - phys_last), false);
			if (err != OK) {
				goto out;
			}
		}

		if ((phys_last < rbase) || (phys_start > rlast)) {
			// No overlap with hyp image; add the entire range
			err = partition_add_ram_range(partition_root, rbase,
						      rsize, false);
			if (err != OK) {
				goto out;
			}
		}
	}

out:
	if (err != OK) {
		panic("Adding platform RAM ranges failed");
	}
}

paddr_t
partition_image_virt_to_phys(uintptr_t addr)
{
	assert((addr >= virt_start) && (addr < virt_last));

	return addr - virt_start + phys_start;
}

partition_t *
partition_get_private(void)
{
	return &partition_hyp;
}

partition_t *
partition_get_root(void)
{
	return partition_root;
}
