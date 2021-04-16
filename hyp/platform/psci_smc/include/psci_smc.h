// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

uint32_t
psci_smc_psci_version(void);

error_t
psci_smc_cpu_suspend(register_t power_state, register_t entry_point,
		     register_t context_id);

#if defined(PLATFORM_PSCI_DEFAULT_SUSPEND)
error_t
psci_smc_cpu_default_suspend(paddr_t entry_point, register_t context_id);
#endif

error_t
psci_smc_system_reset(void);

error_t
psci_smc_cpu_off(void);

error_t
psci_smc_cpu_on(psci_mpidr_t cpu_id, register_t entry_point,
		register_t context_id);

sint32_result_t
psci_smc_psci_features(psci_function_t fn, bool smc64);

error_t
psci_smc_cpu_freeze(void);

error_t
psci_smc_psci_set_suspend_mode(psci_mode_t mode);

register_t
psci_smc_psci_stat_count(psci_mpidr_t cpu_id, register_t power_state);
