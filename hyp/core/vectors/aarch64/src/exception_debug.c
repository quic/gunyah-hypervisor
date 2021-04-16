// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <trace.h>

#include <asm/barrier.h>

#if !defined(NDEBUG)
void
dump_self_sync_fault(uint64_t *context_addr);

void
dump_self_irq_fault(uint64_t *context_addr);

void
dump_self_fiq_fault(uint64_t *context_addr);

void
dump_self_serror(uint64_t *context_addr);

void
dump_nested_fault(uint64_t *context_addr);

static void
dump_regs(uint64_t *context)
{
	uint32_t i;

	for (i = 0; i < 31; i++) {
		TRACE_AND_LOG(ERROR, INFO, "X{:d} = {:#x}", i, context[i]);
	}

	TRACE_AND_LOG(ERROR, INFO, "SP = {:#x}", context[i]);
	TRACE_AND_LOG(ERROR, INFO, "ELR_EL2 = {:#x}", context[i + 1]);
	TRACE_AND_LOG(ERROR, INFO, "SPSR_EL2 = {:#x}", context[i + 2]);

	ESR_EL2_t esr = register_ESR_EL2_read_ordered(&asm_ordering);
	TRACE_AND_LOG(ERROR, INFO, "ESR_EL2 = {:#x}", ESR_EL2_raw(esr));
}

void
dump_self_sync_fault(uint64_t *context)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t synchronous fault");
	dump_regs(context);
}

void
dump_self_irq_fault(uint64_t *context)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t IRQ");
	dump_regs(context);
}

void
dump_self_fiq_fault(uint64_t *context)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t FIQ fault");
	dump_regs(context);
}

void
dump_self_serror(uint64_t *context)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t SError fault");
	dump_regs(context);
}

void
dump_nested_fault(uint64_t *context)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2 stack fault");
	dump_regs(context);
}

#endif
