// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(HYPERCALLS)
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <thread.h>

error_t
hypercall_object_activate(cap_id_t cap)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}

	err = object_activate(type, o.r);
	object_put(type, o.r);
out:
	return err;
}

error_t
hypercall_object_activate_from(cap_id_t cspace_cap, cap_id_t cap)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	cspace_ptr_result_t c;
	c = cspace_lookup_cspace(cspace, cspace_cap,
				 CAP_RIGHTS_CSPACE_CAP_CREATE);
	if (compiler_unexpected(c.e != OK)) {
		err = c.e;
		goto out;
	}
	cspace_t *dest_cspace = c.r;

	object_ptr_result_t o = cspace_lookup_object_any(
		dest_cspace, cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_dest_cspace_release;
	}

	err = object_activate(type, o.r);
	object_put(type, o.r);
out_dest_cspace_release:
	object_put_cspace(dest_cspace);
out:
	return err;
}

error_t
hypercall_object_reset(cap_id_t cap)
{
	(void)cap;
	return ERROR_UNIMPLEMENTED;
}

error_t
hypercall_object_reset_from(cap_id_t cspace, cap_id_t cap)
{
	(void)cspace;
	(void)cap;
	return ERROR_UNIMPLEMENTED;
}
#else
extern int unused;
#endif
