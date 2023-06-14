// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
etm_set_reg(cpu_index_t cpu, size_t offset, register_t val, size_t access_size);

void
etm_get_reg(cpu_index_t cpu, size_t offset, register_t *val,
	    size_t access_size);

void
etm_save_context_percpu(cpu_index_t cpu);

void
etm_restore_context_percpu(cpu_index_t cpu);
