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
#include <object.h>
#include <partition.h>
#include <spinlock.h>
#include <vpm.h>

error_t
hypercall_vpm_group_configure(cap_id_t		       vpm_group_cap,
			      vpm_group_option_flags_t flags)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	if (!vpm_group_option_flags_is_clean(flags)) {
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	object_type_t	    type;
	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, vpm_group_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out;
	}
	if (type != OBJECT_TYPE_VPM_GROUP) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_release_vpm_group;
	}
	vpm_group_t *vpm_group = o.r.vpm_group;

	spinlock_acquire(&vpm_group->header.lock);

	if (atomic_load_relaxed(&vpm_group->header.state) ==
	    OBJECT_STATE_INIT) {
		err = vpm_group_configure(vpm_group, flags);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&vpm_group->header.lock);

out_release_vpm_group:
	object_put(type, o.r);
out:
	return err;
}

error_t
hypercall_vpm_group_attach_vcpu(cap_id_t vpm_group_cap, cap_id_t vcpu_cap,
				index_t index)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vpm_group_ptr_result_t vpm_group_r = cspace_lookup_vpm_group(
		cspace, vpm_group_cap, CAP_RIGHTS_VPM_GROUP_ATTACH_VCPU);
	if (compiler_unexpected(vpm_group_r.e) != OK) {
		err = vpm_group_r.e;
		goto out;
	}

	object_type_t	    type;
	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, vcpu_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_release_vic;
	}
	if (type != OBJECT_TYPE_THREAD) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_release_vcpu;
	}
	thread_t *thread = o.r.thread;

	spinlock_acquire(&thread->header.lock);
	if (atomic_load_relaxed(&thread->header.state) == OBJECT_STATE_INIT) {
		err = vpm_attach(vpm_group_r.r, thread, index);
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&thread->header.lock);

out_release_vcpu:
	object_put(type, o.r);
out_release_vic:
	object_put_vpm_group(vpm_group_r.r);
out:
	return err;
}

error_t
hypercall_vpm_group_bind_virq(cap_id_t vpm_group_cap, cap_id_t vic_cap,
			      virq_t virq)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	vpm_group_ptr_result_t p = cspace_lookup_vpm_group(
		cspace, vpm_group_cap, CAP_RIGHTS_VPM_GROUP_BIND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	vpm_group_t *vpm_group = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_vpm_group_release;
	}
	vic_t *vic = v.r;

	err = vpm_bind_virq(vpm_group, vic, virq);

	object_put_vic(vic);
out_vpm_group_release:
	object_put_vpm_group(vpm_group);
out:
	return err;
}

error_t
hypercall_vpm_group_unbind_virq(cap_id_t vpm_group_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	vpm_group_ptr_result_t p = cspace_lookup_vpm_group(
		cspace, vpm_group_cap, CAP_RIGHTS_VPM_GROUP_BIND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	vpm_group_t *vpm_group = p.r;

	vpm_unbind_virq(vpm_group);

	object_put_vpm_group(vpm_group);
out:
	return err;
}

hypercall_vpm_group_get_state_result_t
hypercall_vpm_group_get_state(cap_id_t vpm_group_cap)
{
	hypercall_vpm_group_get_state_result_t ret    = { 0 };
	cspace_t			      *cspace = cspace_get_self();

	vpm_group_ptr_result_t p = cspace_lookup_vpm_group(
		cspace, vpm_group_cap, CAP_RIGHTS_VPM_GROUP_QUERY);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	vpm_group_t *vpm_group = p.r;

	vpm_state_t state = vpm_get_state(vpm_group);

	ret.error     = OK;
	ret.vpm_state = (uint64_t)state;

	object_put_vpm_group(vpm_group);
out:
	return ret;
}
