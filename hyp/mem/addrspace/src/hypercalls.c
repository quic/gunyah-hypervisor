// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <memextent.h>
#include <object.h>
#include <pgtable.h>
#include <rcu.h>
#include <spinlock.h>

#include "addrspace.h"
#include "events/addrspace.h"

error_t
hypercall_addrspace_attach_thread(cap_id_t addrspace_cap, cap_id_t thread_cap)
{
	error_t	      ret;
	cspace_t	 *cspace = cspace_get_self();
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
hypercall_addrspace_map(cap_id_t addrspace_cap, cap_id_t memextent_cap,
			vmaddr_t vbase, memextent_mapping_attrs_t map_attrs)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	if (memextent_mapping_attrs_get_res_0(&map_attrs) != 0U) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	ret = memextent_map(memextent, addrspace, vbase, map_attrs);
	if (ret == OK) {
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
			  vmaddr_t vbase)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	ret = memextent_unmap(memextent, addrspace, vbase);
	if (ret == OK) {
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
				  memextent_access_attrs_t access_attrs)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	if (memextent_access_attrs_get_res_0(&access_attrs) != 0U) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	addrspace_ptr_result_t c = cspace_lookup_addrspace(
		cspace, addrspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}

	addrspace_t *addrspace = c.r;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		ret = m.e;
		goto out_addrspace_release;
	}

	memextent_t *memextent = m.r;

	ret = memextent_update_access(memextent, addrspace, vbase,
				      access_attrs);
	if (ret == OK) {
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
hypercall_addrspace_configure(cap_id_t addrspace_cap, vmid_t vmid)
{
	error_t	      err;
	cspace_t	 *cspace = cspace_get_self();
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
