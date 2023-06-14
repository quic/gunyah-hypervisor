// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>

#include <addrspace.h>
#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <pgtable.h>
#include <preempt.h>
#include <trace.h>

#include <asm/event.h>

#include "event_handlers.h"

#define PHYS_MAX (1UL << GPT_PHYS_BITS)

static addrspace_t *as1;
static addrspace_t *as2;

static _Atomic bool tests_complete;

static addrspace_t *
create_addrspace(vmid_t vmid)
{
	partition_t *partition = partition_get_root();
	assert(partition != NULL);

	addrspace_ptr_result_t ret;
	addrspace_create_t     params = { NULL };

	ret = partition_allocate_addrspace(partition, params);
	if (ret.e != OK) {
		panic("Failed to create addrspace");
	}

	if (addrspace_configure(ret.r, vmid) != OK) {
		panic("Failed addrspace configuration");
	}

	if (object_activate_addrspace(ret.r) != OK) {
		panic("Failed addrspace activation");
	}

	return ret.r;
}

static memextent_t *
create_memextent(memextent_t *parent, size_t offset, size_t size, bool sparse)
{
	partition_t *partition = partition_get_root();
	assert(partition != NULL);

	memextent_ptr_result_t ret;
	memextent_create_t     params = { .memextent_device_mem = false };

	ret = partition_allocate_memextent(partition, params);
	if (ret.e != OK) {
		panic("Failed to create addrspace");
	}

	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_memtype(&attrs, MEMEXTENT_MEMTYPE_ANY);
	memextent_attrs_set_access(&attrs, PGTABLE_ACCESS_RWX);
	if (sparse) {
		memextent_attrs_set_type(&attrs, MEMEXTENT_TYPE_SPARSE);
	}

	if (parent != NULL) {
		if (memextent_configure_derive(ret.r, parent, offset, size,
					       attrs)) {
			panic("Failed to configure derived memextent");
		}
	} else {
		if (memextent_configure(ret.r, offset, size, attrs) != OK) {
			panic("Failed to configure memextent");
		}
	}

	if (object_activate_memextent(ret.r) != OK) {
		panic("Failed to activate memextent");
	}

	return ret.r;
}

static error_t
phys_range_walk(paddr_t phys, size_t size, void *arg)
{
	phys_range_result_t *ret = (phys_range_result_t *)arg;
	assert(ret != NULL);

	if ((ret->e != OK) && (size >= ret->r.size)) {
		ret->r.base = phys;
		ret->e	    = OK;
	}

	return OK;
}

static paddr_t
get_free_phys_range(size_t min_size)
{
	partition_t *partition = partition_get_root();
	assert(partition != NULL);

	phys_range_t	    range = { .size = min_size };
	phys_range_result_t ret	  = { .r = range, .e = ERROR_NOMEM };

	error_t err = memdb_walk((uintptr_t)partition, MEMDB_TYPE_PARTITION,
				 phys_range_walk, &ret);
	if ((err != OK) || (ret.e != OK)) {
		panic("Failed to find free phys range");
	}

	return ret.r.base;
}

static error_t
map_memextent(memextent_t *me, addrspace_t *as, vmaddr_t vbase, size_t offset,
	      size_t size, pgtable_vm_memtype_t memtype,
	      pgtable_access_t access)
{
	memextent_mapping_attrs_t map_attrs = memextent_mapping_attrs_default();
	memextent_mapping_attrs_set_memtype(&map_attrs, memtype);
	memextent_mapping_attrs_set_user_access(&map_attrs, access);
	memextent_mapping_attrs_set_kernel_access(&map_attrs, access);

	return memextent_map_partial(me, as, vbase, offset, size, map_attrs);
}

static bool
lookup_addrspace(addrspace_t *as, vmaddr_t vbase, paddr_t expected_phys,
		 pgtable_vm_memtype_t expected_memtype,
		 pgtable_access_t     expected_access)
{
	bool ret = false;

	paddr_t		     mapped_base;
	size_t		     mapped_size;
	pgtable_vm_memtype_t mapped_memtype;
	pgtable_access_t     mapped_vm_kernel_access;
	pgtable_access_t     mapped_vm_user_access;

	bool mapped = pgtable_vm_lookup(&as->vm_pgtable, vbase, &mapped_base,
					&mapped_size, &mapped_memtype,
					&mapped_vm_kernel_access,
					&mapped_vm_user_access);

	if (mapped) {
		mapped_base += vbase & (mapped_size - 1U);
		ret = (expected_phys == mapped_base) &&
		      (expected_memtype == mapped_memtype) &&
		      (expected_access == mapped_vm_kernel_access) &&
		      (expected_access == mapped_vm_user_access);
	}

	return ret;
}

static bool
is_owner(memextent_t *me, paddr_t phys, size_t size)
{
	return memdb_is_ownership_contiguous(phys, phys + size - 1U,
					     (uintptr_t)me, MEMDB_TYPE_EXTENT);
}

void
tests_memextent_sparse_init(void)
{
	as1 = create_addrspace(33U);
	as2 = create_addrspace(44U);
}

bool
tests_memextent_sparse_start(void)
{
	error_t err;

	cpulocal_begin();
	cpu_index_t cpu = cpulocal_get_index();
	cpulocal_end();

	if (cpu != 0U) {
		goto wait;
	}

	LOG(DEBUG, INFO, "Starting sparse memextent tests");

	memextent_t *me_0_0 = create_memextent(NULL, 0U, PHYS_MAX, true);
	assert(me_0_0 != NULL);

	// Test 1: Apply mapping after donate from partition.
	vmaddr_t	     vbase   = 0x80000000U;
	size_t		     size    = PGTABLE_VM_PAGE_SIZE;
	paddr_t		     phys    = get_free_phys_range(size);
	pgtable_vm_memtype_t memtype = PGTABLE_VM_MEMTYPE_NORMAL_WB;
	pgtable_access_t     access  = PGTABLE_ACCESS_RW;

	err = map_memextent(me_0_0, as1, vbase, phys, size, memtype, access);
	assert(err == OK);

	bool mapped = lookup_addrspace(as1, vbase, phys, memtype, access);
	assert(!mapped);

	err = memextent_donate_child(me_0_0, phys, size, false);
	assert(err == OK);

	mapped = lookup_addrspace(as1, vbase, phys, memtype, access);
	assert(mapped);

	err = memextent_donate_child(me_0_0, phys, size, true);
	assert(err == OK);

	mapped = lookup_addrspace(as1, vbase, phys, memtype, access);
	assert(!mapped);

	// Test 2: Donate between siblings.
	memextent_t *me_1_0 = create_memextent(me_0_0, 0U, PHYS_MAX, true);
	assert(me_1_0 != NULL);

	memextent_t *me_1_1 = create_memextent(me_0_0, 0U, PHYS_MAX, true);
	assert(me_1_1 != NULL);

	size = 0x10000U;
	phys = get_free_phys_range(size);

	err = memextent_donate_child(me_0_0, phys, size, false);
	assert(err == OK);

	bool owner = is_owner(me_0_0, phys, size);
	assert(owner);

	vmaddr_t vbase_1  = 0x60000000U;
	vmaddr_t vbase_2a = 0x340404000U;
	vmaddr_t vbase_2b = 0x288840000U;

	err = map_memextent(me_1_0, as1, vbase_1, phys, 0x6000U, memtype,
			    access);
	assert(err == OK);

	err = map_memextent(me_1_0, as2, vbase_2a, phys, size, memtype, access);
	assert(err == OK);

	err = map_memextent(me_1_1, as1, vbase_1 + 0x4000U, phys + 0x4000U,
			    0x6000U, memtype, access);
	assert(err == OK);

	err = map_memextent(me_1_1, as2, vbase_2b, phys, size, memtype, access);
	assert(err == OK);

	mapped = lookup_addrspace(as1, vbase_1, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as1, vbase_1 + 0x6000U, phys + 0x6000U,
				  memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2a, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2b, phys, memtype, access);
	assert(!mapped);

	err = memextent_donate_child(me_1_0, phys, size, false);
	assert(err == OK);

	owner = is_owner(me_1_0, phys, size);
	assert(owner);

	mapped = lookup_addrspace(as1, vbase_1, phys, memtype, access);
	assert(mapped);
	mapped = lookup_addrspace(as1, vbase_1 + 0x6000U, phys + 0x6000U,
				  memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2a, phys, memtype, access);
	assert(mapped);
	mapped = lookup_addrspace(as2, vbase_2b, phys, memtype, access);
	assert(!mapped);

	err = memextent_donate_sibling(me_1_0, me_1_1, phys, size);
	assert(err == OK);

	owner = is_owner(me_1_1, phys, size);
	assert(owner);

	mapped = lookup_addrspace(as1, vbase_1, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as1, vbase_1 + 0x6000U, phys + 0x6000U,
				  memtype, access);
	assert(mapped);
	mapped = lookup_addrspace(as2, vbase_2a, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2b, phys, memtype, access);
	assert(mapped);

	err = memextent_unmap_partial(me_1_0, as1, vbase_1, phys, 0x6000U);
	assert(err == OK);

	err = memextent_unmap_partial(me_1_0, as2, vbase_2a, phys, size);
	assert(err == OK);

	memextent_unmap_all(me_1_1);

	mapped = lookup_addrspace(as1, vbase_1, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as1, vbase_1 + 0x6000U, phys + 0x6000U,
				  memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2a, phys, memtype, access);
	assert(!mapped);
	mapped = lookup_addrspace(as2, vbase_2b, phys, memtype, access);
	assert(!mapped);

	object_put_addrspace(as1);
	object_put_addrspace(as2);

	object_put_memextent(me_0_0);
	object_put_memextent(me_1_0);
	object_put_memextent(me_1_1);

	LOG(DEBUG, INFO, "Finished sparse memextent tests");

	asm_event_store_and_wake(&tests_complete, true);

wait:
	while (!asm_event_load_before_wait(&tests_complete)) {
		asm_event_wait(&tests_complete);
	}

	return false;
}

#else

extern char unused;

#endif
