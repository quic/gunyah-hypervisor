// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#if defined(INTERFACE_VCPU_RUN)
#include <gpt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <vcpu_run.h>

#include "event_handlers.h"

vcpu_trap_result_t
addrspace_handle_vdevice_access_fixed_addr(vmaddr_t ipa, size_t access_size,
					   register_t *value, bool is_write)
{
	thread_t *current = thread_get_self();

	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	scheduler_lock(current);
	if (vcpu_run_is_enabled(current)) {
		addrspace_t *addrspace = current->addrspace;

		// We need to call gpt_lookup() in an RCU critical section to
		// ensure that levels aren't freed while it is accessing them,
		// but we can end the critical section immediately afterwards
		// since we are not dereferencing anything.
		rcu_read_start();
		gpt_lookup_result_t result =
			gpt_lookup(&addrspace->vmmio_ranges, ipa, access_size);
		rcu_read_finish();

		if ((result.size == access_size) &&
		    (result.entry.type == GPT_TYPE_VMMIO_RANGE)) {
			current->addrspace_vmmio_access_ipa  = ipa;
			current->addrspace_vmmio_access_size = access_size;
			current->addrspace_vmmio_access_value =
				is_write ? *value : 0U;
			current->addrspace_vmmio_access_write = is_write;

			scheduler_block(current,
					SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS);
			scheduler_unlock_nopreempt(current);
			(void)scheduler_schedule();
			scheduler_lock_nopreempt(current);

			if (!is_write) {
				*value = current->addrspace_vmmio_access_value;
			}

			ret = VCPU_TRAP_RESULT_EMULATED;
		}
	}
	scheduler_unlock(current);

	return ret;
}

vcpu_run_state_t
addrspace_handle_vcpu_run_check(const thread_t *vcpu, register_t *state_data_0,
				register_t *state_data_1,
				register_t *state_data_2)
{
	vcpu_run_state_t ret;

	if (scheduler_is_blocked(vcpu,
				 SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS)) {
		*state_data_0 = (register_t)vcpu->addrspace_vmmio_access_ipa;
		*state_data_1 = (register_t)vcpu->addrspace_vmmio_access_size;
		*state_data_2 = (register_t)vcpu->addrspace_vmmio_access_value;
		ret	      = vcpu->addrspace_vmmio_access_write
					? VCPU_RUN_STATE_ADDRSPACE_VMMIO_WRITE
					: VCPU_RUN_STATE_ADDRSPACE_VMMIO_READ;
	} else {
		ret = VCPU_RUN_STATE_BLOCKED;
	}

	return ret;
}

error_t
addrspace_handle_vcpu_run_resume_read(thread_t *vcpu, register_t resume_data_0)
{
	assert(scheduler_is_blocked(vcpu,
				    SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS) &&
	       !vcpu->addrspace_vmmio_access_write);
	vcpu->addrspace_vmmio_access_value = resume_data_0;
	(void)scheduler_unblock(vcpu, SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS);
	return OK;
}

error_t
addrspace_handle_vcpu_run_resume_write(thread_t *vcpu)
{
	assert(scheduler_is_blocked(vcpu,
				    SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS) &&
	       vcpu->addrspace_vmmio_access_write);
	(void)scheduler_unblock(vcpu, SCHEDULER_BLOCK_ADDRSPACE_VMMIO_ACCESS);
	return OK;
}

#endif // INTERFACE_VCPU_RUN
