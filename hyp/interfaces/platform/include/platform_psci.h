// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause
// Checks that the suspend state is valid
//
// This function checks that the pcpu supports the cpu and cluster level
// state specified. If the specified state is not valid, then it returns
// PSCI_RET_INVALID_PARAMETERS.
psci_ret_t
platform_psci_suspend_state_validation(psci_suspend_powerstate_t suspend_state,
				       cpu_index_t cpu, psci_mode_t psci_mode);

// Gets the cpu state from the suspend power state
psci_cpu_state_t
platform_psci_get_cpu_state(psci_suspend_powerstate_t suspend_state);

// Sets cpu state to the stateid of the suspend power state
void
platform_psci_set_cpu_state(psci_suspend_powerstate_t *suspend_state,
			    psci_cpu_state_t	       cpu_state);

// Returns that shallowest state between two cpu states
psci_cpu_state_t
platform_psci_shallowest_cpu_state(psci_cpu_state_t state1,
				   psci_cpu_state_t state2);

// Returns the deepest cpu state supported by a cpu
psci_cpu_state_t
platform_psci_deepest_cpu_state(cpu_index_t cpu);

// Returns the deepest cpu-level suspend state id supported by a cpu
psci_suspend_powerstate_stateid_t
platform_psci_deepest_cpu_level_stateid(cpu_index_t cpu);

// Returns true if cpu state is in active state
bool
platform_psci_is_cpu_active(psci_cpu_state_t cpu_state);

// Returns true if cpus is in power collapse state
bool
platform_psci_is_cpu_poweroff(psci_cpu_state_t cpu_state);

// Returns true if cluster state is in active state
bool
platform_psci_is_cluster_active(psci_cluster_state_L3_t cluster_state);

// Returns the cluster indices
uint32_t
platform_psci_get_cluster_index(cpu_index_t cpu);

// Returns the start index of children in hierarchy/counts based on level and
// cpu
error_t
platform_psci_get_index_by_level(cpu_index_t cpu, uint32_t *start_idx,
				 uint32_t *children_counts, uint32_t level);

#if !defined(PSCI_AFFINITY_LEVELS_NOT_SUPPORTED) ||                            \
	!PSCI_AFFINITY_LEVELS_NOT_SUPPORTED
// Checks if cluster state corresponds to a power off state
bool
platform_psci_is_cluster_state_poweroff(psci_suspend_powerstate_t suspend_state);

// Returns true if cluster state is in active state
bool
platform_psci_is_cluster_active(psci_cluster_state_L3_t cluster_state);

// Checks if cluster state corresponds to a retention state
bool
platform_psci_is_cluster_state_retention(
	psci_suspend_powerstate_t suspend_state);

// Gets the cluster state from the suspend power state
psci_cluster_state_t
platform_psci_get_cluster_state(psci_suspend_powerstate_t suspend_state);

// Sets cluster state to the stateid of the suspend power state
void
platform_psci_set_cluster_state(psci_suspend_powerstate_t *suspend_state,
				psci_cluster_state_t	   cluster_state);

#if (PLATFORM_MAX_HIERARCHY == 2)
// Gets the system state from the suspend power state
psci_system_state_t
platform_psci_get_system_state(psci_suspend_powerstate_t suspend_state);

// Sets system state to the stateid of the suspend power state
void
platform_psci_set_system_state(psci_suspend_powerstate_t *suspend_state,
			       psci_system_state_t	  system_state);
#endif

// Returns the suspend level of the last cpu
index_t
platform_psci_get_last_cpu_level(psci_suspend_powerstate_t suspend_state);

// Sets the suspend level of the last cpu
void
platform_psci_set_last_cpu_level(psci_suspend_powerstate_t *suspend_state,
				 index_t		    last_cpu);

// Returns that shallowest state between two cluster states
psci_cluster_state_t
platform_psci_shallowest_cluster_state(psci_cluster_state_t state1,
				       uint16_t		    state2);

// Returns the deepest cluster-level suspend state supported by the system
psci_cluster_state_t
platform_psci_deepest_cluster_state(void);

// Returns the deepest cluster-level suspend state id supported by a cpu
psci_suspend_powerstate_stateid_t
platform_psci_deepest_cluster_level_stateid(cpu_index_t cpu);

#endif
