// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <attributes.h>
#include <compiler.h>
#include <cpulocal.h>
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

#if defined(ARCH_ARM_FEAT_PAuth)
static inline uintptr_t
remove_pointer_auth(uintptr_t addr)
{
	__asm__("xpaci %0" : "+r"(addr));
	return addr;
}
#endif

static inline uintptr_t
vectors_get_return_address(kernel_trap_frame_t *frame)
{
#if defined(ARCH_ARM_FEAT_PAuth)
	return remove_pointer_auth(ELR_EL2_get_ReturnAddress(&frame->pc));
#else
	return ELR_EL2_get_ReturnAddress(&frame->pc);
#endif
}

#if defined(ARCH_ARM_FEAT_PAuth)
static inline ALWAYS_INLINE uintptr_t
sign_pc_using_framepointer(uintptr_t pc, uintptr_t fp)
{
	// The new PC needs to be signed with a modifier equal to the value the
	// SP will have after restoring the frame, i.e. the address immediately
	// after the end of the frame. Note that this must be inlined and BTI
	// must be enabled to avoid providing a gadget for signing an arbitrary
	// return address.
	__asm__("pacia %0, %1" : "+r"(pc) : "r"(fp));
	return pc;
}
#endif

static inline ALWAYS_INLINE void
vectors_set_return_address(kernel_trap_frame_t *frame, uintptr_t pc)
{
#if defined(ARCH_ARM_FEAT_PAuth)
	ELR_EL2_set_ReturnAddress(
		&frame->pc,
		sign_pc_using_framepointer(
			pc, (uintptr_t)(SP_EL2_raw(frame->sp_el2))));
#else
	ELR_EL2_set_ReturnAddress(&frame->pc, pc);
#endif
}

// Dispatching EL2h synchronous traps
void
vectors_exception_dispatch(kernel_trap_frame_full_t *frame)
{
	bool	    handled	    = false;
	bool	    is_memory_fault = false;
	cpu_index_t cpu		    = cpulocal_get_index();

	ESR_EL2_t esr = register_ESR_EL2_read_ordered(&asm_ordering);
	esr_ec_t  ec  = ESR_EL2_get_EC(&esr);
	uintptr_t pc  = ELR_EL2_get_ReturnAddress(&frame->base.pc);
#if defined(ARCH_ARM_FEAT_PAuth)
	pc = remove_pointer_auth(pc);
#endif
	TRACE(ERROR, WARN,
	      "EL2 exception at PC = {:x} ESR_EL2 = {:#x}, LR = {:#x}, "
	      "SP = {:#x}, FP = {:#x}",
	      pc, ESR_EL2_raw(esr), frame->base.x30,
	      SP_EL2_raw(frame->base.sp_el2), frame->base.x29);

	switch (ec) {
	case ESR_EC_UNKNOWN:
		handled = trigger_vectors_trap_unknown_el2_event(&frame->base);
		break;

#if defined(ARCH_ARM_FEAT_BTI)
	case ESR_EC_BTI:
		TRACE_AND_LOG(ERROR, WARN,
			      "BTI abort in EL2 on CPU {:d}, from {:#x}, "
			      "LR = {:#x}, ESR_EL2 = {:#x}",
			      cpu, pc, frame->base.x30, ESR_EL2_raw(esr));
		panic("BTI abort in EL2");
#endif

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

#if defined(ARCH_ARM_FEAT_PAuth) && defined(ARCH_ARM_FEAT_FPAC)
	case ESR_EC_FPAC:
		handled = trigger_vectors_trap_pauth_failed_el2_event(esr);
		break;
#endif

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
	case ESR_EC_INST_ABT_LO:
	case ESR_EC_DATA_ABT_LO:
	case ESR_EC_FP64:
#if defined(ARCH_ARM_FEAT_PAuth)
	case ESR_EC_PAUTH:
#endif
#if defined(ARCH_ARM_FEAT_PAuth) && defined(ARCH_ARM_FEAT_NV)
	case ESR_EC_ERET:
#endif
	case ESR_EC_SYSREG:
#if defined(ARCH_ARM_FEAT_SVE)
	case ESR_EC_SVE:
#endif
#if defined(ARCH_ARM_FEAT_LS64)
	case ESR_EC_LD64B_ST64B:
#endif
#if defined(ARCH_ARM_FEAT_TME)
	case ESR_EC_TSTART:
#endif
#if defined(ARCH_ARM_FEAT_SME)
	case ESR_EC_SME:
#endif
#if defined(ARCH_ARM_FEAT_RME)
	case ESR_EC_RME:
#endif
#if defined(ARCH_ARM_FEAT_MOPS)
	case ESR_EC_MOPS:
#endif
	default:
		// Unexpected trap, fall through to panic
		break;
	}

	if (!handled) {
		if (is_memory_fault) {
			FAR_EL2_t far =
				register_FAR_EL2_read_ordered(&asm_ordering);
			TRACE_AND_LOG(ERROR, WARN,
				      "Unhandled EL2 trap on CPU {:d}, "
				      "ESR_EL2 = {:#x}, ELR_EL2 = {:#x}, "
				      "FAR_EL2 = {:#x}",
				      cpu, ESR_EL2_raw(esr), pc,
				      FAR_EL2_raw(far));
		} else {
			TRACE_AND_LOG(ERROR, WARN,
				      "Unhandled EL2 trap on CPU {:d}, "
				      "ESR_EL2 = {:#x}, ELR_EL2 = {:#x}",
				      cpu, ESR_EL2_raw(esr), pc);
		}

		vectors_dump_regs(frame);
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
#if !defined(QQVP_SIMULATION_PLATFORM) || !QQVP_SIMULATION_PLATFORM
	// The QQVP model exits with invalid instruction here.
	// This should be resolved in model.
	__asm__ volatile("hlt 1" ::: "memory");
#endif
#endif
}

bool
vectors_handle_vectors_trap_unknown_el2(kernel_trap_frame_t *frame)
{
	bool	  ret = false;
	uintptr_t pc  = vectors_get_return_address(frame);

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
		uint16_t imm16 =
			(uint16_t)((inst & AARCH64_INST_EXCEPTION_IMM16_MASK) >>
				   AARCH64_INST_EXCEPTION_IMM16_SHIFT);

		if ((inst & AARCH64_INST_EXCEPTION_SUBTYPE_MASK) ==
		    AARCH64_INST_EXCEPTION_SUBTYPE_HLT_VAL) {
			LOG(ERROR, WARN,
			    "skipping hlt instruction at PC: {:x}, imm16: {:x}",
			    pc, imm16);

			// Adjust PC past HLT instruction
			vectors_set_return_address(frame, pc + 4U);

			ret = true;
		}
	}

out:
	return ret;
}
