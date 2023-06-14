// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
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
#include <vcpu.h>
#include <vcpu_run.h>
#include <vic.h>
#include <virq.h>

#include <events/vcpu_run.h>

#include "event_handlers.h"

error_t
vcpu_run_handle_object_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		task_queue_init(&thread->vcpu_run_wakeup_virq_task,
				TASK_QUEUE_CLASS_VCPU_RUN_WAKEUP_VIRQ);

		thread->vcpu_run_last_state = VCPU_RUN_STATE_READY;
	}

	return OK;
}

bool
vcpu_run_is_enabled(const thread_t *vcpu)
{
	return vcpu->vcpu_run_enabled;
}

hypercall_vcpu_run_result_t
hypercall_vcpu_run(cap_id_t vcpu_cap_id, register_t resume_data_0,
		   register_t resume_data_1, register_t resume_data_2)
{
	hypercall_vcpu_run_result_t ret	   = { 0 };
	cspace_t		   *cspace = cspace_get_self();

	thread_ptr_result_t thread_r = cspace_lookup_thread(
		cspace, vcpu_cap_id,
		cap_rights_thread_union(CAP_RIGHTS_THREAD_AFFINITY,
					CAP_RIGHTS_THREAD_YIELD_TO));
	if (compiler_unexpected(thread_r.e != OK)) {
		ret.error = thread_r.e;
		goto out_err;
	}

	thread_t *vcpu = thread_r.r;
	if (compiler_unexpected(vcpu->kind != THREAD_KIND_VCPU)) {
		ret.error = ERROR_ARGUMENT_INVALID;
		goto out_obj_put_thread;
	}

	scheduler_lock(vcpu);
	if (!scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_RUN)) {
		// VCPU not proxy-scheduled, or is being run by another caller
		ret.error = ERROR_BUSY;
		goto unlock;
	}

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
		ret.vcpu_state = trigger_vcpu_run_check_event(
			vcpu, &ret.state_data_0, &ret.state_data_1,
			&ret.state_data_2);
	}
	vcpu->vcpu_run_last_state = ret.vcpu_state;

	scheduler_block(vcpu, SCHEDULER_BLOCK_VCPU_RUN);
unlock:
	scheduler_unlock(vcpu);
out_obj_put_thread:
	object_put_thread(vcpu);
out_err:
	return ret;
}

vcpu_run_state_t
vcpu_run_handle_vcpu_run_check(const thread_t *vcpu, register_t *state_data_0)
{
	vcpu_run_state_t ret = VCPU_RUN_STATE_BLOCKED;
	if (compiler_unexpected(thread_has_exited(vcpu))) {
		vcpu_run_poweroff_flags_t flags =
			vcpu_run_poweroff_flags_default();
		ret = VCPU_RUN_STATE_POWERED_OFF;
		vcpu_run_poweroff_flags_set_exited(&flags, true);
		*state_data_0 = vcpu_run_poweroff_flags_raw(flags);
	}
	return ret;
}

hypercall_vcpu_run_check_result_t
hypercall_vcpu_run_check(cap_id_t vcpu_cap_id)
{
	hypercall_vcpu_run_check_result_t ret	 = { 0 };
	cspace_t			 *cspace = cspace_get_self();

	thread_ptr_result_t thread_r = cspace_lookup_thread(
		cspace, vcpu_cap_id,
		cap_rights_thread_union(CAP_RIGHTS_THREAD_BIND_VIRQ,
					CAP_RIGHTS_THREAD_STATE));
	if (compiler_unexpected(thread_r.e != OK)) {
		ret.error = thread_r.e;
		goto out_err;
	}

	thread_t *vcpu = thread_r.r;
	if (compiler_unexpected(vcpu->kind != THREAD_KIND_VCPU)) {
		ret.error = ERROR_ARGUMENT_INVALID;
		goto out_obj_put_thread;
	}

	scheduler_lock(vcpu);
	if (scheduler_is_runnable(vcpu)) {
		ret.error = ERROR_BUSY;
	} else {
		ret.vcpu_state = trigger_vcpu_run_check_event(
			vcpu, &ret.state_data_0, &ret.state_data_1,
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
	scheduler_lock(vcpu);

	error_t err = vic_bind_shared(&vcpu->vcpu_run_wakeup_virq, vic, virq,
				      VIRQ_TRIGGER_VCPU_RUN_WAKEUP);
	if (err == OK) {
		scheduler_block(vcpu, SCHEDULER_BLOCK_VCPU_RUN);
		vcpu->vcpu_run_enabled = true;
		trigger_vcpu_run_enabled_event(vcpu);
	}

	scheduler_unlock(vcpu);

	return err;
}

error_t
vcpu_run_handle_vcpu_unbind_virq(thread_t *vcpu)
{
	scheduler_lock(vcpu);
	if (vcpu->vcpu_run_enabled) {
		trigger_vcpu_run_disabled_event(vcpu);
		vcpu->vcpu_run_enabled = false;
		if (scheduler_unblock(vcpu, SCHEDULER_BLOCK_VCPU_RUN)) {
			scheduler_trigger();
		}
	}
	scheduler_unlock(vcpu);

	vic_unbind_sync(&vcpu->vcpu_run_wakeup_virq);

	return OK;
}

error_t
vcpu_run_handle_task_queue_execute(task_queue_entry_t *task_entry)
{
	assert(task_entry != NULL);
	thread_t *vcpu =
		thread_container_of_vcpu_run_wakeup_virq_task(task_entry);

	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);

	(void)virq_assert(&vcpu->vcpu_run_wakeup_virq, true);
	object_put_thread(vcpu);

	return OK;
}

void
vcpu_run_trigger_virq(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);

	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_RUN)) {
		(void)object_get_thread_additional(vcpu);
		if (task_queue_schedule(&vcpu->vcpu_run_wakeup_virq_task) !=
		    OK) {
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
	if (thread->kind == THREAD_KIND_VCPU) {
		// Killing the VCPU may have made it temporarily runnable so
		// it can unwind its EL2 stack. Raise a scheduling doorbell.
		vcpu_run_trigger_virq(thread);
	}
}

void
vcpu_run_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->kind == THREAD_KIND_VCPU) {
		vic_unbind(&thread->vcpu_run_wakeup_virq);
	}
}

scheduler_block_properties_t
vcpu_run_handle_scheduler_get_block_properties(scheduler_block_t block)
{
	assert(block == SCHEDULER_BLOCK_VCPU_RUN);

	// Set the vcpu_run block flag as non-killable to ensure that killed
	// VCPUs continue to be scheduled normally.
	scheduler_block_properties_t props =
		scheduler_block_properties_default();
	scheduler_block_properties_set_non_killable(&props, true);

	return props;
}
