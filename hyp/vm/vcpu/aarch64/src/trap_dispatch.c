// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <abort.h>
#include <addrspace.h>
#include <compiler.h>
#include <irq.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <scheduler.h>
#include <smc_trace.h>
#include <thread.h>
#include <trace.h>

#include <events/thread.h>
#include <events/vcpu.h>

#include <asm/barrier.h>

#include "exception_dispatch.h"
#include "exception_inject.h"

#if ARCH_AARCH64_32BIT_EL1 && !ARCH_AARCH64_32BIT_EL0
#error invalid CPU config
#endif

static inline void
exception_skip_inst(bool is_il32)
{
	thread_t  *thread = thread_get_self();
	register_t pc = ELR_EL2_get_ReturnAddress(&thread->vcpu_regs_gpr.pc);

#if ARCH_AARCH64_32BIT_EL0
	pc += is_il32 ? 4U : 2U;

	SPSR_EL2_base_t spsr_base = thread->vcpu_regs_gpr.spsr_el2.base;

	if (SPSR_EL2_base_get_M4(&spsr_base)) {
		// Exception was in AArch32 execution. Update PSTATE.IT
		SPSR_EL2_A32_t spsr32 = thread->vcpu_regs_gpr.spsr_el2.a32;
		if (SPSR_EL2_A32_get_T(&spsr32)) {
			uint8_t IT = SPSR_EL2_A32_get_IT(&spsr32);
			if ((IT & 0xfU) == 0x8U) {
				// Was the last instruction in IT block
				IT = 0;
			} else {
				// Otherwise shift bits. This is safe even if
				// not in an IT block.
				IT = (uint8_t)((IT & 0xe0U) |
					       ((IT & 0xfU) << 1U));
			}
			SPSR_EL2_A32_set_IT(&spsr32, IT);

			thread->vcpu_regs_gpr.spsr_el2.a32 = spsr32;
		} else {
			assert(is_il32);
		}
	} else {
		assert(is_il32);
	}
#else
	assert(is_il32);
	pc += 4U;
#endif
	ELR_EL2_set_ReturnAddress(&thread->vcpu_regs_gpr.pc, pc);
}

static vcpu_trap_result_t
handle_inst_data_abort(ESR_EL2_t esr, esr_ec_t ec, FAR_EL2_t far,
		       HPFAR_EL2_t hpfar, iss_da_ia_fsc_t fsc, bool is_s1ptw,
		       bool is_data_abort)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;
	gvaddr_t	   va  = FAR_EL2_get_VirtualAddress(&far);
	vmaddr_result_t	   ipa_r;

	if (is_s1ptw || (fsc == ISS_DA_IA_FSC_ADDR_SIZE_0) ||
	    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_1) ||
	    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_2) ||
	    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_3) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_0) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_1) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_2) ||
	    (fsc == ISS_DA_IA_FSC_TRANSLATION_3) ||
	    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_1) ||
	    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_2) ||
	    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_3)) {
		// HPFAR_EL2 is valid; combine it with the sub-page bits
		// of the VA to find the exact IPA.
		ipa_r = vmaddr_result_ok(HPFAR_EL2_get_FIPA(&hpfar) |
					 (va & 0xfffU));
	} else {
		// HPFAR_EL2 was not set by the fault; we can't rely on it.
		ipa_r = vmaddr_result_error(ERROR_ADDR_INVALID);
	}

	// Call the event handlers for the DA/PA
	if (is_data_abort) {
		ret = trigger_vcpu_trap_data_abort_guest_event(esr, ipa_r, far);
	} else {
		ret = trigger_vcpu_trap_pf_abort_guest_event(esr, ipa_r, far);
	}

	// If faulting or still not handled, inject the abort to the guest
	if (((ret == VCPU_TRAP_RESULT_UNHANDLED) ||
	     (ret == VCPU_TRAP_RESULT_FAULT)) &&
	    inject_inst_data_abort(esr, ec, fsc, far, ipa_r.r, is_data_abort)) {
		ret = VCPU_TRAP_RESULT_RETRY;
	}

	return ret;
}

// Dispatching of guest interrupts
void
vcpu_interrupt_dispatch(void)
{
	trigger_thread_entry_from_user_event(THREAD_ENTRY_REASON_INTERRUPT);

	preempt_disable_in_irq();

	if (irq_interrupt_dispatch()) {
		(void)scheduler_schedule();
	}

	preempt_enable_in_irq();

	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_INTERRUPT);
}

// Dispatching of guest synchronous exceptions
void
vcpu_exception_dispatch(bool is_aarch64)
{
	ESR_EL2_t   esr	  = register_ESR_EL2_read_ordered(&asm_ordering);
	FAR_EL2_t   far	  = register_FAR_EL2_read_ordered(&asm_ordering);
	HPFAR_EL2_t hpfar = register_HPFAR_EL2_read_ordered(&asm_ordering);

	trigger_thread_entry_from_user_event(THREAD_ENTRY_REASON_EXCEPTION);

	bool		   fatal  = false;
	vcpu_trap_result_t result = VCPU_TRAP_RESULT_UNHANDLED;

	esr_ec_t ec	 = ESR_EL2_get_EC(&esr);
	bool	 is_il32 = true;
	// FIXME:
	// For exceptions AArch32 execution, we need to determine whether the
	// trapped instruction passed its condition code. If it did not pass,
	// then skip the instruction. Remember special cases, such as BKPT in
	// IT blocks!
	// The decoding to do this is specific to each ESR_EL2.EC value, and
	// should probably be done within the switch cases below.
#if ARCH_AARCH64_32BIT_EL0
	is_il32 = ESR_EL2_get_IL(&esr);
#endif
#if !ARCH_AARCH64_32BIT_EL1
	assert(is_aarch64);
#endif

	switch (ec) {
	case ESR_EC_UNKNOWN:
		result = trigger_vcpu_trap_unknown_event(esr);
		break;

	case ESR_EC_WFIWFE: {
		ESR_EL2_ISS_WFI_WFE_t iss =
			ESR_EL2_ISS_WFI_WFE_cast(ESR_EL2_get_ISS(&esr));
#if ARCH_AARCH64_32BIT_EL1
		// FIXME:
#error Check the condition code
#endif
		switch (ESR_EL2_ISS_WFI_WFE_get_TI(&iss)) {
		case ISS_WFX_TI_WFE:
			result = trigger_vcpu_trap_wfe_event(iss);
			break;
		case ISS_WFX_TI_WFI:
			result = trigger_vcpu_trap_wfi_event(iss);
			break;
#if defined(ARCH_ARM_FEAT_WFxT)
		// These need events updated to pass timeout for FEAT_WFxT
		// support
		// FIXME:
		case ISS_WFX_TI_WFET:
			result = trigger_vcpu_trap_wfe_event(iss);
			break;
		case ISS_WFX_TI_WFIT:
			result = trigger_vcpu_trap_wfi_event(iss);
			break;
#endif
		default:
			// should not happen
			// result = VCPU_TRAP_RESULT_UNHANDLED
			break;
		}
		break;
	}
	case ESR_EC_FPEN: {
#if ARCH_AARCH64_32BIT_EL1
		// FIXME:
#error Check the condition code
#endif
		result = trigger_vcpu_trap_fp_enabled_event(esr);
		break;
	}

#if defined(ARCH_ARM_FEAT_PAuth)
	case ESR_EC_PAUTH:
		if (trigger_vcpu_trap_pauth_event()) {
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;

#if defined(ARCH_ARM_FEAT_NV)
	case ESR_EC_ERET:
		if (trigger_vcpu_trap_eret_event(esr)) {
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;
#endif
#endif // defined(ARCH_ARM_FEAT_FPAC)

	case ESR_EC_ILLEGAL:
		if (trigger_vcpu_trap_illegal_state_event()) {
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;

	case ESR_EC_SVC64:
		if (trigger_vcpu_trap_svc64_event(esr)) {
			// SVC is not an exception generating instruction for
			// EL2; it is trapped, and therefore the preferred
			// return address is the instruction itself. So, we
			// treat success as an emulated instruction so the PC
			// will be advanced in software.
			result = VCPU_TRAP_RESULT_EMULATED;
		}
		break;

	case ESR_EC_HVC64_EL2: {
		ESR_EL2_ISS_HVC_t iss =
			ESR_EL2_ISS_HVC_cast(ESR_EL2_get_ISS(&esr));
		if (trigger_vcpu_trap_hvc64_event(iss)) {
			// HVC is an exception generating instruction for EL2;
			// the preferred return address is the next instruction.
			// So, we treat success as a retry so the PC will not be
			// advanced again in software.
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;
	}

	case ESR_EC_SMC64_EL2: {
		ESR_EL2_ISS_SMC64_t iss =
			ESR_EL2_ISS_SMC64_cast(ESR_EL2_get_ISS(&esr));

		SMC_TRACE_CURRENT(SMC_TRACE_ID_EL1_64ENT, 8);

		if (trigger_vcpu_trap_smc64_event(iss)) {
			// SMC is not an exception generating instruction for
			// EL2; it is trapped, and therefore the preferred
			// return address is the instruction itself. So, we
			// treat success as an emulated instruction so the PC
			// will be advanced in software.
			result = VCPU_TRAP_RESULT_EMULATED;

			SMC_TRACE_CURRENT(SMC_TRACE_ID_EL1_64RET, 7);
		}

		break;
	}

	case ESR_EC_SYSREG: {
		ESR_EL2_ISS_MSR_MRS_t iss =
			ESR_EL2_ISS_MSR_MRS_cast(ESR_EL2_get_ISS(&esr));
		if (ESR_EL2_ISS_MSR_MRS_get_Direction(&iss)) {
			result = trigger_vcpu_trap_sysreg_read_event(iss);
		} else {
			result = trigger_vcpu_trap_sysreg_write_event(iss);
		}
		break;
	}
#if defined(ARCH_ARM_FEAT_SVE)
	case ESR_EC_SVE:
		result = trigger_vcpu_trap_sve_access_event();
		break;
#endif
	case ESR_EC_INST_ABT_LO: {
		ESR_EL2_ISS_INST_ABORT_t iss =
			ESR_EL2_ISS_INST_ABORT_cast(ESR_EL2_get_ISS(&esr));
		iss_da_ia_fsc_t fsc   = ESR_EL2_ISS_INST_ABORT_get_IFSC(&iss);
		bool		s1ptw = ESR_EL2_ISS_INST_ABORT_get_S1PTW(&iss);

		result = handle_inst_data_abort(esr, ec, far, hpfar, fsc, s1ptw,
						false);
		break;
	}
	case ESR_EC_PC_ALIGN:
		if (trigger_vcpu_trap_pc_alignment_fault_event()) {
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;

	case ESR_EC_DATA_ABT_LO: {
		ESR_EL2_ISS_DATA_ABORT_t iss =
			ESR_EL2_ISS_DATA_ABORT_cast(ESR_EL2_get_ISS(&esr));
		iss_da_ia_fsc_t fsc   = ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);
		bool		s1ptw = ESR_EL2_ISS_DATA_ABORT_get_S1PTW(&iss);

		result = handle_inst_data_abort(esr, ec, far, hpfar, fsc, s1ptw,
						true);
		break;
	}
	case ESR_EC_SP_ALIGN:
		if (trigger_vcpu_trap_sp_alignment_fault_event()) {
			result = VCPU_TRAP_RESULT_RETRY;
		}
		break;

	case ESR_EC_FP64:
		result = trigger_vcpu_trap_fp64_event(esr);
		break;

	case ESR_EC_BREAK_LO:
		result = trigger_vcpu_trap_breakpoint_guest_event(esr);
		break;

	case ESR_EC_STEP_LO:
		result = trigger_vcpu_trap_software_step_guest_event(esr);
		break;

	case ESR_EC_WATCH_LO:
		result = trigger_vcpu_trap_watchpoint_guest_event(esr);
		break;

	case ESR_EC_BRK:
		result = trigger_vcpu_trap_brk_instruction_guest_event(esr);
		break;

		/* AArch32 traps which may come from EL0/1 */
#if ARCH_AARCH64_32BIT_EL0
	case ESR_EC_LDCSTC: {
		ESR_EL2_ISS_LDC_STC_t iss =
			ESR_EL2_ISS_LDC_STC_cast(ESR_EL2_get_ISS(&esr));
		result = trigger_vcpu_trap_ldcstc_guest_event(iss);
		break;
	}
	case ESR_EC_MCRMRC14: {
		ESR_EL2_ISS_MCR_MRC_t iss =
			ESR_EL2_ISS_MCR_MRC_cast(ESR_EL2_get_ISS(&esr));
		result = trigger_vcpu_trap_mcrmrc14_guest_event(iss);
		break;
	}
	case ESR_EC_MCRMRC15:
		result = trigger_vcpu_trap_mcrmrc15_guest_event(esr);
		break;
	case ESR_EC_MCRRMRRC15:
		result = trigger_vcpu_trap_mcrrmrrc15_guest_event(esr);
		break;
	case ESR_EC_MRRC14:
		result = trigger_vcpu_trap_mrrc14_guest_event(esr);
		break;
	case ESR_EC_BKPT:
		result = trigger_vcpu_trap_bkpt_guest_event(esr);
		break;
#else
	case ESR_EC_LDCSTC:
	case ESR_EC_MCRMRC14:
	case ESR_EC_MCRMRC15:
	case ESR_EC_MCRRMRRC15:
	case ESR_EC_MRRC14:
	case ESR_EC_BKPT:
		break;
#endif
	/* Asynchronous traps which don't come through this path */
	case ESR_EC_SERROR:
	/* AArch32 traps which may come when TGE=1 */
	case ESR_EC_FP32:
	/* AArch32 traps which may come only from EL1 */
	case ESR_EC_VMRS_EL2:
	case ESR_EC_SVC32:
	case ESR_EC_HVC32_EL2:
	case ESR_EC_SMC32_EL2:
	case ESR_EC_VECTOR32_EL2:
		// FIXME: Handle the traps coming from AArch32 EL1
		break;

	// EL2 traps, we should never get these here
	case ESR_EC_INST_ABT:
	case ESR_EC_DATA_ABT:
	case ESR_EC_BREAK:
	case ESR_EC_STEP:
	case ESR_EC_WATCH:
#if defined(ARCH_ARM_FEAT_BTI)
	case ESR_EC_BTI:
#endif
#if defined(ARCH_ARM_FEAT_PAuth) && defined(ARCH_ARM_FEAT_FPAC)
	case ESR_EC_FPAC:
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
#if defined(VERBOSE) && VERBOSE
	// Cause a fatal error in verbose builds so we can detect any unhandled
	// ECs
	default:
#endif
		fatal = true;
		break;
#if !(defined(VERBOSE) && VERBOSE)
	// On non-verbose builds pass the unexpected ECs back to the VM
	default:
		result = VCPU_TRAP_RESULT_UNHANDLED;
		break;
#endif
	}

	thread_t *thread = thread_get_self();

	if (compiler_unexpected(fatal)) {
		TRACE_AND_LOG(ERROR, WARN,
			      "Unexpected trap from VM {:d}, ESR_EL2 = {:#x}, "
			      "ELR_EL2 = {:#x}",
			      thread->addrspace->vmid, ESR_EL2_raw(esr),
			      ELR_EL2_raw(thread->vcpu_regs_gpr.pc));
		abort("Unexpected guest trap",
		      ABORT_REASON_UNHANDLED_EXCEPTION);
	}

	switch (result) {
	case VCPU_TRAP_RESULT_UNHANDLED:
		TRACE_AND_LOG(ERROR, WARN,
			      "Unhandled trap from VM {:d}, ESR_EL2 = {:#x}, "
			      "ELR_EL2 = {:#x}",
			      thread->addrspace->vmid, ESR_EL2_raw(esr),
			      ELR_EL2_raw(thread->vcpu_regs_gpr.pc));
		inject_undef_abort(esr);
		break;
	case VCPU_TRAP_RESULT_FAULT:
		inject_undef_abort(esr);
		break;
	case VCPU_TRAP_RESULT_EMULATED:
		exception_skip_inst(is_il32);
		break;
	case VCPU_TRAP_RESULT_RETRY:
	default:
		// Nothing to do here.
		break;
	}

	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_EXCEPTION);
}

// Dispatching of guest asynchronous system errors
void
vcpu_error_dispatch(void)
{
	ESR_EL2_t esr = register_ESR_EL2_read_ordered(&asm_ordering);

	trigger_thread_entry_from_user_event(THREAD_ENTRY_REASON_INTERRUPT);

	preempt_disable_in_irq();

	ESR_EL2_ISS_SERROR_t iss =
		ESR_EL2_ISS_SERROR_cast(ESR_EL2_get_ISS(&esr));
	(void)trigger_vcpu_trap_serror_event(iss);

	preempt_enable_in_irq();

	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_INTERRUPT);
}
