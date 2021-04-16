// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// SMC Trace interface
//
// SMC Trace logs SMC calls and returns for SMC calls, whether called by a user
// or internally in the hypervisor.

#define SMC_TRACE_CURRENT(id, num)                                             \
	static_assert(num <= SMC_TRACE_REG_MAX, "num out of range");           \
	smc_trace_log(id,                                                      \
		      (register_t(*)[SMC_TRACE_REG_MAX]) &                     \
			      thread_get_self()->vcpu_regs_gpr.x[0],           \
		      num)

void
smc_trace_init(partition_t *partition);

void
smc_trace_log(smc_trace_id_t id, register_t (*registers)[SMC_TRACE_REG_MAX],
	      count_t	     num_registers);
