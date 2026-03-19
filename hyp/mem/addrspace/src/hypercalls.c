// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>

#include "addrspace.h"
#include "events/addrspace.h"
#include "useraccess.h"

error_t
hypercall_addrspace_attach_thread(cap_id_t addrspace_cap, cap_id_t thread_cap)
{
	error_t	      ret;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, thread_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		ret = o.e;
		goto out;
	}

	if (type != OBJECT_TYPE_THREAD) {
		ret = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_thread_release;
	}

	thread_t *thread = o.r.thread;

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_ATTACH);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out_thread_release;
	}

	addrspace_t *addrspace = c.r;

	spinlock_acquire(&thread->header.lock);

	if (atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT) {
		ret = addrspace_attach_thread(addrspace, thread);
	} else {
		ret = ERROR_OBJECT_STATE;
	}

	spinlock_release(&thread->header.lock);

	object_put_addrspace(addrspace);

out_thread_release:
	object_put(type, o.r);
out:
	return ret;
}

error_t
hypercall_addrspace_attach_vdma(cap_id_t addrspace_cap, cap_id_t dma_device_cap,
				index_t index)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	addrspace_ptr_result_t addrspace_r = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_ATTACH);
	if (compiler_unexpected(addrspace_r.e != OK)) {
		err = addrspace_r.e;
		goto out;
	}

	err = trigger_addrspace_attach_vdma_event(addrspace_r.r, dma_device_cap,
						  index);

	object_put_addrspace(addrspace_r.r);
out:
	return err;
}

error_t
hypercall_addrspace_attach_vdevice(cap_id_t addrspace_cap, cap_id_t vdevice_cap,
				   index_t index, vmaddr_t vbase, size_t size,
				   addrspace_attach_vdevice_flags_t flags)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	addrspace_ptr_result_t addrspace_r = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(addrspace_r.e != OK)) {
		err = addrspace_r.e;
		goto out;
	}

	err = trigger_addrspace_attach_vdevice_event(addrspace_r.r, vdevice_cap,
						     index, vbase, size, flags);

	object_put_addrspace(addrspace_r.r);
out:
	return err;
}

error_t
hypercall_addrspace_map(cap_id_t addrspace_cap, cap_id_t memextent_cap,
			vmaddr_t vbase, memextent_mapping_attrs_t map_attrs,
			addrspace_map_flags_t map_flags, size_t offset,
			size_t size)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	// Ensure that no unknown or unsupported flags are set
	addrspace_map_flags_t checked_flags = addrspace_map_flags_default();
	addrspace_map_flags_copy_private(&checked_flags, &map_flags);
	addrspace_map_flags_copy_vmmio(&checked_flags, &map_flags);
	addrspace_map_flags_copy_partial(&checked_flags, &map_flags);
	addrspace_map_flags_copy_no_sync(&checked_flags, &map_flags);

	if ((memextent_mapping_attrs_get_res_0(&map_attrs) != 0U) ||
	    !addrspace_map_flags_is_clean(map_flags) ||
	    !addrspace_map_flags_is_equal(map_flags, checked_flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace_map_flags_get_private(&map_flags) &&
	    addrspace_map_flags_get_vmmio(&map_flags)) {
		// These flags are mutually exclusive.
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap,
		(addrspace_map_flags_get_private(&map_flags) ||
		 addrspace_map_flags_get_vmmio(&map_flags))
			? CAP_RIGHTS_ADDRSPACE_MAP_PROTECTED
			: CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap,
		addrspace_map_flags_get_private(&map_flags)
			? CAP_RIGHTS_MEMEXTENT_MAP_PRIVATE
			: CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	if (addrspace_map_flags_get_partial(&map_flags)) {
		ret = memextent_map_partial(memextent, addrspace, vbase, offset,
					    size, map_attrs, map_flags);
	} else {
		ret = memextent_map(memextent, addrspace, vbase, map_attrs,
				    map_flags);
	}

	if ((ret == OK) && !addrspace_map_flags_get_no_sync(&map_flags)) {
		// Wait for completion of EL2 operations using manual lookups
		rcu_sync();
	}

	object_put_memextent(memextent);
out_addrspace_release:
	object_put_addrspace(addrspace);
out:
	return ret;
}

error_t
hypercall_addrspace_unmap(cap_id_t addrspace_cap, cap_id_t memextent_cap,
			  vmaddr_t vbase, addrspace_map_flags_t map_flags,
			  size_t offset, size_t size)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	// Ensure that no unknown or unsupported flags are set
	addrspace_map_flags_t checked_flags = addrspace_map_flags_default();
	addrspace_map_flags_copy_private(&checked_flags, &map_flags);
	addrspace_map_flags_copy_vmmio(&checked_flags, &map_flags);
	addrspace_map_flags_copy_partial(&checked_flags, &map_flags);
	addrspace_map_flags_copy_whole_extent(&checked_flags, &map_flags);
	addrspace_map_flags_copy_no_sync(&checked_flags, &map_flags);

	if (!addrspace_map_flags_is_clean(map_flags) ||
	    !addrspace_map_flags_is_equal(map_flags, checked_flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace_map_flags_get_private(&map_flags) &&
	    addrspace_map_flags_get_vmmio(&map_flags)) {
		// These flags are mutually exclusive.
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace_map_flags_get_partial(&map_flags) &&
	    addrspace_map_flags_get_whole_extent(&map_flags)) {
		// These flags are mutually exclusive.
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap,
		(addrspace_map_flags_get_private(&map_flags) ||
		 addrspace_map_flags_get_vmmio(&map_flags))
			? CAP_RIGHTS_ADDRSPACE_MAP_PROTECTED
			: CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap,
		addrspace_map_flags_get_private(&map_flags)
			? CAP_RIGHTS_MEMEXTENT_MAP_PRIVATE
			: CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	if (addrspace_map_flags_get_partial(&map_flags)) {
		ret = memextent_unmap_partial(memextent, addrspace, vbase,
					      offset, size, map_flags);
	} else if (addrspace_map_flags_get_whole_extent(&map_flags)) {
		ret = memextent_unmap_whole_extent(memextent, addrspace,
						   map_flags);
	} else {
		ret = memextent_unmap(memextent, addrspace, vbase, map_flags);
	}

	if ((ret == OK) && !addrspace_map_flags_get_no_sync(&map_flags)) {
		// Wait for completion of EL2 operations using manual lookups
		rcu_sync();
	}

	object_put_memextent(memextent);
out_addrspace_release:
	object_put_addrspace(addrspace);
out:
	return ret;
}

error_t
hypercall_addrspace_update_access(cap_id_t addrspace_cap,
				  cap_id_t memextent_cap, vmaddr_t vbase,
				  memextent_access_attrs_t access_attrs,
				  addrspace_map_flags_t	   map_flags,
				  size_t offset, size_t size)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	// Ensure that no unknown or unsupported flags are set
	addrspace_map_flags_t checked_flags = addrspace_map_flags_default();
	addrspace_map_flags_copy_private(&checked_flags, &map_flags);
	addrspace_map_flags_copy_vmmio(&checked_flags, &map_flags);
	addrspace_map_flags_copy_partial(&checked_flags, &map_flags);
	addrspace_map_flags_copy_no_sync(&checked_flags, &map_flags);

	if ((memextent_access_attrs_get_res_0(&access_attrs) != 0U) ||
	    !addrspace_map_flags_is_clean(map_flags) ||
	    !addrspace_map_flags_is_equal(map_flags, checked_flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (addrspace_map_flags_get_private(&map_flags) &&
	    addrspace_map_flags_get_vmmio(&map_flags)) {
		// These flags are mutually exclusive.
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap,
		(addrspace_map_flags_get_private(&map_flags) ||
		 addrspace_map_flags_get_vmmio(&map_flags))
			? CAP_RIGHTS_ADDRSPACE_MAP_PROTECTED
			: CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap,
		addrspace_map_flags_get_private(&map_flags)
			? CAP_RIGHTS_MEMEXTENT_MAP_PRIVATE
			: CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	if (addrspace_map_flags_get_partial(&map_flags)) {
		ret = memextent_update_access_partial(memextent, addrspace,
						      vbase, offset, size,
						      access_attrs, map_flags);
	} else {
		ret = memextent_update_access(memextent, addrspace, vbase,
					      access_attrs, map_flags);
	}

	if ((ret == OK) && !addrspace_map_flags_get_no_sync(&map_flags)) {
		// Wait for completion of EL2 operations using manual lookups
		rcu_sync();
	}

	object_put_memextent(memextent);
out_addrspace_release:
	object_put_addrspace(addrspace);
out:
	return ret;
}

hypercall_addrspace_modify_pages_result_t
hypercall_addrspace_modify_pages(cap_id_t addrspace_cap, vmaddr_t vbase,
				 size_t				size,
				 addrspace_modify_pages_flags_t flags)
{
	hypercall_addrspace_modify_pages_result_t ret;
	cspace_t				 *cspace = cspace_get_self();

	if (!addrspace_modify_pages_flags_is_clean(flags)) {
		ret = (hypercall_addrspace_modify_pages_result_t){
			.error		= ERROR_ARGUMENT_INVALID,
			.size_remaining = size,
		};
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MODIFY_PROTECTED);
	if (compiler_unexpected(c.e != OK)) {
		ret = (hypercall_addrspace_modify_pages_result_t){
			.error		= c.e,
			.size_remaining = size,
		};
		goto out;
	}

	size_result_t remaining_r =
		addrspace_modify_pages(c.r, vbase, size, flags);

	assert(remaining_r.r <= size);
	ret = (hypercall_addrspace_modify_pages_result_t){
		.error		= remaining_r.e,
		.size_remaining = remaining_r.r,
	};

	object_put_addrspace(c.r);
out:
	return ret;
}

error_t
hypercall_addrspace_configure(cap_id_t addrspace_cap, vmid_t vmid)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, addrspace_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}
	if (type != OBJECT_TYPE_ADDRSPACE) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_addrspace_release;
	}

	addrspace_t *target_as = o.r.addrspace;

	spinlock_acquire(&target_as->header.lock);

	if (atomic_load_relaxed(&target_as->header.state) ==
	    OBJECT_STATE_INIT) {
		err = addrspace_configure(target_as, vmid);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&target_as->header.lock);
out_addrspace_release:
	object_put(type, o.r);
out:
	return err;
}

hypercall_addrspace_lookup_result_t
hypercall_addrspace_lookup(cap_id_t addrspace_cap, cap_id_t memextent_cap,
			   vmaddr_t vbase, size_t size)
{
	hypercall_addrspace_lookup_result_t ret	   = { .error = OK };
	cspace_t			   *cspace = cspace_get_self();

	addrspace_ptr_result_t a = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_LOOKUP);
	if (compiler_unexpected(a.e != OK)) {
		ret.error = a.e;
		goto out;
	}

	addrspace_t *addrspace = a.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_LOOKUP);
	if (compiler_unexpected(m.e != OK)) {
		ret.error = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	addrspace_lookup_result_t lookup_ret =
		addrspace_lookup(addrspace, vbase, size);
	if (lookup_ret.e != OK) {
		ret.error = lookup_ret.e;
		goto out_memextent_release;
	}

	// Determine if the memextent owns the range of memory returned by the
	// lookup.
	paddr_t phys_start = lookup_ret.r.phys;
	paddr_t phys_end   = phys_start + (lookup_ret.r.size - 1U);
	if (!memdb_is_ownership_contiguous(phys_start, phys_end,
					   (uintptr_t)memextent,
					   MEMDB_TYPE_EXTENT)) {
		ret.error = ERROR_MEMDB_NOT_OWNER;
		goto out_memextent_release;
	}

	assert((phys_start >= memextent->phys_base) &&
	       (phys_end <= (memextent->phys_base + (memextent->size - 1U))));

	memextent_mapping_attrs_t map_attrs = memextent_mapping_attrs_default();
	memextent_mapping_attrs_set_memtype(&map_attrs, lookup_ret.r.memtype);
	memextent_mapping_attrs_set_user_access(&map_attrs,
						lookup_ret.r.user_access);
	memextent_mapping_attrs_set_kernel_access(&map_attrs,
						  lookup_ret.r.kernel_access);

	ret.offset    = phys_start - memextent->phys_base;
	ret.size      = lookup_ret.r.size;
	ret.map_attrs = map_attrs;

out_memextent_release:
	object_put_memextent(memextent);
out_addrspace_release:
	object_put_addrspace(addrspace);
out:
	return ret;
}

error_t
hypercall_addrspace_configure_info_area(cap_id_t addrspace_cap,
					cap_id_t info_area_me_cap, vmaddr_t ipa)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, addrspace_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_bad_cap;
	}
	if (type != OBJECT_TYPE_ADDRSPACE) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_addrspace_release;
	}
	addrspace_t *target_as = o.r.addrspace;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, info_area_me_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
	if (compiler_unexpected(m.e != OK)) {
		err = m.e;
		goto out_addrspace_release;
	}
	memextent_t *info_area_me = m.r;

	spinlock_acquire(&target_as->header.lock);
	if (atomic_load_relaxed(&target_as->header.state) ==
	    OBJECT_STATE_INIT) {
		err = addrspace_configure_info_area(target_as, info_area_me,
						    ipa);
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&target_as->header.lock);

	object_put_memextent(info_area_me);
out_addrspace_release:
	object_put(type, o.r);
out_bad_cap:
	return err;
}

hypercall_addrspace_find_info_area_result_t
hypercall_addrspace_find_info_area(void)
{
	thread_t				   *current = thread_get_self();
	hypercall_addrspace_find_info_area_result_t ret;

	if (current->addrspace->info_area.me != NULL) {
		ret = (hypercall_addrspace_find_info_area_result_t){
			.base  = current->addrspace->info_area.ipa,
			.size  = current->addrspace->info_area.me->size,
			.error = OK,
		};
	} else {
		ret = (hypercall_addrspace_find_info_area_result_t){
			.error = ERROR_ADDR_INVALID,
		};
	}

	return ret;
}

hypercall_addrspace_info_area_add_entry_result_t
hypercall_addrspace_info_area_add_entry(
	cap_id_t addrspace_cap, addrspace_info_area_entry_type_t type,
	user_ptr_t data, addrspace_info_area_entry_data_info_t data_info)
{
	hypercall_addrspace_info_area_add_entry_result_t ret;

	addrspace_ptr_result_t o =
		cspace_lookup_addrspace(cspace_get_self(), addrspace_cap,
					CAP_RIGHTS_ADDRSPACE_ADD_INFO);
	if (compiler_unexpected(o.e != OK)) {
		ret = (hypercall_addrspace_info_area_add_entry_result_t){
			.error = o.e,
		};
		goto out_lookup;
	}
	addrspace_t *addrspace = o.r;

	switch (addrspace_info_area_entry_type_get_owner(&type)) {
	case ADDRSPACE_INFO_AREA_ID_OWNER_ROOTVM:
	case ADDRSPACE_INFO_AREA_ID_OWNER_RM:
	case ADDRSPACE_INFO_AREA_ID_OWNER_QCRM:
	case ADDRSPACE_INFO_AREA_ID_OWNER_DEV:
		break;
	case ADDRSPACE_INFO_AREA_ID_OWNER_INVALID:
	case ADDRSPACE_INFO_AREA_ID_OWNER_GUNYAH:
	default:
		// Reserved use
		ret = (hypercall_addrspace_info_area_add_entry_result_t){
			.error = ERROR_ARGUMENT_INVALID,
		};
		goto out_ref;
	}

	size_t size = addrspace_info_area_entry_data_info_get_size(&data_info);
	size_t alignment =
		addrspace_info_area_entry_data_info_get_alignment(&data_info);

	addrspace_alloc_info_area_result_t alloc_r =
		addrspace_alloc_info_area(addrspace, size, alignment, true);
	if (alloc_r.e != OK) {
		ret = (hypercall_addrspace_info_area_add_entry_result_t){
			.error = alloc_r.e,
		};
		goto out_ref;
	}

	error_t err = useraccess_copy_from_guest_va((void *)alloc_r.ptr, size,
						    (gvaddr_t)data, size)
			      .e;
	if (err != OK) {
		ret = (hypercall_addrspace_info_area_add_entry_result_t){
			.error = err,
		};
		goto out_ref;
	}

	addrspace_enable_info_area(alloc_r.descriptor, type);

	ret = (hypercall_addrspace_info_area_add_entry_result_t){
		.error = OK,
		.ipa   = alloc_r.ipa,
	};
out_ref:
	object_put_addrspace(addrspace);
out_lookup:
	return ret;
}

hypercall_addrspace_info_area_get_entry_result_t
hypercall_addrspace_info_area_get_entry(addrspace_info_area_entry_type_t type,
					user_ptr_t buf, size_t buf_size)
{
	hypercall_addrspace_info_area_get_entry_result_t ret;

	addrspace_t *addrspace = thread_get_self()->addrspace;

	addrspace_info_area_id_owner_t owner =
		addrspace_info_area_entry_type_get_owner(&type);
	bool by_index = (uint32_t)owner == ADDRSPACE_INFO_AREA_ID_OWNER_INDEX;

	addrspace_info_area_entry_result_t entry_r;

	if (by_index) {
		entry_r = addrspace_info_area_get_by_index(
			addrspace,
			addrspace_info_area_entry_type_get_id(&type));
	} else {
		entry_r = addrspace_info_area_get_by_type(addrspace, type);
	}
	if (entry_r.e != OK) {
		ret = (hypercall_addrspace_info_area_get_entry_result_t){
			.error = entry_r.e
		};
		goto out;
	}
	addrspace_info_area_entry_t entry = entry_r.r;

	uintptr_t data =
		addrspace_info_area_get_data_addr(addrspace, entry.offset);

	if (buf_size < entry.size) {
		ret = (hypercall_addrspace_info_area_get_entry_result_t){
			.error = ERROR_ARGUMENT_SIZE, .size = entry.size
		};
		goto out;
	}

	error_t err = useraccess_copy_to_guest_va((gvaddr_t)buf, buf_size,
						  (void *)data, entry.size,
						  false)
			      .e;
	if (err != OK) {
		ret = (hypercall_addrspace_info_area_get_entry_result_t){
			.error = err, .size = entry.size
		};
		goto out;
	}

	ret = (hypercall_addrspace_info_area_get_entry_result_t){
		.error = OK,
		.size  = entry.size,
		.type  = entry.type,
	};
out:
	return ret;
}

error_t
hypercall_addrspace_configure_range(cap_id_t addrspace_cap, vmaddr_t vbase,
				    size_t			   size,
				    addrspace_range_configure_op_t op)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	addrspace_ptr_result_t o = cspace_lookup_addrspace_any(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_CONFIGURE_RANGE);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}

	addrspace_t *target_as = o.r;

	object_state_t state = atomic_load_relaxed(&target_as->header.state);
	if ((state != OBJECT_STATE_INIT) && (state != OBJECT_STATE_ACTIVE)) {
		err = ERROR_OBJECT_STATE;
		goto out_ref;
	}

	switch (op) {
	case ADDRSPACE_RANGE_CONFIGURE_OP_ADD_VMMIO:
		err = addrspace_add_fault_range(target_as, vbase, size, true);
		break;
	case ADDRSPACE_RANGE_CONFIGURE_OP_REMOVE_VMMIO:
		err = addrspace_remove_fault_range(target_as, vbase, size,
						   true);
		break;
	case ADDRSPACE_RANGE_CONFIGURE_OP_ADD_PRIVATE:
		err = addrspace_add_fault_range(target_as, vbase, size, false);
		break;
	case ADDRSPACE_RANGE_CONFIGURE_OP_REMOVE_PRIVATE:
		err = addrspace_remove_fault_range(target_as, vbase, size,
						   false);
		break;
	default:
		err = ERROR_UNIMPLEMENTED;
		break;
	}

out_ref:
	object_put_addrspace(o.r);
out:
	return err;
}

error_t
hypercall_addrspace_trigger_msi(cap_id_t addrspace_cap, vmaddr_t mailbox,
				uint32_t message, index_t device_id)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	addrspace_ptr_result_t o = cspace_lookup_addrspace_any(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_TRIGGER_MSI);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}

	addrspace_t *as = o.r;

	// Note: we trigger the event directly here instead of calling the
	// addrspace_trigger_msi() wrapper because the event does not trust its
	// input, but the wrapper does: it will fall back to accessing memory
	// if the event fails and the mailbox has a S2 mapping.
	err = trigger_addrspace_trigger_msi_event(as, mailbox, message,
						  device_id);

	object_put_addrspace(as);
out:
	return err;
}
