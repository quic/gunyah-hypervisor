// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <hyp_aspace.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <events/vectors.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "trap_dispatch.h"

// Dispatching EL2h synchronous traps
void
vectors_exception_dispatch(kernel_trap_frame_t *frame)
{
	bool handled	     = false;
	bool is_memory_fault = false;

	ESR_EL2_t esr = register_ESR_EL2_read_ordered(&asm_ordering);
	esr_ec_t  ec  = ESR_EL2_get_EC(&esr);
	TRACE(ERROR, WARN, "EL2 exception at PC: {:x} EC: {:x}",
	      ELR_EL2_raw(frame->pc), ec);

	switch (ec) {
	case ESR_EC_UNKNOWN:
		handled = trigger_vectors_trap_unknown_el2_event(frame);
		break;

	case ESR_EC_ILLEGAL:
		handled = trigger_vectors_trap_illegal_state_el2_event();
		break;

	case ESR_EC_INST_ABT:
		is_memory_fault = true;
		handled		= trigger_vectors_trap_pf_abort_el2_event(esr);
		break;

	case ESR_EC_PC_ALIGN:
		is_memory_fault = true;
		handled = trigger_vectors_trap_pc_alignment_fault_el2_event();
		break;

	case ESR_EC_DATA_ABT:
		is_memory_fault = true;
		handled = trigger_vectors_trap_data_abort_el2_event(esr);
		break;

	case ESR_EC_SP_ALIGN:
		handled = trigger_vectors_trap_sp_alignment_fault_el2_event();
		break;

	case ESR_EC_SERROR:
		handled = preempt_abort_dispatch();
		break;

	case ESR_EC_BRK:
		handled = trigger_vectors_trap_brk_el2_event(esr);
		break;

	case ESR_EC_BREAK:
	case ESR_EC_BREAK_LO:
	case ESR_EC_STEP:
	case ESR_EC_STEP_LO:
	case ESR_EC_WATCH:
	case ESR_EC_WATCH_LO:
		panic("EL2 debug trap");
	case ESR_EC_VECTOR32_EL2:
	case ESR_EC_MCRMRC15:
	case ESR_EC_MCRRMRRC15:
	case ESR_EC_MCRMRC14:
	case ESR_EC_VMRS_EL2:
	case ESR_EC_MRRC14:
	case ESR_EC_LDCSTC:
	case ESR_EC_SVC32:
	case ESR_EC_HVC32_EL2:
	case ESR_EC_SMC32_EL2:
	case ESR_EC_FP32:
	case ESR_EC_BKPT:
	case ESR_EC_WFIWFE:
	case ESR_EC_FPEN:
	case ESR_EC_SVC64:
	case ESR_EC_HVC64_EL2:
	case ESR_EC_SMC64_EL2:
#if (ARCH_ARM_VER >= 83) || defined(ARCH_ARM_8_3_PAUTH)
	case ESR_EC_PAUTH:
	case ESR_EC_ERET:
#endif
	case ESR_EC_SYSREG:
#if defined(ARCH_ARM_8_2_SVE)
	case ESR_EC_SVE:
#endif
	case ESR_EC_INST_ABT_LO:
	case ESR_EC_DATA_ABT_LO:
	case ESR_EC_FP64:
	default:
		break;
	}

	if (!handled) {
		uintptr_t pc = ELR_EL2_get_ReturnAddress(&frame->pc);

		if (is_memory_fault) {
			FAR_EL2_t far =
				register_FAR_EL2_read_ordered(&asm_ordering);
			TRACE_AND_LOG(ERROR, WARN,
				      "Unhandled EL2 trap, ESR_EL2 = {:#x}, "
				      "ELR_EL2 = {:#x}, FAR_EL2= {:#x}",
				      ESR_EL2_raw(esr), pc, FAR_EL2_raw(far));
		} else {
			TRACE_AND_LOG(ERROR, WARN,
				      "Unhandled EL2 trap, ESR_EL2 = {:#x}, "
				      "ELR_EL2 = {:#x}",
				      ESR_EL2_raw(esr), pc);
		}
		panic("Unhandled EL2 trap");
	}
}

SPSR_EL2_A64_t
vectors_interrupt_dispatch(void)
{
	SPSR_EL2_A64_t ret = { 0 };

	if (preempt_interrupt_dispatch()) {
		SPSR_EL2_A64_set_I(&ret, true);
	}

	return ret;
}

void
vectors_handle_abort_kernel(void)
{
#if defined(VERBOSE) && VERBOSE
	// HLT instruction will stop if an external debugger is attached,
	// otherwise it generates an exception and the trap handler below will
	// skip the instruction.
	__asm__ volatile("hlt 1" ::: "memory");
#endif
}

bool
vectors_handle_vectors_trap_unknown_el2(kernel_trap_frame_t *frame)
{
	bool	  ret = false;
	uintptr_t pc  = ELR_EL2_get_ReturnAddress(&frame->pc);

	error_t err =
		hyp_aspace_va_to_pa_el2_read((void *)pc, NULL, NULL, NULL);

	if (err != OK) {
		LOG(ERROR, WARN, "EL2 undef instruction bad PC: {:x}", pc);
		goto out;
	}

	assert(util_is_baligned(pc, 4));

	// Read the faulting EL2 instruction
	uint32_t inst = *(uint32_t *)pc;

	if ((inst & AARCH64_INST_EXCEPTION_MASK) ==
	    AARCH64_INST_EXCEPTION_VAL) {
		uint16_t imm16 = (inst & AARCH64_INST_EXCEPTION_IMM16_MASK) >>
				 AARCH64_INST_EXCEPTION_IMM16_SHIFT;

		if ((inst & AARCH64_INST_EXCEPTION_SUBTYPE_MASK) ==
		    AARCH64_INST_EXCEPTION_SUBTYPE_HLT_VAL) {
			LOG(ERROR, WARN,
			    "skipping hlt instruction at PC: {:x}, imm16: {:x}",
			    pc, imm16);

			// Adjust PC past HLT instruction
			ELR_EL2_set_ReturnAddress(&frame->pc, pc + 4U);

			ret = true;
		}
	}

out:
	return ret;
}
