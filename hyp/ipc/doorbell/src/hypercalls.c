// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <thread.h>

#include "doorbell.h"

error_t
hypercall_doorbell_bind_virq(cap_id_t doorbell_cap, cap_id_t vic_cap,
			     virq_t virq)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_BIND);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_doorbell_release;
	}
	vic_t *vic = v.r;

	err = doorbell_bind(doorbell, vic, virq);

	object_put_vic(vic);
out_doorbell_release:
	object_put_doorbell(doorbell);
out:
	return err;
}

error_t
hypercall_doorbell_unbind_virq(cap_id_t doorbell_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_BIND);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	doorbell_unbind(doorbell);

	object_put_doorbell(doorbell);
out:
	return err;
}

hypercall_doorbell_send_result_t
hypercall_doorbell_send(cap_id_t doorbell_cap, uint64_t new_flags)
{
	hypercall_doorbell_send_result_t ret	= { 0 };
	cspace_t *			 cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_SEND);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	doorbell_flags_result_t res;
	res = doorbell_send(doorbell, (doorbell_flags_t)new_flags);
	if (res.e == OK) {
		ret.error     = OK;
		ret.old_flags = res.r;
	} else {
		ret.error = res.e;
	}

	object_put_doorbell(doorbell);
out:
	return ret;
}

hypercall_doorbell_receive_result_t
hypercall_doorbell_receive(cap_id_t doorbell_cap, uint64_t clear_flags)
{
	hypercall_doorbell_receive_result_t ret	   = { 0 };
	cspace_t *			    cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	doorbell_flags_result_t res;
	res = doorbell_receive(doorbell, (doorbell_flags_t)clear_flags);
	if (res.e == OK) {
		ret.error     = OK;
		ret.old_flags = res.r;
	} else {
		ret.error = res.e;
	}

	object_put_doorbell(doorbell);
out:
	return ret;
}

error_t
hypercall_doorbell_reset(cap_id_t doorbell_cap)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	err = doorbell_reset(doorbell);

	object_put_doorbell(doorbell);
out:
	return err;
}

error_t
hypercall_doorbell_mask(cap_id_t doorbell_cap, uint64_t enable_mask,
			uint64_t ack_mask)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	doorbell_ptr_result_t p = cspace_lookup_doorbell(
		cspace, doorbell_cap, CAP_RIGHTS_DOORBELL_RECEIVE);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	doorbell_t *doorbell = p.r;

	err = doorbell_mask(doorbell, (doorbell_flags_t)enable_mask,
			    (doorbell_flags_t)ack_mask);

	object_put_doorbell(doorbell);
out:
	return err;
}
