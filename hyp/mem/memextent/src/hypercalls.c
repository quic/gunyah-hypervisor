// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(HYPERCALLS)
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

error_t
hypercall_memextent_unmap_all(cap_id_t memextent_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		err = m.e;
		goto out;
	}

	memextent_t *memextent = m.r;

	memextent_unmap_all(memextent);
	// Wait for completion of EL2 operations using manual lookups
	rcu_sync();

	object_put_memextent(memextent);

out:
	return err;
}

error_t
hypercall_memextent_configure(cap_id_t memextent_cap, paddr_t phys_base,
			      size_t size, memextent_attrs_t attributes)
{
	error_t	      err;
	cspace_t *    cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, memextent_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}
	if (type != OBJECT_TYPE_MEMEXTENT) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_memextent_release;
	}

	memextent_t *target_me = o.r.memextent;

	spinlock_acquire(&target_me->header.lock);

	if (atomic_load_relaxed(&target_me->header.state) ==
	    OBJECT_STATE_INIT) {
		err = memextent_configure(target_me, phys_base, size,
					  attributes);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&target_me->header.lock);
out_memextent_release:
	object_put(type, o.r);
out:
	return err;
}

error_t
hypercall_memextent_configure_derive(cap_id_t memextent_cap,
				     cap_id_t parent_memextent_cap,
				     size_t offset, size_t size,
				     memextent_attrs_t attributes)
{
	error_t	      err;
	cspace_t *    cspace = cspace_get_self();
	object_type_t type;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, parent_memextent_cap, CAP_RIGHTS_MEMEXTENT_DERIVE);
	if (compiler_unexpected(m.e != OK)) {
		err = m.e;
		goto out;
	}

	memextent_t *parent = m.r;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, memextent_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_parent_release;
	}
	if (type != OBJECT_TYPE_MEMEXTENT) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_memextent_release;
	}

	memextent_t *target_me = o.r.memextent;

	spinlock_acquire(&target_me->header.lock);

	if (atomic_load_relaxed(&target_me->header.state) ==
	    OBJECT_STATE_INIT) {
		err = memextent_configure_derive(target_me, parent, offset,
						 size, attributes);

	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&target_me->header.lock);
out_memextent_release:
	object_put(type, o.r);
out_parent_release:
	object_put_memextent(parent);
out:
	return err;
}
#else
extern int unused;
#endif
