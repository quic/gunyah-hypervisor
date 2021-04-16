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

static inline void
exception_skip_inst(bool is_il32)
{
	thread_t * thread = thread_get_self();
	register_t pc = ELR_EL2_get_ReturnAddress(&thread->vcpu_regs_gpr.pc);

	pc += is_il32 ? 4U : 2U;
	ELR_EL2_set_ReturnAddress(&thread->vcpu_regs_gpr.pc, pc);

#if defined(CONFIG_AARCH64_32BIT_EL1)
#error Adjust ITSTATE if necessary
#endif
}

static bool
handle_tlb_conflict()
{
	// FIXME:
	// First check if a page table update is already in progress. If this is
	// the case, flush the TLBs and return true (to retry the instruction).
	// Otherwise return false.

	return false;
}

static bool
handle_break_before_make()
{
	// FIXME:
	// First check if a page table update is already in progress. If this is
	// the case return true (to retry the instruction). Otherwise return
	// false.

	return false;
}

static vcpu_trap_result_t
handle_inst_data_abort(ESR_EL2_t esr, esr_ec_t ec, FAR_EL2_t far,
		       HPFAR_EL2_t hpfar, iss_da_ia_fsc_t fsc,
		       bool is_data_abort)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	if (fsc == ISS_DA_IA_FSC_TLB_CONFLICT) {
		if (handle_tlb_conflict()) {
			ret = VCPU_TRAP_RESULT_RETRY;
		}
#if defined(ARCH_ARM_8_1_TTHM)
	} else if (fsc == ISS_DA_IA_FSC_ATOMIC_HW_UPDATE) {
		// Unsupported atomic hardware update fail
		if (handle_break_before_make()) {
			ret = VCPU_TRAP_RESULT_RETRY;
		}
#endif
	} else {
		gvaddr_t	va = FAR_EL2_get_VirtualAddress(&far);
		vmaddr_result_t ipa_r;

		if ((fsc == ISS_DA_IA_FSC_ADDR_SIZE_0) ||
		    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_1) ||
		    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_2) ||
		    (fsc == ISS_DA_IA_FSC_ADDR_SIZE_3) ||
		    (fsc == ISS_DA_IA_FSC_TRANSLATION_0) ||
		    (fsc == ISS_DA_IA_FSC_TRANSLATION_1) ||
		    (fsc == ISS_DA_IA_FSC_TRANSLATION_2) ||
		    (fsc == ISS_DA_IA_FSC_TRANSLATION_3) ||
		    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_1) ||
		    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_2) ||
		    (fsc == ISS_DA_IA_FSC_ACCESS_FLAG_3) ||
		    (fsc == ISS_DA_IA_FSC_SYNC_EXTERN_WALK_0) ||
		    (fsc == ISS_DA_IA_FSC_SYNC_EXTERN_WALK_1) ||
		    (fsc == ISS_DA_IA_FSC_SYNC_EXTERN_WALK_2) ||
		    (fsc == ISS_DA_IA_FSC_SYNC_EXTERN_WALK_3)) {
			// HPFAR_EL2 is valid
			ipa_r = vmaddr_result_ok(HPFAR_EL2_get_FIPA(&hpfar) |
						 (va & 0xfff));
		} else {
			// HPFAR_EL2 is invalid, translate
			ipa_r = addrspace_va_to_ipa_read(va);
		}

		// Call the event handlers for the DA/PA
		if (compiler_unexpected(ipa_r.e != OK)) {
			// This can happen if the guest unmapped the faulting
			// VA in stage 1 on another CPU after the stage 2
			// fault was triggered. In that case, we must retry the
			// faulting instruction; it should fault in stage 1.
			ret = VCPU_TRAP_RESULT_RETRY;
		} else if (is_data_abort) {
			ret = trigger_vcpu_trap_data_abort_guest_event(
				esr, ipa_r.r, far);
		} else {
			ret = trigger_vcpu_trap_pf_abort_guest_event(
				esr, ipa_r.r, far);
		}

		// If not handled, check if we are in the middle of a page
		// table update
		if ((ret == VCPU_TRAP_RESULT_UNHANDLED) &&
		    handle_break_before_make()) {
			ret = VCPU_TRAP_RESULT_RETRY;
		}

		// If still not handled inject the abort to the guest
		if ((ret == VCPU_TRAP_RESULT_UNHANDLED) &&
		    inject_inst_data_abort(esr, ec, fsc, far, ipa_r.r,
					   is_data_abort)) {
			ret = VCPU_TRAP_RESULT_RETRY;
		}
	}

	return ret;
}

// Dispatching of guest interrupts
void
vcpu_interrupt_dispatch(void)
{
	trigger_thread_entry_from_user_event(THREAD_ENTRY_REASON_INTERRUPT);

	if (irq_interrupt_dispatch()) {
		assert_preempt_enabled();
		scheduler_schedule();
	}

	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_INTERRUPT);
}

// Dispatching of guest synchronous exceptions and asynchronous system errors
void
vcpu_exception_dispatch(bool is_aarch64)
{
	ESR_EL2_t   esr	  = register_ESR_EL2_read_ordered(&asm_ordering);
	FAR_EL2_t   far	  = register_FAR_EL2_read_ordered(&asm_ordering);
	HPFAR_EL2_t hpfar = register_HPFAR_EL2_read_ordered(&asm_ordering);

	trigger_thread_entry_from_user_event(THREAD_ENTRY_REASON_EXCEPTION);

	vcpu_trap_result_t result = VCPU_TRAP_RESULT_UNHANDLED;

	esr_ec_t ec	 = ESR_EL2_get_EC(&esr);
	bool	 is_il32 = true;
#if defined(CONFIG_AARCH64_32BIT_EL1)
	is_il32 = ESR_EL2_get_IL(&esr);

	// Make sure we didn't get here as AARCH64 with a 16-bit instruction
	assert(!(is_aarch64 && !is_il32));
#else
	assert(is_aarch64);
#endif

	switch (ec) {
	case ESR_EC_MCRMRC15:
	case ESR_EC_MCRRMRRC15:
	case ESR_EC_MCRMRC14:
	case ESR_EC_LDCSTC:
	case ESR_EC_VMRS_EL2:
	case ESR_EC_MRRC14:
	case ESR_EC_SVC32:
	case ESR_EC_HVC32_EL2:
	case ESR_EC_SMC32_EL2:
	case ESR_EC_FP32:
	case ESR_EC_BKPT:
	case ESR_EC_VECTOR32_EL2:
		// FIXME: Handle the traps coming from AArch32
		break;
	case ESR_EC_UNKNOWN:
		result = trigger_vcpu_trap_unknown_event(esr);
		break;

	case ESR_EC_WFIWFE: {
		ESR_EL2_ISS_WFI_WFE_t iss =
			ESR_EL2_ISS_WFI_WFE_cast(ESR_EL2_get_ISS(&esr));
#if defined(CONFIG_AARCH64_32BIT_EL1)
#error Check the condition code
#endif
		if (ESR_EL2_ISS_WFI_WFE_get_TI(&iss)) {
			result = trigger_vcpu_trap_wfe_event(iss);
		} else {
			result = trigger_vcpu_trap_wfi_event(iss);
		}
		break;
	}
	case ESR_EC_FPEN:
#if defined(CONFIG_AARCH64_32BIT_EL1)
#error Check the condition code
#endif
		result = trigger_vcpu_trap_fp_enabled_event(esr);
		break;

#if (ARCH_ARM_VER >= 83) || defined(ARCH_ARM_8_3_PAUTH)
	case ESR_EC_PAUTH:
#error Call the event handlers for this EC
		break;

	case ESR_EC_ERET:
#error Call the event handlers for this EC
		break;
#endif
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
#if defined(ARCH_ARM_8_2_SVE)
	case ESR_EC_SVE:
#error Call the event handlers for this EC
		break;
#endif
	case ESR_EC_INST_ABT_LO: {
		ESR_EL2_ISS_INST_ABORT_t iss =
			ESR_EL2_ISS_INST_ABORT_cast(ESR_EL2_get_ISS(&esr));
		iss_da_ia_fsc_t fsc = ESR_EL2_ISS_INST_ABORT_get_IFSC(&iss);

		result =
			handle_inst_data_abort(esr, ec, far, hpfar, fsc, false);
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
		iss_da_ia_fsc_t fsc = ESR_EL2_ISS_DATA_ABORT_get_DFSC(&iss);

		result = handle_inst_data_abort(esr, ec, far, hpfar, fsc, true);
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

	case ESR_EC_SERROR:
		result = trigger_vcpu_trap_serror_event(esr);
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

	// EL2 traps, we should never get these here
	case ESR_EC_INST_ABT:
	case ESR_EC_DATA_ABT:
	case ESR_EC_BREAK:
	case ESR_EC_STEP:
	case ESR_EC_WATCH:
	case ESR_EC_BRK:
		panic("EL2 trap from the guest vector");
	default:
		panic("Unknown trap EC from the guest vector");
	}

	switch (result) {
	case VCPU_TRAP_RESULT_UNHANDLED: {
		thread_t *thread = thread_get_self();
		TRACE_AND_LOG(ERROR, WARN,
			      "Unhandled trap from VM {:d}, ESR_EL2 = {:#x}, "
			      "ELR_EL2 = {:#x}",
			      thread->addrspace->vmid, ESR_EL2_raw(esr),
			      ELR_EL2_raw(thread->vcpu_regs_gpr.pc));
		inject_undef_abort(esr);
		break;
	}
	case VCPU_TRAP_RESULT_FAULT:
		inject_undef_abort(esr);
		break;
	case VCPU_TRAP_RESULT_EMULATED:
		exception_skip_inst(is_il32);
		break;
	case VCPU_TRAP_RESULT_RETRY:
		// Nothing to do here.
		break;
	}

	trigger_thread_exit_to_user_event(THREAD_ENTRY_REASON_EXCEPTION);
}
