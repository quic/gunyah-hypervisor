// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <preempt.h>
#include <scheduler.h>
#include <spinlock.h>
#include <task_queue.h>
#include <thread.h>
#include <vcpu.h>
#include <vcpu_run.h>
#include <vic.h>
#include <virq.h>

#include <events/vcpu_run.h>

#include "event_handlers.h"

bool
vcpu_run_is_enabled(const thread_t *vcpu)
{
	return vcpu_runtime_flags_get_vcpu_run_enabled(&vcpu->vcpu_flags);
}

static bool
vcpu_run_is_scheduled(const thread_t *vcpu) REQUIRE_SCHEDULER_LOCK(vcpu)
{
	return vcpu_runtime_flags_get_vcpu_run_scheduled(&vcpu->vcpu_flags);
}

static void
vcpu_run_set_scheduled(thread_t *vcpu, bool scheduled)
	REQUIRE_SCHEDULER_LOCK(vcpu)
{
	vcpu_runtime_flags_set_vcpu_run_scheduled(&vcpu->vcpu_flags, scheduled);
}

bool
vcpu_run_handle_vcpu_activate_thread(thread_t		*thread,
				     vcpu_option_flags_t options)
{
	bool ret;

	assert_debug(thread != NULL);
	assert_debug(vcpu_is_vcpu(thread));

	task_queue_init(&thread->vcpu_run_wakeup_virq_task);
	thread->vcpu_run_last_state = VCPU_RUN_STATE_READY;

	scheduler_lock(thread);
	if (vcpu_option_flags_get_vcpu_run_scheduled(&options)) {
		vcpu_option_flags_set_vcpu_run_scheduled(&thread->vcpu_options,
							 true);
		scheduler_block(thread, SCHEDULER_BLOCK_VCPU_RUN);

		vcpu_runtime_flags_set_vcpu_run_enabled(&thread->vcpu_flags,
							true);
	}
	if (vcpu_option_flags_get_vcpu_run_no_timer_wakeup(&options)) {
		if (!vcpu_run_is_enabled(thread)) {
			ret = false;
			goto out_unlock;
		}
		vcpu_option_flags_set_vcpu_run_no_timer_wakeup(
			&thread->vcpu_options, true);
	}
	ret = true;

out_unlock:
	scheduler_unlock(thread);

	return ret;
}

// This handler is registered for resume from run-state
// VCPU_RUN_STATE_EXPECTS_WAKEUP_OR_TIMEOUT. We just wakeup the vcpu since
// this resume was expected.
// Note: We don't check whether the host performed an early resume since
// its allowed to schedule the vcpu at any time.
error_t
vcpu_run_handle_vcpu_run_resume(thread_t *vcpu)
{
	vcpu_wakeup(vcpu);
	return OK;
}

static vcpu_run_state_t
do_vcpu_run_check(const thread_t *vcpu, register_t *state_data_0,
		  register_t *state_data_1, register_t *state_data_2)
	REQUIRE_SCHEDULER_LOCK(vcpu)
{
	vcpu_run_state_t ret;

	assert(vcpu_is_vcpu(vcpu));
	assert(!scheduler_is_runnable(vcpu));

	thread_state_t state = atomic_load_relaxed(&vcpu->state);
	if (compiler_expected(state == THREAD_STATE_READY)) {
		ret = trigger_vcpu_run_check_event(vcpu, state_data_0,
						   state_data_1, state_data_2);

		if ((ret == VCPU_RUN_STATE_EXPECTS_WAKEUP) &&
		    vcpu_option_flags_get_vcpu_run_no_timer_wakeup(
			    &vcpu->vcpu_options)) {
			trigger_vcpu_run_get_next_wakeup_event(vcpu,
							       state_data_2);
			if (*state_data_2 != TIMER_INVALID_TIMEOUT) {
				// Update the vcpu_run result to timeout
				ret = VCPU_RUN_STATE_EXPECTS_WAKEUP_OR_TIMEOUT;
			}
		}
	} else if (state == THREAD_STATE_EXITED) {
		vcpu_run_poweroff_flags_t flags =
			vcpu_run_poweroff_flags_default();
		ret = VCPU_RUN_STATE_POWERED_OFF;
		vcpu_run_poweroff_flags_set_exited(&flags, true);
		*state_data_0 = vcpu_run_poweroff_flags_raw(flags);
	} else {
		assert(state == THREAD_STATE_KILLED);
		// The killed VCPU must be run until it has exited, and should
		// only ever be blocked for hypervisor internal reasons.
		ret = VCPU_RUN_STATE_BLOCKED;
	}

	return ret;
}

hypercall_vcpu_run_result_t
hypercall_vcpu_run(cap_id_t vcpu_cap, register_t resume_data_0,
		   register_t resume_data_1, register_t resume_data_2)
{
	hypercall_vcpu_run_result_t ret	   = { 0 };
	cspace_t		   *cspace = cspace_get_self();

	thread_ptr_result_t thread_r = cspace_lookup_thread(
		cspace, vcpu_cap,
		cap_rights_thread_union(CAP_RIGHTS_THREAD_AFFINITY,
					CAP_RIGHTS_THREAD_YIELD_TO));
	if (compiler_unexpected(thread_r.e != OK)) {
		ret.error = thread_r.e;
		goto out_err;
	}

	thread_t *vcpu = thread_r.r;
	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
		ret.error = ERROR_ARGUMENT_INVALID;
		goto out_obj_put_thread;
	}

	scheduler_lock(vcpu);
	if (!scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_RUN)) {
		// VCPU not proxy-scheduled, or is being run by another caller.
		if (thread_is_dying(vcpu) || thread_has_exited(vcpu)) {
			// As a special case, if the VCPU has been killed, we
			// return OK / VCPU_RUN_STATE_BLOCKED. This is because a
			// killed VCPU will typically be scheduled to run to
			// completion by whoever killed it, which may be a
			// management VM rather than the VCPU's regular
			// scheduler. In that case, this call may not actually
			// be in error, and the caller should treat it as
			// transiently blocked.
			ret.error      = OK;
			ret.vcpu_state = VCPU_RUN_STATE_BLOCKED;
		} else {
			ret.error = ERROR_BUSY;
		}
		goto unlock;
	}
	assert_debug(vcpu_run_is_enabled(vcpu));

	// Set the vcpu_run scheduled state so we don't treat the resulting
	// vcpu_wakeup events from the resume event below as requiring raising
	// VIRQs to the host.
	vcpu_run_set_scheduled(vcpu, true);

	ret.error = trigger_vcpu_run_resume_event(vcpu->vcpu_run_last_state,
						  vcpu, resume_data_0,
						  resume_data_1, resume_data_2);
	if (ret.error != OK) {
		goto unlock;
	}

	(void)scheduler_unblock(vcpu, SCHEDULER_BLOCK_VCPU_RUN);

	if (scheduler_is_runnable(vcpu)) {
		assert_cpulocal_safe();
		cpu_index_t this_pcpu = cpulocal_get_index();
		// Make sure the vCPU will run on this PCPU. Note that this
		// might block the thread for an RCU grace period, which will
		// show up as a brief transient VCPU_RUN_STATE_BLOCKED. To
		// prevent that persisting indefinitely, the caller should avoid
		// migration as much as possible.
		ret.error = scheduler_set_affinity(vcpu, this_pcpu);
		if (ret.error != OK) {
			goto unlock;
		}

		// Use a nopreempt unlock to make sure we don't get migrated
		scheduler_unlock_nopreempt(vcpu);
		scheduler_yield_to(vcpu);
		scheduler_lock_nopreempt(vcpu);
	}

	if (scheduler_is_runnable(vcpu)) {
		ret.vcpu_state = VCPU_RUN_STATE_READY;
	} else {
		ret.vcpu_state = do_vcpu_run_check(vcpu, &ret.state_data_0,
						   &ret.state_data_1,
						   &ret.state_data_2);
	}
	vcpu->vcpu_run_last_state = ret.vcpu_state;

	scheduler_block(vcpu, SCHEDULER_BLOCK_VCPU_RUN);
unlock:
	vcpu_run_set_scheduled(vcpu, false);
	scheduler_unlock(vcpu);
out_obj_put_thread:
	object_put_thread(vcpu);
out_err:
	return ret;
}

hypercall_vcpu_run_check_result_t
hypercall_vcpu_run_check(cap_id_t vcpu_cap)
{
	hypercall_vcpu_run_check_result_t ret	 = { 0 };
	cspace_t			 *cspace = cspace_get_self();

	thread_ptr_result_t thread_r = cspace_lookup_thread(
		cspace, vcpu_cap,
		cap_rights_thread_union(CAP_RIGHTS_THREAD_BIND_VIRQ,
					CAP_RIGHTS_THREAD_STATE));
	if (compiler_unexpected(thread_r.e != OK)) {
		ret.error = thread_r.e;
		goto out_err;
	}

	thread_t *vcpu = thread_r.r;
	if (compiler_unexpected(!vcpu_is_vcpu(vcpu))) {
		ret.error = ERROR_ARGUMENT_INVALID;
		goto out_obj_put_thread;
	}

	scheduler_lock(vcpu);
	if (scheduler_is_runnable(vcpu)) {
		ret.error = ERROR_BUSY;
	} else {
		ret.vcpu_state = do_vcpu_run_check(vcpu, &ret.state_data_0,
						   &ret.state_data_1,
						   &ret.state_data_2);
		if (ret.vcpu_state == VCPU_RUN_STATE_BLOCKED) {
			ret.error = ERROR_BUSY;
		}
	}
	scheduler_unlock(vcpu);

out_obj_put_thread:
	object_put_thread(vcpu);
out_err:
	return ret;
}

error_t
vcpu_run_handle_vcpu_bind_virq(thread_t *vcpu, vic_t *vic, virq_t virq)
{
	error_t err;

	scheduler_lock(vcpu);

	if (scheduler_is_running(vcpu)) {
		err = ERROR_BUSY;
		goto out_unlock;
	}

	err = vic_bind_shared(&vcpu->vcpu_run_wakeup_virq, vic, virq,
			      VIRQ_TRIGGER_VCPU_RUN_WAKEUP);

	if ((err == OK) && !vcpu_run_is_enabled(vcpu)) {
		scheduler_block(vcpu, SCHEDULER_BLOCK_VCPU_RUN);
		vcpu_runtime_flags_set_vcpu_run_enabled(&vcpu->vcpu_flags,
							true);
		trigger_vcpu_run_enabled_event(vcpu);
	}

out_unlock:
	scheduler_unlock(vcpu);

	return err;
}

error_t
vcpu_run_handle_vcpu_unbind_virq(thread_t *vcpu)
{
	vic_unbind_sync(&vcpu->vcpu_run_wakeup_virq);

	return OK;
}

error_t
vcpu_run_handle_task_queue_execute(task_queue_entry_t *entry)
{
	assert(entry != NULL);
	thread_t *vcpu = thread_container_of_vcpu_run_wakeup_virq_task(entry);

	assert(vcpu != NULL);
	assert(vcpu_is_vcpu(vcpu));

	(void)virq_assert(&vcpu->vcpu_run_wakeup_virq, true);
	object_put_thread(vcpu);

	return OK;
}

void
vcpu_run_trigger_virq(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu_is_vcpu(vcpu));

	if (compiler_unexpected(
		    scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_RUN) &&
		    !vcpu_run_is_scheduled(vcpu))) {
		(void)object_get_thread_additional(vcpu);
		if (!task_queue_schedule(
			    &vcpu->vcpu_run_wakeup_virq_task,
			    TASK_QUEUE_CLASS_VCPU_RUN_WAKEUP_VIRQ)) {
			object_put_thread(vcpu);
		}
	}
}

error_t
vcpu_run_handle_vcpu_poweron(thread_t *vcpu)
{
	vcpu_run_trigger_virq(vcpu);
	return OK;
}

void
vcpu_run_handle_thread_killed(thread_t *thread)
{
	assert(thread != NULL);
	if (vcpu_is_vcpu(thread)) {
		// Killing the VCPU may have made it temporarily runnable so
		// it can unwind its EL2 stack. Raise a scheduling doorbell.
		scheduler_lock(thread);
		vcpu_run_trigger_virq(thread);
		scheduler_unlock(thread);
	}
}

void
vcpu_run_handle_object_deactivate_thread(thread_t *thread)
{
	if (vcpu_is_vcpu(thread)) {
		vic_unbind(&thread->vcpu_run_wakeup_virq);
	}
}

scheduler_block_properties_t
vcpu_run_handle_scheduler_get_block_properties(scheduler_block_t block)
{
	assert_debug(block == SCHEDULER_BLOCK_VCPU_RUN);

	// Set the vcpu_run block flag as non-killable to ensure that killed
	// VCPUs continue to be scheduled normally.
	scheduler_block_properties_t props =
		scheduler_block_properties_default();
	scheduler_block_properties_set_non_killable(&props, true);

	return props;
}

void
vcpu_run_handle_vcpu_run_get_next_wakeup(ticks_t *timeout)
{
	assert_debug(timeout != NULL);

	// This handler has first-priority, and sets the default timeout to
	// invalid.
	*timeout = TIMER_INVALID_TIMEOUT;
}
