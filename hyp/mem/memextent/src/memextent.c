// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>

#include <events/memextent.h>
#include <events/object.h>

#include "event_handlers.h"

error_t
memextent_handle_object_create_memextent(memextent_create_t params)
{
	memextent_t *memextent = params.memextent;
	assert(memextent != NULL);
	spinlock_init(&memextent->lock);
	list_init(&memextent->children_list);

	memextent->device_mem = params.memextent_device_mem;

	return OK;
}

error_t
memextent_configure(memextent_t *me, paddr_t phys_base, size_t size,
		    memextent_attrs_t attributes)
{
	error_t ret = OK;

	assert(me != NULL);

	// The address range must not wrap around the end of the address space
	if ((size == 0U) || util_add_overflows(phys_base, size - 1U)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(phys_base, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	// Only basic memory extents are implemented
	if ((memextent_attrs_get_res_0(&attributes) != 0U) ||
	    (memextent_attrs_get_append(&attributes))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// Validate arguments
	memextent_memtype_t memtype = memextent_attrs_get_memtype(&attributes);
	switch (memtype) {
	case MEMEXTENT_MEMTYPE_ANY:
	case MEMEXTENT_MEMTYPE_DEVICE:
	case MEMEXTENT_MEMTYPE_UNCACHED:
#if defined(ARCH_AARCH64_USE_S2FWB)
	case MEMEXTENT_MEMTYPE_CACHED:
#endif
		break;
#if !defined(ARCH_AARCH64_USE_S2FWB)
	// Without S2FWB, we cannot force cached mappings
	case MEMEXTENT_MEMTYPE_CACHED:
#endif
	default:
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	pgtable_access_t access = memextent_attrs_get_access(&attributes);
	switch (access) {
	case PGTABLE_ACCESS_X:
	case PGTABLE_ACCESS_W:
	case PGTABLE_ACCESS_R:
	case PGTABLE_ACCESS_RX:
	case PGTABLE_ACCESS_RW:
	case PGTABLE_ACCESS_RWX:
		break;
	case PGTABLE_ACCESS_NONE:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	me->type      = MEMEXTENT_TYPE_BASIC;
	me->phys_base = phys_base;
	me->size      = size;
	me->memtype   = memtype;
	me->access    = access;

	if (me->parent != NULL) {
		object_put_memextent(me->parent);
	}

	me->parent = NULL;
out:
	return ret;
}

error_t
memextent_configure_derive(memextent_t *me, memextent_t *parent, size_t offset,
			   size_t size, memextent_attrs_t attributes)
{
	error_t ret = OK;

	assert(parent != NULL);
	assert(me != NULL);

	spinlock_acquire(&parent->lock);

	if ((size == 0U) || util_add_overflows(offset, size - 1U)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (util_add_overflows(parent->phys_base, offset) ||
	    ((parent->phys_base + offset) >=
	     (parent->phys_base + parent->size)) ||
	    ((offset + size) > parent->size)) {
		ret = ERROR_ADDR_INVALID;
		goto out;
	}

	if (!util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	// Validate arguments
	memextent_memtype_t memtype = memextent_attrs_get_memtype(&attributes);
	switch (memtype) {
	case MEMEXTENT_MEMTYPE_ANY:
	case MEMEXTENT_MEMTYPE_DEVICE:
	case MEMEXTENT_MEMTYPE_UNCACHED:
#if defined(ARCH_AARCH64_USE_S2FWB)
	case MEMEXTENT_MEMTYPE_CACHED:
#endif
		break;
#if !defined(ARCH_AARCH64_USE_S2FWB)
	// Without S2FWB, we cannot force cached mappings
	case MEMEXTENT_MEMTYPE_CACHED:
#endif
	default:
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	pgtable_access_t access = memextent_attrs_get_access(&attributes);
	switch (access) {
	case PGTABLE_ACCESS_X:
	case PGTABLE_ACCESS_W:
	case PGTABLE_ACCESS_R:
	case PGTABLE_ACCESS_RX:
	case PGTABLE_ACCESS_RW:
	case PGTABLE_ACCESS_RWX:
		break;
	case PGTABLE_ACCESS_NONE:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if ((parent->access & access) != access) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// Only basic memory extents are implemented
	if ((memextent_attrs_get_res_0(&attributes) != 0U) ||
	    (memextent_attrs_get_append(&attributes))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	paddr_t phys_base = parent->phys_base + offset;

	me->type      = MEMEXTENT_TYPE_BASIC;
	me->phys_base = phys_base;
	me->size      = size;
	me->memtype   = memtype;
	me->access    = access;

	if (me->parent != NULL) {
		object_put_memextent(me->parent);
	}

	me->parent = object_get_memextent_additional(parent);

out:
	spinlock_release(&parent->lock);

	return ret;
}

error_t
memextent_handle_object_activate_memextent(memextent_t *me)
{
	assert(me != NULL);
	error_t ret = OK;

	if (me->parent != NULL) {
		assert(!me->device_mem);

		// Check new memtype is compatible with parent type
		switch (me->parent->memtype) {
		case MEMEXTENT_MEMTYPE_ANY:
			break;
		case MEMEXTENT_MEMTYPE_DEVICE:
		case MEMEXTENT_MEMTYPE_UNCACHED:
#if defined(ARCH_AARCH64_USE_S2FWB)
		case MEMEXTENT_MEMTYPE_CACHED:
#endif
			if (me->memtype != me->parent->memtype) {
				ret = ERROR_ARGUMENT_INVALID;
			}
			break;
#if !defined(ARCH_AARCH64_USE_S2FWB)
		// Without S2FWB, we cannot force cached mappings
		case MEMEXTENT_MEMTYPE_CACHED:
#endif
		default:
			ret = ERROR_OBJECT_CONFIG;
			break;
		}
		if (ret != OK) {
			goto out;
		}

		assert((me->access & me->parent->access) == me->access);

		ret = trigger_memextent_activate_derive_event(me->type, me);
	} else {
		if (me->size == 0U) {
			ret = ERROR_OBJECT_CONFIG;
			goto out;
		}

		// memextent should have been zero initialized
		for (index_t i = 0; i < util_array_size(me->mappings); i++) {
			assert(me->mappings[i].addrspace == NULL);
		}

		partition_t *partition = me->header.partition;

		partition_t *hyp_partition = partition_get_private();

		if (me->device_mem) {
			assert(me->memtype == MEMEXTENT_MEMTYPE_DEVICE);

			ret = memdb_insert(hyp_partition, me->phys_base,
					   me->phys_base + (me->size - 1U),
					   (uintptr_t)me, MEMDB_TYPE_EXTENT);
		} else {
			ret = memdb_update(hyp_partition, me->phys_base,
					   me->phys_base + (me->size - 1U),
					   (uintptr_t)me, MEMDB_TYPE_EXTENT,
					   (uintptr_t)partition,
					   MEMDB_TYPE_PARTITION);

			if (ret == ERROR_MEMDB_NOT_OWNER) {
				// We might have failed to take ownership
				// because a previously deleted memextent has
				// not yet been cleaned up, so wait for an RCU
				// grace period and then retry. If it still
				// fails after that, there's a real conflict.
				rcu_sync();
				ret = memdb_update(
					hyp_partition, me->phys_base,
					me->phys_base + (me->size - 1U),
					(uintptr_t)me, MEMDB_TYPE_EXTENT,
					(uintptr_t)partition,
					MEMDB_TYPE_PARTITION);
			}
		}
	}

	if (ret == OK) {
		me->active = true;
	}
out:
	return ret;
}

error_t
memextent_map(memextent_t *extent, addrspace_t *addrspace, vmaddr_t vm_base,
	      memextent_mapping_attrs_t map_attrs)
{
	error_t ret;

	pgtable_access_t access_user =
		memextent_mapping_attrs_get_user_access(&map_attrs);
	pgtable_access_t access_kernel =
		memextent_mapping_attrs_get_kernel_access(&map_attrs);
	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&map_attrs);

	// Check validity of access rights and memtype

	if (((access_user & ~extent->access) != 0U) ||
	    ((access_kernel & ~extent->access) != 0U)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (!memextent_check_memtype(extent->memtype, memtype)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace->vm_read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_map_event(
			extent->type, extent, addrspace, vm_base, map_attrs);
	}

out:
	return ret;
}

error_t
memextent_unmap(memextent_t *extent, addrspace_t *addrspace, vmaddr_t vm_base)
{
	error_t ret;

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (addrspace->vm_read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_unmap_event(extent->type, extent,
						    addrspace, vm_base);
	}

out:
	return ret;
}

void
memextent_unmap_all(memextent_t *extent)
{
	trigger_memextent_unmap_all_event(extent->type, extent);
}

error_t
memextent_update_access(memextent_t *extent, addrspace_t *addrspace,
			vmaddr_t vm_base, memextent_access_attrs_t access_attrs)
{
	error_t ret;

	pgtable_access_t access_user =
		memextent_access_attrs_get_user_access(&access_attrs);
	pgtable_access_t access_kernel =
		memextent_access_attrs_get_kernel_access(&access_attrs);

	// Check validity of access rights
	if (((access_user & ~extent->access) != 0U) ||
	    ((access_kernel & ~extent->access) != 0U)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (addrspace->vm_read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_update_access_event(
			extent->type, extent, addrspace, vm_base, access_attrs);
	}

out:
	return ret;
}

void
memextent_handle_object_deactivate_memextent(memextent_t *memextent)
{
	trigger_memextent_deactivate_event(memextent->type, memextent);
}

void
memextent_handle_object_cleanup_memextent(memextent_t *memextent)
{
	trigger_memextent_cleanup_event(memextent->type, memextent);

	if (memextent->parent != NULL) {
		object_put_memextent(memextent->parent);
		memextent->parent = NULL;
	}
}

#if defined(ARCH_AARCH64_USE_S2FWB)
#if !defined(ARCH_ARM_8_4_S2FWB)
#error S2FWB requires ARCH_ARM_8_4_S2FWB
#endif
#error S2FWB support not implemented
#endif

// FIXME: move this to arch dependent code
bool
memextent_check_memtype(memextent_memtype_t  extent_type,
			pgtable_vm_memtype_t map_type)
{
	bool success = false;

	// Check valid type
	switch (map_type) {
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE:
	case PGTABLE_VM_MEMTYPE_DEVICE_NGNRE:
	case PGTABLE_VM_MEMTYPE_DEVICE_NGRE:
	case PGTABLE_VM_MEMTYPE_DEVICE_GRE:
		if ((extent_type == MEMEXTENT_MEMTYPE_ANY) ||
		    (extent_type == MEMEXTENT_MEMTYPE_DEVICE) ||
		    (extent_type == MEMEXTENT_MEMTYPE_UNCACHED)) {
			success = true;
		}
		break;
	case PGTABLE_VM_MEMTYPE_NORMAL_NC:
		if ((extent_type == MEMEXTENT_MEMTYPE_ANY) ||
		    (extent_type == MEMEXTENT_MEMTYPE_UNCACHED)) {
			success = true;
		}
		break;
	case PGTABLE_VM_MEMTYPE_NORMAL_WB:
#if defined(ARCH_AARCH64_USE_S2FWB)
		if ((extent_type == MEMEXTENT_MEMTYPE_ANY) ||
		    (extent_type == MEMEXTENT_MEMTYPE_CACHED)) {
#else
		if (extent_type == MEMEXTENT_MEMTYPE_ANY) {
#endif
			success = true;
		}
		break;
	case PGTABLE_VM_MEMTYPE_NORMAL_WT:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_INC:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWB_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWT:
	case PGTABLE_VM_MEMTYPE_NORMAL_ONC_IWB:
	case PGTABLE_VM_MEMTYPE_NORMAL_OWT_INC:
		if (extent_type == MEMEXTENT_MEMTYPE_ANY) {
			success = true;
		}
		break;
	default:
		success = false;
	}

	return success;
}

memextent_ptr_result_t
memextent_derive(memextent_t *parent, paddr_t offset, size_t size,
		 memextent_memtype_t memtype, pgtable_access_t access)
{
	memextent_create_t     params_me = { .memextent		   = NULL,
					     .memextent_device_mem = false };
	memextent_ptr_result_t me_ret;
	me_ret = partition_allocate_memextent(parent->header.partition,
					      params_me);
	if (me_ret.e != OK) {
		goto out;
	}

	memextent_t *	  me	= me_ret.r;
	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_access(&attrs, access);
	memextent_attrs_set_memtype(&attrs, memtype);

	spinlock_acquire(&me->header.lock);

	me_ret.e = memextent_configure_derive(me, parent, offset, size, attrs);
	if (me_ret.e != OK) {
		spinlock_release(&me->header.lock);
		object_put_memextent(me);
		goto out;
	}
	spinlock_release(&me->header.lock);

	me_ret.e = object_activate_memextent(me);
	if (me_ret.e != OK) {
		object_put_memextent(me);
	}

out:
	return me_ret;
}
