// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <allocator.h>
#include <memdb.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <util.h>

#include "event_handlers.h"

extern partition_t partition_hypervisor;

void_ptr_result_t
partition_alloc(partition_t *partition, size_t bytes, size_t min_alignment)
{
	void_ptr_result_t ret;

	ret = allocator_allocate_object(&partition->allocator, bytes,
					min_alignment);

	return ret;
}

error_t
partition_free(partition_t *partition, void *mem, size_t bytes)
{
	error_t ret;

	ret = allocator_deallocate_object(&partition->allocator, mem, bytes);

	return ret;
}

error_t
partition_free_phys(partition_t *partition, paddr_t phys, size_t bytes)
{
	void *mem = (void *)(phys + partition->virt_offset);
	return partition_free(partition, mem, bytes);
}

paddr_t
partition_virt_to_phys(partition_t *partition, uintptr_t addr)
{
	return (paddr_t)(addr - partition->virt_offset);
}

error_t
partition_standard_handle_object_create_partition(partition_create_t create)
{
	partition_t *partition = create.partition;
	assert(partition != NULL);

	partition->virt_offset = partition->header.partition->virt_offset;

	allocator_init(&partition->allocator);

	return OK;
}

error_t
partition_standard_handle_object_activate_partition(partition_t *partition)
{
	// Partitions hold a reference to themselves to prevent asynchronus
	// destruction when the last capability is deleted.
	//
	// Partitions must be explicitly destroyed to ensure that all objects in
	// them are deactivated synchronously, especially threads which might
	// still be executing on other CPUs; this self-reference will be deleted
	// after that is done. This destruction operation is not yet
	// implemented.
	object_get_partition_additional(partition);

	return OK;
}

noreturn void
partition_standard_handle_object_deactivate_partition(void)
{
	// This is currently not implemented and not needed. The self-reference
	// taken in activate() above should prevent this, but we panic here to
	// ensure that it doesn't happen by accident.
	panic("Partition deactivation attempted");
}

error_t
partition_mem_donate(partition_t *src_partition, paddr_t base, size_t size,
		     partition_t *dst_partition)
{
	error_t ret;

	partition_t *hyp_partition = partition_get_private();

	if ((size != 0U) && (!util_add_overflows(base, size - 1))) {
		ret = memdb_update(hyp_partition, base, base + (size - 1),
				   (uintptr_t)dst_partition,
				   MEMDB_TYPE_PARTITION,
				   (uintptr_t)src_partition,
				   MEMDB_TYPE_PARTITION);
	} else {
		ret = ERROR_ARGUMENT_SIZE;
	}

	return ret;
}

error_t
partition_add_heap(partition_t *partition, paddr_t base, size_t size)
{
	error_t ret;
	assert(partition != NULL);

	partition_t *hyp_partition = partition_get_private();

	if ((size != 0U) && (!util_add_overflows(base, size - 1))) {
		ret = memdb_update(hyp_partition, base, base + (size - 1),
				   (uintptr_t)&partition->allocator,
				   MEMDB_TYPE_ALLOCATOR, (uintptr_t)partition,
				   MEMDB_TYPE_PARTITION);
	} else {
		ret = ERROR_ARGUMENT_SIZE;
	}

	if (ret == OK) {
		uintptr_t virt = (uintptr_t)(base + partition->virt_offset);
		ret = allocator_heap_add_memory(&partition->allocator,
						(void *)virt, size);
	}

	return ret;
}

error_t
partition_map_and_add_heap(partition_t *partition, paddr_t phys, size_t size)
{
	error_t ret;
	error_t err = OK;
	assert(partition != NULL);

	// Mapping the partition should preallocate top page-table levels from
	// the hyp partition and then map with the target partition, but we
	// have a chicken-and-egg problem to solve: if the target partition has
	// no memory yet (because it is new) then it can't allocate page
	// tables. We will probably need to seed new partition allocators with
	// some memory from the parent partition.
	partition_t *hyp_partition = partition_get_private();

	uintptr_t virt = phys + partition->virt_offset;

	if ((size == 0U) || (util_add_overflows(virt, size - 1U) ||
			     (util_add_overflows(phys, size - 1U)))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	ret = memdb_update(hyp_partition, phys, phys + (size - 1U),
			   (uintptr_t)&partition->allocator,
			   MEMDB_TYPE_ALLOCATOR, (uintptr_t)partition,
			   MEMDB_TYPE_PARTITION);
	if (ret != OK) {
		goto out;
	}

	pgtable_hyp_start();
	ret = pgtable_hyp_map(hyp_partition, virt, size, phys,
			      PGTABLE_HYP_MEMTYPE_WRITEBACK, PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_INNER_SHAREABLE);
	if (ret == OK) {
		pgtable_hyp_commit();
		ret = allocator_heap_add_memory(&partition->allocator,
						(void *)virt, size);
	} else {
		// This should unmap the failed range, freeing to the target
		// partition and preserve the levels that were preallocated,
		// followed by unmapping the preserved tables (if they are
		// empty), freeing to the hyp_partition.
		pgtable_hyp_unmap(hyp_partition, virt, size,
				  PGTABLE_HYP_UNMAP_PRESERVE_NONE);
		pgtable_hyp_commit();
	}

	if (ret != OK) {
		err = memdb_update(hyp_partition, phys, phys + (size - 1U),
				   (uintptr_t)partition, MEMDB_TYPE_PARTITION,
				   (uintptr_t)&partition->allocator,
				   MEMDB_TYPE_ALLOCATOR);
		if (err != OK) {
			panic("Error updating memdb.");
		}
	}
out:
	return ret;
}
