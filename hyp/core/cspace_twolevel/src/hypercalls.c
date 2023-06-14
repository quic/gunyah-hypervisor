// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(HYPERCALLS)
#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <spinlock.h>

error_t
hypercall_cspace_delete_cap_from(cap_id_t cspace_cap, cap_id_t cap)
{
	error_t		    ret;
	cspace_ptr_result_t c;
	c = cspace_lookup_cspace(cspace_get_self(), cspace_cap,
				 CAP_RIGHTS_CSPACE_CAP_DELETE);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}
	cspace_t *cspace = c.r;

	ret = cspace_delete_cap(cspace, cap);

	object_put_cspace(cspace);
out:
	return ret;
}

hypercall_cspace_copy_cap_from_result_t
hypercall_cspace_copy_cap_from(cap_id_t src_cspace_cap, cap_id_t src_cap,
			       cap_id_t	    dest_cspace_cap,
			       cap_rights_t rights_mask)
{
	hypercall_cspace_copy_cap_from_result_t ret = { 0 };
	cspace_ptr_result_t			c;
	c = cspace_lookup_cspace(cspace_get_self(), src_cspace_cap,
				 CAP_RIGHTS_CSPACE_CAP_COPY);
	if (compiler_unexpected(c.e != OK)) {
		ret.error = c.e;
		goto out;
	}
	cspace_t *src_cspace = c.r;

	c = cspace_lookup_cspace(cspace_get_self(), dest_cspace_cap,
				 CAP_RIGHTS_CSPACE_CAP_CREATE);
	if (compiler_unexpected(c.e != OK)) {
		ret.error = c.e;
		goto out_src_cspace_release;
	}
	cspace_t *dest_cspace = c.r;

	cap_id_result_t new =
		cspace_copy_cap(dest_cspace, src_cspace, src_cap, rights_mask);
	if (new.e == OK) {
		ret.error   = OK;
		ret.new_cap = new.r;
	} else {
		ret.error = new.e;
	}

	object_put_cspace(dest_cspace);
out_src_cspace_release:
	object_put_cspace(src_cspace);
out:
	return ret;
}

error_t
hypercall_cspace_revoke_cap_from(cap_id_t src_cspace, cap_id_t src_cap)
{
	(void)src_cspace;
	(void)src_cap;
	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_cspace_revoke_caps_from(cap_id_t src_cspace, cap_id_t master_cap)
{
	error_t		    ret;
	cspace_ptr_result_t c;
	c = cspace_lookup_cspace(cspace_get_self(), src_cspace,
				 CAP_RIGHTS_CSPACE_CAP_REVOKE);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out;
	}
	cspace_t *cspace = c.r;

	ret = cspace_revoke_caps(cspace, master_cap);

	object_put_cspace(cspace);
out:
	return ret;
}

error_t
hypercall_cspace_configure(cap_id_t cspace_cap, count_t max_caps)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, cspace_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}
	if (type != OBJECT_TYPE_CSPACE) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_object_release;
	}

	cspace_t *target_cspace = o.r.cspace;

	spinlock_acquire(&target_cspace->header.lock);

	if (atomic_load_relaxed(&target_cspace->header.state) ==
	    OBJECT_STATE_INIT) {
		err = cspace_configure(target_cspace, max_caps);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&target_cspace->header.lock);
out_object_release:
	object_put(type, o.r);
out:
	return err;
}

error_t
hypercall_cspace_attach_thread(cap_id_t cspace_cap, cap_id_t thread_cap)
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
		goto out_release;
	}

	thread_t *thread = o.r.thread;

	cspace_ptr_result_t c = cspace_lookup_cspace(cspace, cspace_cap,
						     CAP_RIGHTS_CSPACE_ATTACH);
	if (compiler_unexpected(c.e != OK)) {
		ret = c.e;
		goto out_release;
	}

	cspace_t *target_cspace = c.r;

	spinlock_acquire(&thread->header.lock);

	if (atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT) {
		ret = cspace_attach_thread(target_cspace, thread);
	} else {
		ret = ERROR_OBJECT_STATE;
	}

	spinlock_release(&thread->header.lock);

	object_put_cspace(target_cspace);

out_release:
	object_put(type, o.r);
out:
	return ret;
}

#else
extern int unused;
#endif
