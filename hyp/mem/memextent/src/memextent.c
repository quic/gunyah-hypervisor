// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <bitmap.h>
#include <compiler.h>
#include <list.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>
#include <util.h>

#include <events/memextent.h>
#include <events/object.h>

#include <asm/cache.h>
#include <asm/cpu.h>

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

static bool
memextent_validate_attrs(memextent_type_t type, memextent_memtype_t memtype,
			 pgtable_access_t access)
{
	bool ret = true;

	switch (type) {
	case MEMEXTENT_TYPE_BASIC:
	case MEMEXTENT_TYPE_SPARSE:
		break;
	default:
		ret = false;
		break;
	}

	if (!ret) {
		goto out;
	}

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
		ret = false;
		break;
	}

	if (!ret) {
		goto out;
	}

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
		ret = false;
		break;
	}

out:
	return ret;
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

	if ((memextent_attrs_get_res_0(&attributes) != 0U) ||
	    (memextent_attrs_get_append(&attributes))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	memextent_type_t    type    = memextent_attrs_get_type(&attributes);
	memextent_memtype_t memtype = memextent_attrs_get_memtype(&attributes);
	pgtable_access_t    access  = memextent_attrs_get_access(&attributes);
	if (!memextent_validate_attrs(type, memtype, access)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	me->type      = type;
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

// FIXME:
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

	if ((memextent_attrs_get_res_0(&attributes) != 0U) ||
	    (memextent_attrs_get_append(&attributes))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	memextent_type_t    type    = memextent_attrs_get_type(&attributes);
	memextent_memtype_t memtype = memextent_attrs_get_memtype(&attributes);
	pgtable_access_t    access  = memextent_attrs_get_access(&attributes);
	if (!memextent_validate_attrs(type, memtype, access)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!pgtable_access_check(parent->access, access)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if ((parent->memtype != MEMEXTENT_MEMTYPE_ANY) &&
	    (parent->memtype != memtype)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	paddr_t phys_base = parent->phys_base + offset;

	me->type      = type;
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

		ret = trigger_memextent_activate_event(me->type, me);
	}

	if (ret == OK) {
		me->active = true;
	}
out:
	return ret;
}

bool
memextent_supports_donation(memextent_t *me)
{
	return trigger_memextent_supports_donation_event(me->type, me);
}

static bool
extent_range_valid(memextent_t *me, paddr_t phys, size_t size)
{
	assert(!util_add_overflows(phys, size - 1U));

	return (me->phys_base <= phys) &&
	       ((me->phys_base + (me->size - 1U)) >= (phys + (size - 1U)));
}

error_t
memextent_donate_child(memextent_t *me, size_t offset, size_t size,
		       bool reverse)
{
	error_t ret;

	if (!util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (util_add_overflows(me->phys_base, offset)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	paddr_t phys = me->phys_base + offset;

	if ((size == 0U) || util_add_overflows(phys, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!extent_range_valid(me, phys, size)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ret = trigger_memextent_donate_child_event(me->type, me, phys, size,
						   reverse);

out:
	return ret;
}

error_t
memextent_donate_sibling(memextent_t *from, memextent_t *to, size_t offset,
			 size_t size)
{
	error_t ret;

	if (!util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (util_add_overflows(from->phys_base, offset)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	paddr_t phys = from->phys_base + offset;

	if ((size == 0U) || util_add_overflows(phys, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!extent_range_valid(from, phys, size) ||
	    !extent_range_valid(to, phys, size)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if ((from == to) || (from->parent == NULL) ||
	    (from->parent != to->parent)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ret = trigger_memextent_donate_sibling_event(from->type, from, to, phys,
						     size);

out:
	return ret;
}

static bool
memextent_check_map_attrs(memextent_t		   *extent,
			  memextent_mapping_attrs_t map_attrs)
{
	pgtable_access_t access_user =
		memextent_mapping_attrs_get_user_access(&map_attrs);
	pgtable_access_t access_kernel =
		memextent_mapping_attrs_get_kernel_access(&map_attrs);
	pgtable_vm_memtype_t memtype =
		memextent_mapping_attrs_get_memtype(&map_attrs);

	return (pgtable_access_check(extent->access, access_user)) &&
	       pgtable_access_check(extent->access, access_kernel) &&
	       memextent_check_memtype(extent->memtype, memtype);
}

error_t
memextent_map(memextent_t *extent, addrspace_t *addrspace, vmaddr_t vm_base,
	      memextent_mapping_attrs_t map_attrs)
{
	error_t ret;

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (!memextent_check_map_attrs(extent, map_attrs)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_map_event(
			extent->type, extent, addrspace, vm_base, map_attrs);
	}

out:
	return ret;
}

error_t
memextent_map_partial(memextent_t *extent, addrspace_t *addrspace,
		      vmaddr_t vm_base, size_t offset, size_t size,
		      memextent_mapping_attrs_t map_attrs)
{
	error_t ret;

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if ((size == 0U) || util_add_overflows(offset, size - 1U) ||
	    util_add_overflows(vm_base, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if ((offset + (size - 1U)) >= extent->size) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!memextent_check_map_attrs(extent, map_attrs)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_map_partial_event(extent->type, extent,
							  addrspace, vm_base,
							  offset, size,
							  map_attrs);
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

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_unmap_event(extent->type, extent,
						    addrspace, vm_base);
	}

out:
	return ret;
}

error_t
memextent_unmap_partial(memextent_t *extent, addrspace_t *addrspace,
			vmaddr_t vm_base, size_t offset, size_t size)
{
	error_t ret;

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if ((size == 0U) || util_add_overflows(offset, size - 1U) ||
	    util_add_overflows(vm_base, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if ((offset + (size - 1U)) >= extent->size) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_unmap_partial_event(
			extent->type, extent, addrspace, vm_base, offset, size);
	}

out:
	return ret;
}

void
memextent_unmap_all(memextent_t *extent)
{
	if (!trigger_memextent_unmap_all_event(extent->type, extent)) {
		panic("Invalid memory extent unmap all!");
	}
}

static error_t
memextent_do_clean(paddr_t base, size_t size, void *arg)
{
	memextent_clean_flags_t *flags = (memextent_clean_flags_t *)arg;
	assert(flags != NULL);

	void *addr = partition_phys_map(base, size);
	partition_phys_access_enable(addr);

	if (memextent_clean_flags_get_zero(flags)) {
		(void)memset_s(addr, size, 0, size);
	}

	if (memextent_clean_flags_get_flush(flags)) {
		CACHE_CLEAN_INVALIDATE_RANGE((uint8_t *)addr, size);
	} else {
		CACHE_CLEAN_RANGE((uint8_t *)addr, size);
	}

	partition_phys_access_disable(addr);
	partition_phys_unmap(addr, base, size);

	return OK;
}

static error_t
memextent_clean_range(memextent_t *extent, size_t offset, size_t size,
		      memextent_clean_flags_t flags)
{
	error_t err;

	if ((extent->memtype == MEMEXTENT_MEMTYPE_DEVICE) ||
	    !pgtable_access_check(extent->access, PGTABLE_ACCESS_W)) {
		err = ERROR_DENIED;
		goto out;
	}

	if (!util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (util_add_overflows(extent->phys_base, offset)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	paddr_t phys = extent->phys_base + offset;

	if ((size == 0U) || util_add_overflows(phys, size - 1U)) {
		err = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!extent_range_valid(extent, phys, size)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	err = memdb_range_walk((uintptr_t)extent, MEMDB_TYPE_EXTENT, phys,
			       phys + size - 1U, memextent_do_clean, &flags);

out:
	return err;
}

error_t
memextent_zero_range(memextent_t *extent, size_t offset, size_t size)
{
	memextent_clean_flags_t flags = memextent_clean_flags_default();
	memextent_clean_flags_set_zero(&flags, true);

	return memextent_clean_range(extent, offset, size, flags);
}

error_t
memextent_cache_clean_range(memextent_t *me, size_t offset, size_t size)
{
	return memextent_clean_range(me, offset, size,
				     memextent_clean_flags_default());
}

error_t
memextent_cache_flush_range(memextent_t *me, size_t offset, size_t size)
{
	memextent_clean_flags_t flags = memextent_clean_flags_default();
	memextent_clean_flags_set_flush(&flags, true);

	return memextent_clean_range(me, offset, size, flags);
}

static bool
memextent_check_access_attrs(memextent_t	     *extent,
			     memextent_access_attrs_t access_attrs)
{
	pgtable_access_t access_user =
		memextent_access_attrs_get_user_access(&access_attrs);
	pgtable_access_t access_kernel =
		memextent_access_attrs_get_kernel_access(&access_attrs);

	return (pgtable_access_check(extent->access, access_user) &&
		pgtable_access_check(extent->access, access_kernel));
}

error_t
memextent_update_access(memextent_t *extent, addrspace_t *addrspace,
			vmaddr_t vm_base, memextent_access_attrs_t access_attrs)
{
	error_t ret;

	if (!memextent_check_access_attrs(extent, access_attrs)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_update_access_event(
			extent->type, extent, addrspace, vm_base, access_attrs);
	}

out:
	return ret;
}

error_t
memextent_update_access_partial(memextent_t *extent, addrspace_t *addrspace,
				vmaddr_t vm_base, size_t offset, size_t size,
				memextent_access_attrs_t access_attrs)
{
	error_t ret;

	if (!memextent_check_access_attrs(extent, access_attrs)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (!util_is_baligned(vm_base, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(offset, PGTABLE_VM_PAGE_SIZE) ||
	    !util_is_baligned(size, PGTABLE_VM_PAGE_SIZE)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	if ((size == 0U) || util_add_overflows(offset, size - 1U) ||
	    util_add_overflows(vm_base, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if ((offset + (size - 1U)) >= extent->size) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (addrspace->read_only) {
		ret = ERROR_DENIED;
	} else {
		ret = trigger_memextent_update_access_partial_event(
			extent->type, extent, addrspace, vm_base, offset, size,
			access_attrs);
	}

out:
	return ret;
}

bool
memextent_is_mapped(memextent_t *me, addrspace_t *addrspace, bool exclusive)
{
	assert(me != NULL);
	assert(addrspace != NULL);

	return trigger_memextent_is_mapped_event(me->type, me, addrspace,
						 exclusive);
}

void
memextent_handle_object_deactivate_memextent(memextent_t *memextent)
{
	if (!trigger_memextent_deactivate_event(memextent->type, memextent)) {
		panic("Invalid memory extent deactivate!");
	}
}

void
memextent_handle_object_cleanup_memextent(memextent_t *memextent)
{
	if (!trigger_memextent_cleanup_event(memextent->type, memextent)) {
		panic("Invalid memory extent cleanup!");
	}

	if (memextent->parent != NULL) {
		object_put_memextent(memextent->parent);
		memextent->parent = NULL;
	}
}

size_result_t
memextent_get_offset_for_pa(memextent_t *memextent, paddr_t pa, size_t size)
{
	return trigger_memextent_get_offset_for_pa_event(memextent->type,
							 memextent, pa, size);
}

#if defined(ARCH_AARCH64_USE_S2FWB)
#if !defined(ARCH_ARM_FEAT_S2FWB)
#error S2FWB requires ARCH_ARM_FEAT_S2FWB
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
	case PGTABLE_VM_MEMTYPE_NORMAL_WB: {
#if defined(ARCH_AARCH64_USE_S2FWB)
		if ((extent_type == MEMEXTENT_MEMTYPE_ANY) ||
		    (extent_type == MEMEXTENT_MEMTYPE_CACHED))
#else
		if (extent_type == MEMEXTENT_MEMTYPE_ANY)
#endif
		{
			success = true;
		}
	} break;
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
		break;
	}

	return success;
}

memextent_ptr_result_t
memextent_derive(memextent_t *parent, paddr_t offset, size_t size,
		 memextent_memtype_t memtype, pgtable_access_t access,
		 memextent_type_t type)
{
	memextent_create_t     params_me = { .memextent		   = NULL,
					     .memextent_device_mem = false };
	memextent_ptr_result_t me_ret;
	me_ret = partition_allocate_memextent(parent->header.partition,
					      params_me);
	if (me_ret.e != OK) {
		goto out;
	}

	memextent_t	 *me	= me_ret.r;
	memextent_attrs_t attrs = memextent_attrs_default();
	memextent_attrs_set_access(&attrs, access);
	memextent_attrs_set_memtype(&attrs, memtype);
	memextent_attrs_set_type(&attrs, type);

	spinlock_acquire(&me->header.lock);

	me_ret.e = memextent_configure_derive(me, parent, offset, size, attrs);
	if (me_ret.e != OK) {
		spinlock_release(&me->header.lock);
		me_ret.r = NULL;
		object_put_memextent(me);
		goto out;
	}
	spinlock_release(&me->header.lock);

	me_ret.e = object_activate_memextent(me);
	if (me_ret.e != OK) {
		object_put_memextent(me);
		me_ret.r = NULL;
	}

out:
	return me_ret;
}

void
memextent_retain_mappings(memextent_t *me) LOCK_IMPL
{
	(void)trigger_memextent_retain_mappings_event(me->type, me);
}

void
memextent_release_mappings(memextent_t *me, bool clear) LOCK_IMPL
{
	(void)trigger_memextent_release_mappings_event(me->type, me, clear);
}

memextent_mapping_t
memextent_lookup_mapping(memextent_t *me, paddr_t phys, size_t size, index_t i)
{
	memextent_mapping_result_t ret;

	ret = trigger_memextent_lookup_mapping_event(me->type, me, phys, size,
						     i);
	assert(ret.e == OK);

	return ret.r;
}

error_t
memextent_attach(partition_t *owner, memextent_t *me, uintptr_t hyp_va,
		 size_t size)
{
	assert(owner != NULL);
	assert(me != NULL);

	error_t ret = OK;

	if (owner != me->header.partition) {
		ret = ERROR_DENIED;
		goto out;
	}

	if (!pgtable_access_check(me->access, PGTABLE_ACCESS_RW)) {
		ret = ERROR_DENIED;
		goto out;
	}

	if (me->size < size) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	pgtable_hyp_memtype_t memtype;
	switch (me->memtype) {
	case MEMEXTENT_MEMTYPE_CACHED:
	case MEMEXTENT_MEMTYPE_ANY:
		memtype = PGTABLE_HYP_MEMTYPE_WRITEBACK;
		break;
	case MEMEXTENT_MEMTYPE_DEVICE:
		memtype = PGTABLE_HYP_MEMTYPE_DEVICE;
		break;
	case MEMEXTENT_MEMTYPE_UNCACHED:
		memtype = PGTABLE_HYP_MEMTYPE_WRITECOMBINE;
		break;
	default:
		ret = ERROR_ARGUMENT_INVALID;
		break;
	}
	if (ret == ERROR_ARGUMENT_INVALID) {
		goto out;
	}

	ret = trigger_memextent_attach_event(me->type, me, hyp_va, size,
					     memtype);
out:
	return ret;
}

void
memextent_detach(partition_t *owner, memextent_t *me)
{
	assert(owner != NULL);
	assert(me != NULL);
	assert(owner == me->header.partition);

	bool handled = trigger_memextent_detach_event(me->type, me);
	assert(handled);
}
