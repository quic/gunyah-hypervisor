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
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <scheduler.h>
#include <spinlock.h>
#include <vcpu.h>

#include "event_handlers.h"

// This hypercall should be called before the vCPU is activated. It copies the
// provided flags into a variable called vcpu_options in the thread structure.
// Relevant modules (such as the debug module) need to extend the
// vcpu_option_flags bitfield to add their configuration flags, and in their
// thread_activate handlers they need to check the values of these flags (by
// looking at the thread's vcpu_options variable) and act on them.
error_t
hypercall_vcpu_configure(cap_id_t cap_id, vcpu_option_flags_t vcpu_options)
{
	error_t ret = OK;

	if (vcpu_option_flags_get_res0_0(&vcpu_options) != 0) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cspace_t *	    cspace = cspace_get_self();
	object_type_t	    type;
	object_ptr_result_t result = cspace_lookup_object_any(
		cspace, cap_id, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	if (compiler_unexpected(type != OBJECT_TYPE_THREAD)) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put(type, result.r);
		goto out;
	}

	thread_t *vcpu = result.r.thread;

	if (compiler_expected(vcpu->kind == THREAD_KIND_VCPU)) {
		spinlock_acquire(&vcpu->header.lock);
		object_state_t state = atomic_load_relaxed(&vcpu->header.state);
		if (state == OBJECT_STATE_INIT) {
			ret = vcpu_configure(vcpu, vcpu_options);
		} else {
			ret = ERROR_OBJECT_STATE;
		}
		spinlock_release(&vcpu->header.lock);
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_set_affinity(cap_id_t cap_id, cpu_index_t affinity)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread_any(
		cspace, cap_id, CAP_RIGHTS_THREAD_AFFINITY);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_unexpected(vcpu->kind != THREAD_KIND_VCPU)) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
		goto out;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
#if SCHEDULER_CAN_MIGRATE
	if ((state == OBJECT_STATE_INIT) || (state == OBJECT_STATE_ACTIVE)) {
#else
	if (state == OBJECT_STATE_INIT) {
#endif
		scheduler_lock(vcpu);
		ret = scheduler_set_affinity(vcpu, affinity);
		scheduler_unlock(vcpu);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_poweron(cap_id_t cap_id, uint64_t entry_point, uint64_t context)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result =
		cspace_lookup_thread(cspace, cap_id, CAP_RIGHTS_THREAD_POWER);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_expected(vcpu->kind == THREAD_KIND_VCPU)) {
		bool reschedule = false;

		scheduler_lock(vcpu);
		if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
			reschedule = vcpu_poweron(vcpu, entry_point, context);
		} else {
			ret = ERROR_BUSY;
		}
		scheduler_unlock(vcpu);
		object_put_thread(vcpu);

		if (reschedule) {
			scheduler_schedule();
		}
	} else {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
	}
out:
	return ret;
}

error_t
hypercall_vcpu_poweroff(cap_id_t cap_id)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result =
		cspace_lookup_thread(cspace, cap_id, CAP_RIGHTS_THREAD_POWER);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	ret = ERROR_UNIMPLEMENTED;

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_set_priority(cap_id_t cap_id, priority_t priority)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread_any(
		cspace, cap_id, CAP_RIGHTS_THREAD_PRIORITY);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_unexpected(vcpu->kind != THREAD_KIND_VCPU)) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
		goto out;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
	if (state == OBJECT_STATE_INIT) {
		scheduler_lock(vcpu);
		ret = scheduler_set_priority(vcpu, priority);
		scheduler_unlock(vcpu);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_set_timeslice(cap_id_t cap_id, nanoseconds_t timeslice)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread_any(
		cspace, cap_id, CAP_RIGHTS_THREAD_TIMESLICE);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_unexpected(vcpu->kind != THREAD_KIND_VCPU)) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
		goto out;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
	if (state == OBJECT_STATE_INIT) {
		scheduler_lock(vcpu);
		ret = scheduler_set_timeslice(vcpu, timeslice);
		scheduler_unlock(vcpu);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

	object_put_thread(vcpu);
out:
	return ret;
}

#else
extern int unused;
#endif
