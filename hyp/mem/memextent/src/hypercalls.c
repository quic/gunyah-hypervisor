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
hypercall_memextent_modify(cap_id_t		    memextent_cap,
			   memextent_modify_flags_t flags, size_t offset,
			   size_t size)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	// FIXME:
	if (memextent_modify_flags_get_res_0(&flags) != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(m.e != OK)) {
		err = m.e;
		goto out;
	}

	memextent_t *memextent = m.r;
	bool	     need_sync = !memextent_modify_flags_get_no_sync(&flags);

	memextent_modify_op_t op = memextent_modify_flags_get_op(&flags);
	if (op == MEMEXTENT_MODIFY_OP_UNMAP_ALL) {
		memextent_unmap_all(memextent);
	} else if ((op == MEMEXTENT_MODIFY_OP_ZERO_RANGE) && !need_sync) {
		err = memextent_zero_range(memextent, offset, size);
	} else if ((op == MEMEXTENT_MODIFY_OP_CACHE_CLEAN_RANGE) &&
		   !need_sync) {
		err = memextent_cache_clean_range(memextent, offset, size);
	} else if ((op == MEMEXTENT_MODIFY_OP_CACHE_FLUSH_RANGE) &&
		   !need_sync) {
		err = memextent_cache_flush_range(memextent, offset, size);
	} else if (op == MEMEXTENT_MODIFY_OP_SYNC_ALL) {
		err = need_sync ? OK : ERROR_ARGUMENT_INVALID;
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	if ((err == OK) && need_sync) {
		// Wait for completion of EL2 operations using manual lookups
		rcu_sync();
	}

	object_put_memextent(memextent);
out:
	return err;
}

error_t
hypercall_memextent_configure(cap_id_t memextent_cap, paddr_t phys_base,
			      size_t size, memextent_attrs_t attributes)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
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
	cspace_t     *cspace = cspace_get_self();
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

static error_t
hypercall_memextent_donate_child(cap_id_t parent_cap, cap_id_t child_cap,
				 size_t offset, size_t size, bool reverse)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	memextent_ptr_result_t child = cspace_lookup_memextent(
		cspace, child_cap, CAP_RIGHTS_MEMEXTENT_DONATE);
	if (compiler_unexpected(child.e != OK)) {
		err = child.e;
		goto out;
	}

	// We don't actually need a reference to the parent for the donate; the
	// child already has a reference. So after sanity checking the provided
	// parent cap we can immediately drop the reference.
	if (child.r->parent != NULL) {
		memextent_ptr_result_t m = cspace_lookup_memextent(
			cspace, parent_cap, CAP_RIGHTS_MEMEXTENT_DONATE);
		if (compiler_unexpected(m.e != OK)) {
			err = m.e;
			goto out_child_release;
		}

		if (child.r->parent != m.r) {
			err = ERROR_ARGUMENT_INVALID;
		}

		object_put_memextent(m.r);
	} else {
		partition_ptr_result_t p = cspace_lookup_partition(
			cspace, parent_cap, CAP_RIGHTS_PARTITION_DONATE);
		if (compiler_unexpected(p.e != OK)) {
			err = p.e;
			goto out_child_release;
		}

		if (child.r->header.partition != p.r) {
			err = ERROR_ARGUMENT_INVALID;
		}

		object_put_partition(p.r);
	}

	if (err == OK) {
		err = memextent_donate_child(child.r, offset, size, reverse);
	}

out_child_release:
	object_put_memextent(child.r);
out:
	return err;
}

static error_t
hypercall_memextent_donate_sibling(cap_id_t from, cap_id_t to, size_t offset,
				   size_t size)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	memextent_ptr_result_t m1 = cspace_lookup_memextent(
		cspace, from, CAP_RIGHTS_MEMEXTENT_DONATE);
	if (compiler_unexpected(m1.e != OK)) {
		err = m1.e;
		goto out;
	}

	memextent_ptr_result_t m2 = cspace_lookup_memextent(
		cspace, to, CAP_RIGHTS_MEMEXTENT_DONATE);
	if (compiler_unexpected(m2.e != OK)) {
		err = m2.e;
		goto out_m1_release;
	}

	err = memextent_donate_sibling(m1.r, m2.r, offset, size);

	object_put_memextent(m2.r);
out_m1_release:
	object_put_memextent(m1.r);
out:
	return err;
}

error_t
hypercall_memextent_donate(memextent_donate_options_t options, cap_id_t from,
			   cap_id_t to, size_t offset, size_t size)
{
	error_t err;

	if (memextent_donate_options_get_res_0(&options) != 0U) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	memextent_donate_type_t type =
		memextent_donate_options_get_type(&options);
	if (type == MEMEXTENT_DONATE_TYPE_TO_CHILD) {
		err = hypercall_memextent_donate_child(from, to, offset, size,
						       false);
	} else if (type == MEMEXTENT_DONATE_TYPE_TO_PARENT) {
		err = hypercall_memextent_donate_child(to, from, offset, size,
						       true);
	} else if (type == MEMEXTENT_DONATE_TYPE_TO_SIBLING) {
		err = hypercall_memextent_donate_sibling(from, to, offset,
							 size);
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	if ((err == OK) && !memextent_donate_options_get_no_sync(&options)) {
		// The donation may have caused addrspace mappings to change.
		// Wait for completion of EL2 operations using manual lookups.
		rcu_sync();
	}

out:
	return err;
}

#else
extern int unused;
#endif
