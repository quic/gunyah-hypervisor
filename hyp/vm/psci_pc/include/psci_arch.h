// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

psci_mpidr_t
psci_thread_get_mpidr(thread_t *thread);

psci_mpidr_t
psci_thread_set_mpidr_by_index(thread_t *thread, cpu_index_t index);

bool
psci_pc_handle_trapped_idle(void);
