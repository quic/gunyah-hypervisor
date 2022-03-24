// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdatomic.h>
#include <string.h>

#include <hypconstants.h>

#include <cpulocal.h>
#include <panic.h>
#include <partition.h>
#include <preempt.h>
#include <smc_trace.h>
#include <thread.h>
#include <util.h>

#include <asm/timestamp.h>

#include <asm-generic/prefetch.h>

extern smc_trace_t *hyp_smc_trace;
smc_trace_t	    *hyp_smc_trace;

void
smc_trace_init(partition_t *partition)
{
	if (hyp_smc_trace != NULL) {
		panic("smc_trace_init already initialized");
	}

	void_ptr_result_t alloc_ret = partition_alloc(
		partition, sizeof(smc_trace_t), alignof(smc_trace_t));
	if (alloc_ret.e != OK) {
		panic("Error allocating smc trace buffer");
	}

	hyp_smc_trace = (smc_trace_t *)alloc_ret.r;
	memset(hyp_smc_trace, 0, sizeof(*hyp_smc_trace));
}

void
smc_trace_log(smc_trace_id_t id, register_t (*registers)[SMC_TRACE_REG_MAX],
	      count_t	     num_registers)
{
	if (hyp_smc_trace == NULL) {
		goto out;
	}

	assert(num_registers <= SMC_TRACE_REG_MAX);
	uint64_t timestamp = arch_get_timestamp();

	cpu_index_t pcpu = cpulocal_get_index();
	cpu_index_t vcpu = 0U;
	vmid_t	    vmid = 0U;

#if defined(INTERFACE_VCPU)
	thread_t *current = thread_get_self();

	if (current->kind == THREAD_KIND_VCPU) {
		assert(current->addrspace != NULL);
		vmid = current->addrspace->vmid;
		vcpu = current->psci_index;
	}
#endif

	index_t cur_idx = atomic_fetch_add_explicit(&hyp_smc_trace->next_idx, 1,
						    memory_order_consume);
	if (cur_idx >= HYP_SMC_LOG_NUM) {
		index_t next_idx = cur_idx + 1;
		cur_idx -= HYP_SMC_LOG_NUM;
		(void)atomic_compare_exchange_strong_explicit(
			&hyp_smc_trace->next_idx, &next_idx, cur_idx + 1,
			memory_order_relaxed, memory_order_relaxed);
	}
	assert(cur_idx < HYP_SMC_LOG_NUM);

	smc_trace_entry_t *entry = &hyp_smc_trace->entries[cur_idx];

	// Reduce likelihood of half-written trace entries being dumped.
	preempt_disable();

	prefetch_store_stream(entry);

	entry->id	 = (uint8_t)id;
	entry->pcpu	 = (uint8_t)pcpu;
	entry->vcpu	 = (uint8_t)vcpu;
	entry->vmid	 = vmid;
	entry->regs	 = (uint8_t)num_registers;
	entry->timestamp = timestamp;

	num_registers = util_min(num_registers, SMC_TRACE_REG_MAX);

	for (count_t i = 0; i < num_registers; i++) {
		entry->x[i] = (*registers)[i];
	}
	for (count_t i = num_registers; i < SMC_TRACE_REG_MAX; i++) {
		entry->x[i] = 0U;
	}

	preempt_enable();

out:
	return;
}
