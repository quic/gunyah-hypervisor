// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <trace.h>
#include <util.h>

#include <asm/barrier.h>

#include "trap_dispatch.h"

void
dump_self_sync_fault(kernel_trap_frame_full_t *frame);

void
dump_self_irq_fault(kernel_trap_frame_full_t *frame);

void
dump_self_fiq_fault(kernel_trap_frame_full_t *frame);

void
dump_self_serror(kernel_trap_frame_full_t *frame);

void
dump_nested_fault(kernel_trap_frame_full_t *frame);

void
vectors_dump_regs(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, INFO, "Dumping frame at {:#x}", (uintptr_t)frame);
	for (index_t i = 0; i < util_array_size(frame->base.x); i++) {
		TRACE_AND_LOG(ERROR, INFO, "X{:d} = {:#x}", i,
			      frame->base.x[i]);
	}
	TRACE_AND_LOG(ERROR, INFO, "X19 = {:#x}", frame->x19);
	TRACE_AND_LOG(ERROR, INFO, "X20 = {:#x}", frame->x20);
	TRACE_AND_LOG(ERROR, INFO, "X21 = {:#x}", frame->x21);
	TRACE_AND_LOG(ERROR, INFO, "X22 = {:#x}", frame->x22);
	TRACE_AND_LOG(ERROR, INFO, "X23 = {:#x}", frame->x23);
	TRACE_AND_LOG(ERROR, INFO, "X24 = {:#x}", frame->x24);
	TRACE_AND_LOG(ERROR, INFO, "X25 = {:#x}", frame->x25);
	TRACE_AND_LOG(ERROR, INFO, "X26 = {:#x}", frame->x26);
	TRACE_AND_LOG(ERROR, INFO, "X27 = {:#x}", frame->x27);
	TRACE_AND_LOG(ERROR, INFO, "X28 = {:#x}", frame->x28);
	TRACE_AND_LOG(ERROR, INFO, "X29 = {:#x}", frame->base.x29);
	TRACE_AND_LOG(ERROR, INFO, "X30 = {:#x}", frame->base.x30);

	TRACE_AND_LOG(ERROR, INFO, "SP_EL2 = {:#x}",
		      SP_EL2_raw(frame->base.sp_el2));
	TRACE_AND_LOG(ERROR, INFO, "ELR_EL2 = {:#x}",
		      ELR_EL2_raw(frame->base.pc));
	TRACE_AND_LOG(ERROR, INFO, "SPSR_EL2 = {:#x}",
		      SPSR_EL2_A64_raw(frame->base.spsr_el2));

	ESR_EL2_t esr = register_ESR_EL2_read_ordered(&asm_ordering);
	TRACE_AND_LOG(ERROR, INFO, "ESR_EL2 = {:#x}", ESR_EL2_raw(esr));
}

void
dump_self_sync_fault(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t synchronous fault");
	vectors_dump_regs(frame);

	// FIXME:
}

void
dump_self_irq_fault(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t IRQ");
	vectors_dump_regs(frame);

	// FIXME:
}

void
dump_self_fiq_fault(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t FIQ fault");
	vectors_dump_regs(frame);

	// FIXME:
}

void
dump_self_serror(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2t SError fault");
	vectors_dump_regs(frame);

	// FIXME:
}

void
dump_nested_fault(kernel_trap_frame_full_t *frame)
{
	TRACE_AND_LOG(ERROR, WARN, "EL2 stack fault");
	vectors_dump_regs(frame);

	// FIXME:
}
