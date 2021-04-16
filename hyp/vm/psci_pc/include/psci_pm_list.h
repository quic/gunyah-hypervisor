// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Initialize all per core vcpu pm list
void
psci_pm_list_init(void);

// Get the current cpu list of vcpus that participate in power management
// decisions
list_t *
psci_pm_list_get_self(void);

// Add vcpu to specified cpu pm list
void
psci_pm_list_insert(cpu_index_t cpu_index, thread_t *vcpu);

// Remove vcpu to specified cpu pm list
void
psci_pm_list_delete(cpu_index_t cpu_index, thread_t *vcpu);
