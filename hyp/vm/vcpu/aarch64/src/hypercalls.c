// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#include <platform_cpu.h>
#include <qcbor.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <vcpu.h>

#include <events/vcpu.h>

#include "event_handlers.h"
#include "reg_access.h"

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

	// Check for unknown option flags
	vcpu_option_flags_t clean = vcpu_option_flags_clean(vcpu_options);
	if (vcpu_option_flags_raw(vcpu_options) !=
	    vcpu_option_flags_raw(clean)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	cspace_t	   *cspace = cspace_get_self();
	object_type_t	    type;
	object_ptr_result_t result = cspace_lookup_object_any(
		cspace, cap_id, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE, &type);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	if (compiler_unexpected(type != OBJECT_TYPE_THREAD)) {
		ret = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		object_put(type, result.r);
		goto out;
	}

	thread_t *vcpu = result.r.thread;

	if (compiler_expected(vcpu_is_vcpu(vcpu))) {
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
hypercall_vcpu_register_write(cap_id_t		  vcpu_cap,
			      vcpu_register_set_t register_set,
			      index_t register_index, register_t value)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread_any(
		cspace, vcpu_cap, CAP_RIGHTS_THREAD_WRITE_CONTEXT);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	ret = vcpu_register_write(vcpu, register_set, register_index, value);

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_bind_virq(cap_id_t vcpu_cap, cap_id_t vic_cap, virq_t virq,
			 vcpu_virq_type_t virq_type)
{
	error_t	  err;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread(
		cspace, vcpu_cap, CAP_RIGHTS_THREAD_BIND_VIRQ);
	if (compiler_unexpected(result.e != OK)) {
		err = result.e;
		goto out;
	}
	thread_t *vcpu = result.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_release_vcpu;
	}
	vic_t *vic = v.r;

	err = vcpu_bind_virq(vcpu, vic, virq, virq_type);

	object_put_vic(vic);
out_release_vcpu:
	object_put_thread(vcpu);
out:
	return err;
}

error_t
hypercall_vcpu_unbind_virq(cap_id_t vcpu_cap, vcpu_virq_type_t virq_type)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread(
		cspace, vcpu_cap, CAP_RIGHTS_THREAD_BIND_VIRQ);
	if (compiler_unexpected(result.e != OK)) {
		err = result.e;
		goto out;
	}
	thread_t *vcpu = result.r;

	err = vcpu_unbind_virq(vcpu, virq_type);

	object_put_thread(vcpu);
out:
	return err;
}

error_t
hypercall_vcpu_set_affinity(cap_id_t cap_id, register_t arg1,
			    vcpu_affinity_type_t type)
{
	error_t		    ret;
	cspace_t	   *cspace = cspace_get_self();
	cap_rights_thread_t required_rights;
	cpu_index_t	    affinity;

	switch (type) {
	case VCPU_AFFINITY_TYPE_CPU_INDEX:
		affinity = (cpu_index_t)arg1;
		break;
	case VCPU_AFFINITY_TYPE_PLATFORM_CPU_INDEX: {
		// AArch64 platforms use MPIDR_EL1 encoding.
		MPIDR_EL1_t mpidr = MPIDR_EL1_cast(arg1);
		if (!platform_cpu_mpidr_valid(mpidr)) {
			ret = ERROR_ARGUMENT_INVALID;
			goto out;
		}
		affinity = (cpu_index_t)platform_cpu_mpidr_to_index(mpidr);
		break;
	}
	default:
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (affinity == CPU_INDEX_INVALID) {
#if SCHEDULER_CAN_MIGRATE
		// Thread will become non-runnable
		required_rights = cap_rights_thread_union(
			CAP_RIGHTS_THREAD_AFFINITY, CAP_RIGHTS_THREAD_DISABLE);
#else
		ret = ERROR_UNIMPLEMENTED;
		goto out;
#endif
	} else if (!cpulocal_index_valid(affinity) ||
		   !platform_cpu_exists(affinity)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	} else {
		// Affinity is valid
		required_rights = CAP_RIGHTS_THREAD_AFFINITY;
	}

	thread_ptr_result_t result =
		cspace_lookup_thread_any(cspace, cap_id, required_rights);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
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
		scheduler_lock_nopreempt(vcpu);
		ret = scheduler_set_affinity(vcpu, affinity);
		scheduler_unlock_nopreempt(vcpu);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_poweron(cap_id_t cap_id, uint64_t entry_point, uint64_t context,
		       vcpu_poweron_flags_t flags)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	if (!vcpu_poweron_flags_is_clean(flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	thread_ptr_result_t result =
		cspace_lookup_thread(cspace, cap_id, CAP_RIGHTS_THREAD_POWER);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}
	thread_t *vcpu = result.r;

	bool reschedule = false;

	if (!vcpu_is_vcpu(vcpu)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_release_vcpu;
	}

	vcpu_power_req_flags_t power_flags = vcpu_power_req_flags_default();

	scheduler_lock(vcpu);

	bool_result_t poweron_result = vcpu_poweron(
		vcpu,
		vcpu_poweron_flags_get_preserve_entry_point(&flags)
			? vmaddr_result_error(ERROR_ARGUMENT_INVALID)
			: vmaddr_result_ok(entry_point),
		vcpu_poweron_flags_get_preserve_context(&flags)
			? register_result_error(ERROR_ARGUMENT_INVALID)
			: register_result_ok(context),
		power_flags);
	reschedule = poweron_result.r;
	ret	   = poweron_result.e;

	scheduler_unlock(vcpu);

out_release_vcpu:
	object_put_thread(vcpu);

	if (reschedule) {
		(void)scheduler_schedule();
	}
out:
	return ret;
}

error_t
hypercall_vcpu_poweroff(cap_id_t cap_id, vcpu_poweroff_flags_t flags)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	if (!vcpu_poweroff_flags_is_clean(flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	thread_ptr_result_t result =
		cspace_lookup_thread(cspace, cap_id, CAP_RIGHTS_THREAD_POWER);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}
	thread_t *vcpu = result.r;

	if ((!vcpu_is_vcpu(vcpu)) || (vcpu != thread_get_self())) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_release_vcpu;
	}

	// We can (and must) safely release our reference to the VCPU here,
	// because we know it's the current thread so the scheduler will keep
	// a reference to it. Since vcpu_poweroff() does not return, failing
	// to release this reference will leave the thread as a zombie after
	// it halts.
	object_put_thread(vcpu);

	vcpu_power_req_flags_t power_flags = vcpu_power_req_flags_default();
	ret = vcpu_poweroff(vcpu_poweroff_flags_get_last_vcpu(&flags), false,
			    power_flags);
	// This point is only reached on error
	goto out;

out_release_vcpu:
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

	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
		goto out;
	}

	if (priority > VCPU_MAX_PRIORITY) {
		ret = ERROR_DENIED;
		object_put_thread(vcpu);
		goto out;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
	if (state == OBJECT_STATE_INIT) {
		scheduler_lock_nopreempt(vcpu);
		ret = scheduler_set_priority(vcpu, priority);
		scheduler_unlock_nopreempt(vcpu);
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

	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
		ret = ERROR_ARGUMENT_INVALID;
		object_put_thread(vcpu);
		goto out;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
	if (state == OBJECT_STATE_INIT) {
		scheduler_lock_nopreempt(vcpu);
		ret = scheduler_set_timeslice(vcpu, timeslice);
		scheduler_unlock_nopreempt(vcpu);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_kill(cap_id_t cap_id)
{
	error_t	  ret;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread(
		cspace, cap_id, CAP_RIGHTS_THREAD_LIFECYCLE);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_expected(vcpu_is_vcpu(vcpu))) {
		ret = thread_kill(vcpu);
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}

	object_put_thread(vcpu);
out:
	return ret;
}

error_t
hypercall_vcpu_set_local_virq(cap_id_t cap_id, vcpu_local_virq_type_t virq_type,
			      virq_t virq)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	thread_ptr_result_t result = cspace_lookup_thread_any(
		cspace, cap_id, CAP_RIGHTS_THREAD_BIND_LOCAL_VIRQ);
	if (compiler_unexpected(result.e != OK)) {
		ret = result.e;
		goto out;
	}

	thread_t *vcpu = result.r;

	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_release_vcpu;
	}

	spinlock_acquire(&vcpu->header.lock);
	object_state_t state = atomic_load_relaxed(&vcpu->header.state);
	if (state == OBJECT_STATE_INIT) {
		ret = trigger_vcpu_set_local_virq_event(virq_type, vcpu, virq);
	} else {
		ret = ERROR_OBJECT_STATE;
	}
	spinlock_release(&vcpu->header.lock);

out_release_vcpu:
	object_put_thread(vcpu);
out:
	return ret;
}

#else
extern int unused;
#endif
