// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)
#include <assert.h>
#include <hyptypes.h>

#include <addrspace.h>
#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <list.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <pgtable.h>
#include <spinlock.h>
#include <trace.h>

#include <events/object.h>

#include "event_handlers.h"

extern count_t tests_memextent_count;
count_t	       tests_memextent_count;

extern spinlock_t test_memextent_spinlock;
spinlock_t	  test_memextent_spinlock;

static addrspace_t *as;
static addrspace_t *as2;

static partition_t *partition;

#if !defined(NDEBUG)
extern void
pgtable_vm_dump(pgtable_vm_t *pgtable);
#endif

rcu_update_status_t
partition_destroy_memextent(rcu_entry_t *entry);

void
tests_memextent_init(void)
{
	spinlock_init(&test_memextent_spinlock);

	partition = partition_get_root();

	addrspace_ptr_result_t ret;

	addrspace_create_t params = { NULL };

	ret = partition_allocate_addrspace(partition, params);
	if (ret.e != OK) {
		panic("Failed address space creation");
	}

	as = ret.r;

	addrspace_create_t params2 = { NULL };

	ret = partition_allocate_addrspace(partition, params2);
	if (ret.e != OK) {
		panic("Failed address space 2 creation");
	}

	as2 = ret.r;

	// Dummy vmids
	if (addrspace_configure(as, 65U) != OK) {
		panic("Failed addrspace configuration");
	}

	if (addrspace_configure(as2, 66U) != OK) {
		panic("Failed addrspace 2 configuration");
	}

	if (object_activate_addrspace(as) != OK) {
		panic("Failed addrspace activation");
	}

	if (object_activate_addrspace(as2) != OK) {
		panic("Failed addrspace 2 activation");
	}
}

static error_t
get_free_mem_range(paddr_t base, size_t size, void *arg)
{
	test_free_range_t *free_range = (test_free_range_t *)arg;

	free_range->phys_base[free_range->count] = base;
	free_range->size[free_range->count]	 = size;
	free_range->count++;

	return OK;
}

static paddr_t
tests_find_free_range(void)
{
	error_t err = OK;

	// Get free ranges of physical memory from partition
	test_free_range_t free_range = { { 0 }, { 0 }, 0, { 0 } };

	err = memdb_walk((uintptr_t)partition, MEMDB_TYPE_PARTITION,
			 get_free_mem_range, (void *)&free_range);
	if (err != OK) {
		panic("Failed mem walk");
	}

	bool	free_range_found = false;
	paddr_t phys_base	 = 0;

	// Find a range that is big enough to contain the extents
	for (index_t i = 0; i < free_range.count; i++) {
		if (free_range.size[i] >= (4096 * 6)) {
			phys_base	 = free_range.phys_base[0];
			free_range_found = true;
			break;
		}
	}

	if (!free_range_found) {
		panic("No free range big enough");
	}

	return phys_base;
}

static memextent_t *
create_memextent(paddr_t phys_base, size_t size, memextent_memtype_t memtype,
		 pgtable_access_t access)
{
	memextent_ptr_result_t me_ret;
	memextent_create_t     params = { 0 };

	me_ret = partition_allocate_memextent(partition, params);
	if (me_ret.e != OK) {
		panic("Failed creation of new mem extent");
	}
	memextent_t *me = me_ret.r;

	spinlock_acquire(&me->header.lock);
	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_access(&attrs, access);
	memextent_attrs_set_memtype(&attrs, memtype);
	me_ret.e = memextent_configure(me, phys_base, size, attrs);
	if (me_ret.e != OK) {
		panic("Failed configuration of new mem extent");
	}
	spinlock_release(&me->header.lock);

	me_ret.e = object_activate_memextent(me);
	if (me_ret.e != OK) {
		panic("Failed activation of new mem extent");
	}

	return me;
}

//		  ----> extent 1
//		  |     [map as,
//		  |	 map as2]
//     partition -|
//		  |		  ----> extent 2.1 ----> extent 2.1.1
//		  |		  |
//		  ----> extent 2 -|
//			[map as]  |
//		     [after all   ----> extent 2.2 ----> extent 2.2.1
//		      derivations,     [unmap as,	 (update access)
//		      unmap as &        map as2]
//                    map as2]
static bool
tests_memextent_test1(paddr_t phys_base)
{
	bool	ret = false;
	error_t err = OK;

	paddr_t vm_base = phys_base;
	size_t	size	= 4096;

	// Mem extents specifications
	memextent_memtype_t memtype = MEMEXTENT_MEMTYPE_ANY;
	pgtable_access_t    access  = PGTABLE_ACCESS_RW;

	// Create 2 new memory extents from the partition

	memextent_t *me = create_memextent(phys_base, size, memtype, access);

	paddr_t phys_base2 = phys_base + size;
	paddr_t vm_base2   = vm_base + size;
	size_t	size2	   = 4096 * 5;

	memextent_t *me2 = create_memextent(phys_base2, size2, memtype, access);

#if !defined(NDEBUG)
	// Check empty pagetables
	LOG(DEBUG, INFO, "+--------------- EMPTY pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
	LOG(DEBUG, INFO, "+--------------- EMPTY pgtable 2:\n");
	pgtable_vm_dump(&as2->vm_pgtable);
#endif

	// Map extents. First mem extent is mapped into 2 address spaces and
	// second only into one.

	memextent_mapping_attrs_t map_attrs;

	memextent_mapping_attrs_set_user_access(&map_attrs, PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_kernel_access(&map_attrs,
						  PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_memtype(&map_attrs,
					    PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE);

	err = memextent_map(me, as, vm_base, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent");
	}

	err = memextent_map(me, as2, vm_base, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent to address space 2");
	}

	err = memextent_map(me2, as, vm_base2, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent 2");
	}

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+------------- 2 mappings pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
	LOG(DEBUG, INFO, "+------------- 1 mapping pgtable 2:\n");
	pgtable_vm_dump(&as2->vm_pgtable);
#endif

	// Derive 2 memory extents from second mem extent previously created
	// Derive mem extent from the beginning of the parent extent and second
	// from the last 2 pages of parent
	size_t offset = 0;
	size_t size3  = 4096;

	memextent_ptr_result_t me_ret;

	me_ret = memextent_derive(me2, offset, size3, memtype, access);
	if (me_ret.e != OK) {
		panic("Failed creation of derived mem extent");
	}

	memextent_t *me_d = me_ret.r;

	size_t	offset2	 = 4096 * 2;
	size_t	size4	 = 4096 * 2;
	paddr_t vm_base3 = vm_base2 + offset2;

	me_ret = memextent_derive(me2, offset2, size4, memtype, access);
	if (me_ret.e != OK) {
		panic("Failed creation of derived mem extent");
	}

	memextent_t *me_d2 = me_ret.r;

	// Unmap extent from as and map it to as2
	err = memextent_unmap(me_d2, as, vm_base3);
	if (err != OK) {
		panic("Failed memextent unmapping");
	}

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+------------ 1 unmapping pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif

	err = memextent_map(me_d2, as2, vm_base3, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent derived 2");
	}

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+------------ 1 mapping pgtable 2:\n");
	pgtable_vm_dump(&as2->vm_pgtable);
#endif

	// Derive memory extent from the entire derived extent.
	me_ret = memextent_derive(me_d, offset, size3, memtype, access);
	if (me_ret.e != OK) {
		panic("Failed creation of derived mem extent");
	}

	memextent_t *me_dd = me_ret.r;

	// Derive memory extent from first page of derived extent.
	me_ret = memextent_derive(me_d2, offset, size3, memtype, access);
	if (me_ret.e != OK) {
		panic("Failed creation of derived mem extent");
	}

	memextent_t *me_dd2 = me_ret.r;

	// Update access of mapping
	memextent_access_attrs_t access_attrs;

	memextent_access_attrs_set_user_access(&access_attrs, PGTABLE_ACCESS_R);
	memextent_access_attrs_set_kernel_access(&access_attrs,
						 PGTABLE_ACCESS_R);

	err = memextent_update_access(me_dd2, as2, vm_base3, access_attrs);
	if (err != OK) {
		panic("Failed memextent update access");
	}

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+------------ access updated pgtable 1:\n");
	pgtable_vm_dump(&as2->vm_pgtable);
#endif

	// Unmap extent 2 from as and map it to as2. This implies that only two
	// ranges of the extent will change the mapping since the rest of the
	// ranges are owned by the children
	err = memextent_unmap(me2, as, vm_base2);
	if (err != OK) {
		panic("Failed memextent unmapping");
	}

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+------------ 1 unmapping pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif

	err = memextent_map(me2, as2, vm_base2, map_attrs);
	if (err != OK) {
#if !defined(NDEBUG)
		// Check mappings in pagetables
		LOG(DEBUG, INFO, "+------------ mapping failed pgtable 2:\n");
		pgtable_vm_dump(&as2->vm_pgtable);
#endif
		panic("Failed mapping of mem extent 2");
	}

	// deactivate and indirectly unmap all extents from lowest children to
	// parent

	// Uncomment to make the destruction now and be able to see the pgtable
	// update instead of doing it sometime later by the rcu_update
#if 0
	trigger_object_deactivate_memextent_event(me_dd2);
	(void)partition_destroy_memextent(&me_dd2->header.rcu_entry);

	trigger_object_deactivate_memextent_event(me_dd);
	(void)partition_destroy_memextent(&me_dd->header.rcu_entry);

	trigger_object_deactivate_memextent_event(me_d2);
	(void)partition_destroy_memextent(&me_d2->header.rcu_entry);

	trigger_object_deactivate_memextent_event(me_d);
	(void)partition_destroy_memextent(&me_d->header.rcu_entry);

	trigger_object_deactivate_memextent_event(me2);
	(void)partition_destroy_memextent(&me2->header.rcu_entry);

	trigger_object_deactivate_memextent_event(me);
	(void)partition_destroy_memextent(&me->header.rcu_entry);
#else
	object_put_memextent(me_dd2);
	object_put_memextent(me_dd);
	object_put_memextent(me_d2);
	object_put_memextent(me_d);
	object_put_memextent(me2);
	object_put_memextent(me);
#endif

#if !defined(NDEBUG)
	// Check mappings in pagetables
	LOG(DEBUG, INFO, "+--------------- NO MAPS pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
	LOG(DEBUG, INFO, "+--------------- NO MAPS pgtable 2:\n");
	pgtable_vm_dump(&as2->vm_pgtable);
#endif

	return ret;
}

//	extent 1		extent 2
//	   |			   |
//	   V			   |
//   map as in vm_base		   |
//         |			   |
//         V			   |
//      extent 1.1		   |
//         |			   V
//         |		   map as in vm_base2
//         |		(indirectly unmaps ext 1.1)
//         V			   |
//    deactivate extent 1.1           |
//         |			   |
//         V			   |
//    unmap and deactivate extent 1   |
//			           V
//		unmap and deactivate extent 2
//
static bool
tests_memextent_test2(paddr_t phys_base)
{
	bool	ret = false;
	error_t err = OK;

	paddr_t vm_base = phys_base;
	size_t	size	= 4096 * 3;

	// Mem extents specifications
	memextent_memtype_t memtype = MEMEXTENT_MEMTYPE_DEVICE;
	pgtable_access_t    access  = PGTABLE_ACCESS_RW;

	// Create 2 new memory extents from the partition
	memextent_t *me = create_memextent(phys_base, size, memtype, access);

	paddr_t phys_base2 = phys_base + size;
	size_t	size2	   = 4096;

	memextent_t *me2 = create_memextent(phys_base2, size2, memtype, access);

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+--------------- EMPTY pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif

	// Map extents first mem extent to as
	memextent_mapping_attrs_t map_attrs;

	memextent_mapping_attrs_set_user_access(&map_attrs, PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_kernel_access(&map_attrs,
						  PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_memtype(&map_attrs,
					    PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE);

	err = memextent_map(me, as, vm_base, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent");
	}

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+------------- 1 mapping pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif

	// Derive 1 memory extent of one page from first mem extent starting
	// from phys_base + page
	size_t	offset	 = 4096;
	size_t	size3	 = 4096;
	paddr_t vm_base2 = vm_base + offset;

	memextent_ptr_result_t me_ret;

	me_ret = memextent_derive(me, offset, size3, memtype, access);
	if (me_ret.e != OK) {
		panic("Failed creation of derived mem extent");
	}

	memextent_t *me_d = me_ret.r;

	// Map mem extent 2 to as at vm_base2. This will first unmap the derived
	// extent from as before mapping the same virtual address to extent 2
	err = memextent_map(me2, as, vm_base2, map_attrs);
	if (err != OK) {
		panic("Failed mapping of mem extent 2");
	}

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+------------ 1 mapping pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif

	// deactivate derived extent and check what happens with the mapping of
	// vm_base 2 that used to be owned by the parent

	// Uncomment to make the destruction now and be able to see the pgtable
	// update instead of doing it sometime later by the rcu_update
#if 0
	trigger_object_deactivate_memextent_event(me_d);
	(void)partition_destroy_memextent(&me_d->header.rcu_entry);

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+--------------- deactivate me_d pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif
	trigger_object_deactivate_memextent_event(me);
	(void)partition_destroy_memextent(&me->header.rcu_entry);

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+--------------- deactivate me pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif
	trigger_object_deactivate_memextent_event(me2);
	(void)partition_destroy_memextent(&me2->header.rcu_entry);

#if !defined(NDEBUG)
	LOG(DEBUG, INFO, "+--------------- deactivate me2 pgtable 1:\n");
	pgtable_vm_dump(&as->vm_pgtable);
#endif
#else
	object_put_memextent(me_d);
	object_put_memextent(me);
	object_put_memextent(me2);
#endif

	return ret;
}

bool
tests_memextent(void)
{
	bool wait_all_cores_end	  = true;
	bool wait_all_cores_start = true;

	spinlock_acquire_nopreempt(&test_memextent_spinlock);
	tests_memextent_count++;
	spinlock_release_nopreempt(&test_memextent_spinlock);

	// Wait until all cores have reached this point to start.
	while (wait_all_cores_start) {
		spinlock_acquire_nopreempt(&test_memextent_spinlock);

		if (tests_memextent_count == (PLATFORM_MAX_CORES)) {
			wait_all_cores_start = false;
		}

		spinlock_release_nopreempt(&test_memextent_spinlock);
	}

	if (cpulocal_get_index() != 0U) {
		goto wait;
	}

	LOG(DEBUG, INFO, "Memextent tests start");

	paddr_t phys_base = tests_find_free_range();

	tests_memextent_test1(phys_base);

	phys_base = tests_find_free_range();

	tests_memextent_test2(phys_base);
	spinlock_acquire_nopreempt(&test_memextent_spinlock);
	tests_memextent_count++;
	spinlock_release_nopreempt(&test_memextent_spinlock);

	LOG(DEBUG, INFO, "Memextent tests finished");
wait:

	// Make all threads wait for test to end
	while (wait_all_cores_end) {
		spinlock_acquire_nopreempt(&test_memextent_spinlock);

		if (tests_memextent_count == PLATFORM_MAX_CORES + 1) {
			wait_all_cores_end = false;
		}

		spinlock_release_nopreempt(&test_memextent_spinlock);
	}

	return false;
}
#else

extern char unused;

#endif
