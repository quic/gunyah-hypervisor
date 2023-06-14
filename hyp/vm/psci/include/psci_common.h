// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Called by psci_common

error_t
psci_vcpu_suspend(thread_t *current) REQUIRE_PREEMPT_DISABLED;

void
psci_vcpu_resume(thread_t *thread) REQUIRE_PREEMPT_DISABLED;

void
psci_vcpu_clear_vcpu_state(thread_t *thread, cpu_index_t target_cpu)
	REQUIRE_PREEMPT_DISABLED REQUIRE_SCHEDULER_LOCK(thread);

uint32_t
psci_cpu_suspend_features(void);

// Implemented by psci_common

psci_ret_t
psci_suspend(psci_suspend_powerstate_t suspend_state,
	     paddr_t entry_point_address, register_t context_id)
	EXCLUDE_PREEMPT_DISABLED;

bool
psci_set_vpm_active_pcpus_bit(cpu_index_t bit);

bool
psci_clear_vpm_active_pcpus_bit(cpu_index_t bit);

void
psci_vpm_active_vcpus_get(cpu_index_t cpu, thread_t *vcpu)
	REQUIRE_SCHEDULER_LOCK(vcpu);

void
psci_vpm_active_vcpus_put(cpu_index_t cpu, thread_t *vcpu)
	REQUIRE_SCHEDULER_LOCK(vcpu);

bool
psci_vpm_active_vcpus_is_zero(cpu_index_t cpu);

bool
vcpus_state_is_any_awake(vpm_group_suspend_state_t vm_state, uint32_t level,
			 cpu_index_t cpu);

void
vcpus_state_set(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu,
		psci_cpu_state_t cpu_state);

void
vcpus_state_clear(vpm_group_suspend_state_t *vm_state, cpu_index_t cpu);
