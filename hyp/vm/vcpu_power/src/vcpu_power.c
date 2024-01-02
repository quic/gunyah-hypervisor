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
	assert((vcpu != NULL) && !vcpu->vcpu_power_should_vote);
	vcpu->vcpu_power_should_vote = true;

	cpu_index_t cpu	     = scheduler_get_affinity(vcpu);
	bool	    can_vote = cpulocal_index_valid(cpu);

#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		can_vote = false;
	}
#endif

	error_t ret;
	if (can_vote) {
		ret = power_vote_cpu_on(cpu);
	} else {
		ret = OK;
	}

	return ret;
}

error_t
vcpu_power_handle_vcpu_poweroff(thread_t *vcpu)
{
	assert((vcpu != NULL) && vcpu->vcpu_power_should_vote);
	vcpu->vcpu_power_should_vote = false;

	cpu_index_t cpu	     = scheduler_get_affinity(vcpu);
	bool	    can_vote = cpulocal_index_valid(cpu);
#if defined(INTERFACE_VCPU_RUN)
	if (vcpu_run_is_enabled(vcpu)) {
		can_vote = false;
	}
#endif

	if (can_vote) {
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

	if (vcpu->vcpu_power_should_vote) {
		vcpu->vcpu_power_should_vote = false;

		cpu_index_t cpu	     = scheduler_get_affinity(vcpu);
		bool	    can_vote = cpulocal_index_valid(cpu);
#if defined(INTERFACE_VCPU_RUN)
		if (vcpu_run_is_enabled(vcpu)) {
			can_vote = false;
		}
#endif

		if (can_vote) {
			power_vote_cpu_off(cpu);
		}
	}

	scheduler_unlock_nopreempt(vcpu);
}

#if defined(INTERFACE_VCPU_RUN)
void
vcpu_power_handle_vcpu_run_enabled(thread_t *vcpu)
{
	cpu_index_t cpu	     = scheduler_get_affinity(vcpu);
	bool	    can_vote = cpulocal_index_valid(cpu);

	if (can_vote && vcpu->vcpu_power_should_vote) {
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

	if (vcpu->vcpu_power_should_vote) {
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
