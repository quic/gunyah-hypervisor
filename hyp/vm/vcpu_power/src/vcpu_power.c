// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <cpulocal.h>
#include <power.h>
#include <scheduler.h>
#include <thread.h>

#if defined(INTERFACE_VCPU_RUN)
#include <vcpu.h>
#include <vcpu_run.h>
#endif

#include "event_handlers.h"

error_t
vcpu_power_handle_vcpu_poweron(thread_t *vcpu)
{
	cpu_index_t cpu		= scheduler_get_affinity(vcpu);
	bool	    should_vote = cpulocal_index_valid(cpu);

#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		should_vote = false;
	}
#endif

	error_t ret;
	if (should_vote) {
		ret = power_vote_cpu_on(cpu);
	} else {
		ret = OK;
	}

	return ret;
}

error_t
vcpu_power_handle_vcpu_poweroff(thread_t *vcpu)
{
	cpu_index_t cpu		= scheduler_get_affinity(vcpu);
	bool	    should_vote = cpulocal_index_valid(cpu);

#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		should_vote = false;
	}
#endif

	if (should_vote) {
		power_vote_cpu_off(cpu);
	}

	return OK;
}

void
vcpu_power_handle_vcpu_stopped(void)
{
	thread_t *vcpu = thread_get_self();
	assert((vcpu != NULL) && (vcpu->kind == THREAD_KIND_VCPU));

	scheduler_lock_nopreempt(vcpu);

	cpu_index_t cpu		= scheduler_get_affinity(vcpu);
	bool	    should_vote = cpulocal_index_valid(cpu);

#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		should_vote = false;
	}
#endif

	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		// If the VCPU is already powered off, it does not hold a vote.
		should_vote = false;
	}

	if (should_vote) {
		power_vote_cpu_off(cpu);
	}

	scheduler_unlock_nopreempt(vcpu);
}

#if defined(INTERFACE_VCPU_RUN)
void
vcpu_power_handle_vcpu_run_disabled(thread_t *vcpu)
{
	cpu_index_t cpu		= scheduler_get_affinity(vcpu);
	bool	    should_vote = cpulocal_index_valid(cpu);

	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		should_vote = false;
	}

	error_t err;
	if (should_vote) {
		err = power_vote_cpu_on(cpu);
	} else {
		err = OK;
	}

	if (err != OK) {
		// Note: vcpu_run is still enabled when this event is triggered,
		// so the affinity change handler won't cast a duplicate vote.
		err = scheduler_set_affinity(vcpu, CPU_INDEX_INVALID);

		// If there's already an affinity change in progress for the
		// VCPU it is not possible to retry at this point.
		if (err == ERROR_RETRY) {
			// scheduler_lock(vcpu) already held here
			scheduler_block(vcpu, SCHEDULER_BLOCK_VCPU_FAULT);
			vcpu_halted();
		}
	}
}

void
vcpu_power_handle_vcpu_run_enabled(thread_t *vcpu)
{
	cpu_index_t cpu		= scheduler_get_affinity(vcpu);
	bool	    should_vote = cpulocal_index_valid(cpu);

	if (scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		should_vote = false;
	}

	if (should_vote) {
		power_vote_cpu_off(cpu);
	}
}
#endif

error_t
vcpu_power_handle_scheduler_set_affinity_prepare(thread_t   *vcpu,
						 cpu_index_t prev_cpu,
						 cpu_index_t next_cpu)
{
	error_t ret = OK;
	assert(prev_cpu != next_cpu);

	if (vcpu->kind != THREAD_KIND_VCPU) {
		goto out;
	}

#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		goto out;
	}
#endif

	if (!scheduler_is_blocked(vcpu, SCHEDULER_BLOCK_VCPU_OFF)) {
		if (cpulocal_index_valid(next_cpu)) {
			ret = power_vote_cpu_on(next_cpu);
		}
		if ((ret == OK) && cpulocal_index_valid(prev_cpu)) {
			power_vote_cpu_off(prev_cpu);
		}
	}

out:
	return ret;
}
