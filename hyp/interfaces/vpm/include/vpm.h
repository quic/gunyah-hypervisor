// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define vpm__vcpus_state_foreach(cpu_index, cpu_state, vcpus_state, i)         \
	cpu_state =                                                            \
		(psci_cpu_state_t)(uint32_t)(vcpus_state &                     \
					     PSCI_VCPUS_STATE_PER_VCPU_MASK);  \
	cpu_index = 0;                                                         \
	for (index_t i = 0; i < PSCI_VCPUS_STATE_MAX_INDEX;                    \
	     i += PSCI_VCPUS_STATE_PER_VCPU_BITS, cpu_index++,                 \
		     cpu_state =                                               \
			     (psci_cpu_state_t)(uint32_t)((vcpus_state >> i) & \
							  PSCI_VCPUS_STATE_PER_VCPU_MASK))

#define vpm_vcpus_state_foreach(cpu_index, cpu_state, vcpus_state)             \
	vpm__vcpus_state_foreach((cpu_index), (cpu_state), (vcpus_state),      \
				 util_cpp_unique_ident(i))

error_t
vpm_group_configure(vpm_group_t *vpm_group, vpm_group_option_flags_t flags);

error_t
vpm_attach(vpm_group_t *pg, thread_t *thread, index_t index);

error_t
vpm_bind_virq(vpm_group_t *vpm_group, vic_t *vic, virq_t virq);

void
vpm_unbind_virq(vpm_group_t *vpm_group);

vpm_state_t
vpm_get_state(vpm_group_t *vpm_group);
