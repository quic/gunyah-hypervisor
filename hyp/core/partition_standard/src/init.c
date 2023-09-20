// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
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

static partition_t  hyp_partition;
static partition_t *root_partition;

extern const char image_virt_start;
extern const char image_virt_last;
extern const char image_phys_start;
extern const char image_phys_last;

static const uintptr_t virt_start = (uintptr_t)&image_virt_start;
static const paddr_t   phys_start = (paddr_t)&image_phys_start;
static const paddr_t   phys_last  = (paddr_t)&image_phys_last;

#if defined(ARCH_ARM) && ARCH_IS_64BIT
// Ensure hypervisor is 2MiB page size aligned to use AArch64 2M block mappings
static_assert((PLATFORM_RW_DATA_SIZE & 0x1fffffU) == 0U,
	      "PLATFORM_RW_DATA_SIZE must be 2MB aligned");
static_assert((PLATFORM_HEAP_PRIVATE_SIZE & 0xfffU) == 0U,
	      "PLATFORM_HEAP_PRIVATE_SIZE must be 4KB aligned");
#endif

void NOINLINE
partition_standard_handle_boot_cold_init(void)
{
	// Set up the hyp partition's header.
	refcount_init(&hyp_partition.header.refcount);
	hyp_partition.header.type = OBJECT_TYPE_PARTITION;
	atomic_store_release(&hyp_partition.header.state, OBJECT_STATE_ACTIVE);

	paddr_t hyp_heap_end =
		(phys_last + 1U) - ((size_t)PLATFORM_RW_DATA_SIZE -
				    (size_t)PLATFORM_HEAP_PRIVATE_SIZE);
	// Add hypervisor memory as a mapped range.
	hyp_partition.mapped_ranges[0].virt = virt_start;
	hyp_partition.mapped_ranges[0].phys = phys_start;
	hyp_partition.mapped_ranges[0].size =
		(size_t)(hyp_heap_end - phys_start);

	// Allocate management structures for the hypervisor allocator.
	if (allocator_init(&hyp_partition.allocator) != OK) {
		panic("allocator_init() failed for hyp partition");
	}

	// Configure partition to be privileged
	partition_option_flags_set_privileged(&hyp_partition.options, true);

	// Get remaining boot memory and assign it to hypervisor allocator.
	size_t		  hyp_alloc_size;
	void_ptr_result_t ret = bootmem_allocate_remaining(&hyp_alloc_size);
	if (ret.e != OK) {
		panic("no boot mem");
	}

	paddr_t phys = partition_virt_to_phys(&hyp_partition, (uintptr_t)ret.r);
	assert(phys != PADDR_INVALID);

	error_t err = trigger_allocator_add_ram_range_event(
		&hyp_partition, phys, (uintptr_t)ret.r, hyp_alloc_size);
	if (err != OK) {
		panic("Error moving bootmem to hyp_partition allocator");
	}
}

void NOINLINE
partition_standard_boot_add_private_heap(void)
{
	// Only the first 2MiB of RW data was mapped in the assembly mmu_init.
	// The remainder is mapped by hyp_aspace_handle_boot_cold_init. Because
	// of this, the additional memory if any needs to be added to the
	// hyp_partition allocator here.
	if ((size_t)PLATFORM_HEAP_PRIVATE_SIZE > 0x200000U) {
		size_t remaining_size =
			(size_t)PLATFORM_HEAP_PRIVATE_SIZE - 0x200000U;
		paddr_t remaining_phys =
			(phys_last + 1U) -
			((size_t)PLATFORM_RW_DATA_SIZE - 0x200000U);

		error_t err = partition_add_heap(&hyp_partition, remaining_phys,
						 remaining_size);
		if (err != OK) {
			panic("Error expanding hyp_partition allocator");
		}
	}
}

static void
partition_add_ram(partition_t *partition, paddr_t base, size_t size)
{
	error_t err;

#if defined(MODULE_MEM_MEMDB_GPT)
	// Add the ram range to the memory database.
	// FIXME:
	err = memdb_insert(&hyp_partition, base, base + (size - 1U),
			   (uintptr_t)partition, MEMDB_TYPE_PARTITION_NOMAP);
	if (err != OK) {
		panic("Error inserting ram to memdb");
	}
#endif

	// Notify modules about new ram. Memdb type for this range will be
	// updated to MEMDB_TYPE_PARTITION.
	err = trigger_partition_add_ram_range_event(partition, base, size);
	if (err != OK) {
		panic("Error adding ram to partition");
	}
}

void
partition_standard_handle_boot_hypervisor_start(void)
{
	// Allocate root partition from the hypervisor allocator
	partition_ptr_result_t part_ret = partition_allocate_partition(
		&hyp_partition, (partition_create_t){ 0 });
	if (part_ret.e != OK) {
		panic("Error allocating root partition");
	}
	root_partition = (partition_t *)part_ret.r;

	partition_option_flags_set_privileged(&root_partition->options, true);

	if (object_activate_partition(root_partition) != OK) {
		panic("Error activating root partition");
	}

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
			partition_add_ram(root_partition, rbase,
					  (size_t)(phys_start - rbase));
		}

		if ((phys_last >= rbase) && (phys_last < rlast)) {
			// Hyp image ends within the range, add the partial
			// range after the end of the hyp image
			partition_add_ram(root_partition, phys_last + 1U,
					  (size_t)(rlast - phys_last));
		}

		if ((phys_last < rbase) || (phys_start > rlast)) {
			// No overlap with hyp image; add the entire range
			partition_add_ram(root_partition, rbase, rsize);
		}
	}
}

partition_t *
partition_get_private(void)
{
	return &hyp_partition;
}

partition_t *
partition_get_root(void)
{
	return root_partition;
}
