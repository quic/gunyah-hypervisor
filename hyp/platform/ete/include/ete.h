// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
ete_save_context_percpu(cpu_index_t cpu, bool may_poweroff);

void
ete_restore_context_percpu(cpu_index_t cpu, bool was_poweroff);
