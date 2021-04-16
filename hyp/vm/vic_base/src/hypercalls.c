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

#include <events/vic.h>

#include "vic_base.h"

error_t
hypercall_hwirq_bind_virq(cap_id_t hwirq_cap, cap_id_t vic_cap, virq_t virq)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	hwirq_ptr_result_t hwirq_r = cspace_lookup_hwirq(
		cspace, hwirq_cap, CAP_RIGHTS_HWIRQ_BIND_VIC);
	if (compiler_unexpected(hwirq_r.e) != OK) {
		err = hwirq_r.e;
		goto out;
	}

	vic_ptr_result_t vic_r =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(vic_r.e) != OK) {
		err = vic_r.e;
		goto out_release_hwirq;
	}

	err = trigger_vic_bind_hwirq_event(hwirq_r.r->action, vic_r.r,
					   hwirq_r.r, virq);

	object_put_vic(vic_r.r);
out_release_hwirq:
	object_put_hwirq(hwirq_r.r);
out:
	return err;
}

error_t
hypercall_hwirq_unbind_virq(cap_id_t hwirq_cap)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	hwirq_ptr_result_t hwirq_r = cspace_lookup_hwirq(
		cspace, hwirq_cap, CAP_RIGHTS_HWIRQ_BIND_VIC);
	if (compiler_unexpected(hwirq_r.e) != OK) {
		err = hwirq_r.e;
		goto out;
	}

	err = trigger_vic_unbind_hwirq_event(hwirq_r.r->action, hwirq_r.r);

	object_put_hwirq(hwirq_r.r);
out:
	return err;
}

error_t
hypercall_vic_configure(cap_id_t vic_cap, count_t max_vcpus, count_t max_virqs)
{
	error_t	      err;
	cspace_t *    cspace = cspace_get_self();
	object_type_t type;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, vic_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_released;
	}
	if (type != OBJECT_TYPE_VIC) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_unlocked;
	}
	vic_t *vic = o.r.vic;

	spinlock_acquire(&vic->header.lock);
	if (atomic_load_relaxed(&vic->header.state) == OBJECT_STATE_INIT) {
		err = vic_configure(vic, max_vcpus, max_virqs);
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vic->header.lock);

out_unlocked:
	object_put(type, o.r);
out_released:

	return err;
}

error_t
hypercall_vic_attach_vcpu(cap_id_t vic_cap, cap_id_t vcpu_cap, index_t index)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	vic_ptr_result_t vic_r =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_ATTACH_VCPU);
	if (compiler_unexpected(vic_r.e) != OK) {
		err = vic_r.e;
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
		err = vic_attach_vcpu(vic_r.r, thread, index);
	} else {
		err = ERROR_OBJECT_STATE;
	}
	spinlock_release(&thread->header.lock);

out_release_vcpu:
	object_put(type, o.r);
out_release_vic:
	object_put_vic(vic_r.r);
out:
	return err;
}
