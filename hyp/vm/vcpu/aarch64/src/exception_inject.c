// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <abort.h>
#include <compiler.h>
#include <log.h>
#include <panic.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include "exception_inject.h"

#if defined(CONFIG_AARCH64_32BIT_EL1)
#error Exception injection to 32-bit EL1 is not implemented
#endif

static void
exception_inject(void)
{
	VBAR_EL1_t     vbar;
	ELR_EL2_t      elr_el2;
	SPSR_EL2_A64_t spsr_el2;
	thread_t *     thread = thread_get_self();
	register_t     guest_vector;

	vbar	     = register_VBAR_EL1_read();
	guest_vector = VBAR_EL1_get_VectorBase(&vbar);

	spsr_el2 = thread->vcpu_regs_gpr.spsr_el2;

	// FIXME: AArch32 bit guest support
	spsr_64bit_mode_t spsr_m = SPSR_EL2_A64_get_M(&spsr_el2);
	switch (spsr_m) {
	case SPSR_64BIT_MODE_EL0T:
		// Exception from 64-bit EL0
		guest_vector += 0x400U;
		break;
	case SPSR_64BIT_MODE_EL1T:
		// Exception from EL1 with SP_EL0
		// No adjustment needed
		break;
	case SPSR_64BIT_MODE_EL1H:
		// Exception from EL1 with SP_EL1
		guest_vector += 0x200U;
		break;
	case SPSR_64BIT_MODE_EL2T:
	case SPSR_64BIT_MODE_EL2H:
		// Illegal mode, panic
		panic("Illegal CPU mode: injecting exception to EL2");
	default:
		// Either illegal M, or exceptions coming from 32-bit EL0/EL1
		// For now we only support 32-bit EL0
		if (spsr_m == SPSR_32BIT_MODE_USER) {
			// Exception from 32-bit EL0 User
			guest_vector += 0x600U;
			break;
		}
		// Illegal mode, panic
		panic("Illegal or unsupported CPU mode");
	}

	// Set up guest's SPSR
	SPSR_EL1_A64_t spsr_el1 = SPSR_EL1_A64_cast(SPSR_EL2_A64_raw(spsr_el2));
	register_SPSR_EL1_A64_write(spsr_el1);

	// Set mode to EL1H, mask DAIF, clear IL and SS
	SPSR_EL2_A64_set_D(&spsr_el2, true);
	SPSR_EL2_A64_set_A(&spsr_el2, true);
	SPSR_EL2_A64_set_I(&spsr_el2, true);
	SPSR_EL2_A64_set_F(&spsr_el2, true);
	SPSR_EL2_A64_set_IL(&spsr_el2, false);
	SPSR_EL2_A64_set_SS(&spsr_el2, false);
	SPSR_EL2_A64_set_M(&spsr_el2, SPSR_64BIT_MODE_EL1H);
	thread->vcpu_regs_gpr.spsr_el2 = spsr_el2;

#if defined(ARCH_ARM_8_0_SSBS) || (ARCH_ARM_VER >= 81) ||                      \
	defined(ARCH_ARM_8_1_PAN)
	SCTLR_EL1_t sctlr_el1 = register_SCTLR_EL1_read();
#if defined(ARCH_ARM_8_0_SSBS)
	SPSR_EL2_A64_set_SSBS(&spsr_el2, SCTLR_EL1_get_DSSBS(&sctlr_el1));
#endif
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_PAN)
	if (!SCTLR_EL1_get_SPAN(&sctlr_el1)) {
		SPSR_EL2_A64_set_PAN(&spsr_el2, true);
	}
#endif
#endif
#if defined(ARCH_ARM_8_2_UAO)
	SPSR_EL2_A64_set_UAO(&spsr_el2, false);
#endif

	// Tell the guest where the exception came from
	elr_el2		  = thread->vcpu_regs_gpr.pc;
	ELR_EL1_t elr_el1 = ELR_EL1_cast(ELR_EL2_raw(elr_el2));
	register_ELR_EL1_write(elr_el1);

	// Return to the guest's vector
	ELR_EL2_set_ReturnAddress(&elr_el2, guest_vector);
	thread->vcpu_regs_gpr.pc = elr_el2;
}

bool
inject_inst_data_abort(ESR_EL2_t esr_el2, esr_ec_t ec, iss_da_ia_fsc_t fsc,
		       FAR_EL2_t far, vmaddr_t ipa, bool is_data_abort)
{
	thread_t *     thread  = thread_get_self();
	SPSR_EL2_A64_t spsr    = thread->vcpu_regs_gpr.spsr_el2;
	ESR_EL1_t      esr_el1 = ESR_EL1_cast(ESR_EL2_raw(esr_el2));

	assert(thread->kind == THREAD_KIND_VCPU);
	assert(thread->addrspace != NULL);

	// Assert EC is instruction/data abort from lower levels
	assert((ec == ESR_EC_INST_ABT_LO) || (ec == ESR_EC_DATA_ABT_LO));

	// Check the reason behind the abort
	switch (fsc) {
	case ISS_DA_IA_FSC_ADDR_SIZE_0:	  // Address size fault - level 0 of
					  // translation or the TTB
	case ISS_DA_IA_FSC_ADDR_SIZE_1:	  // Address size fault - level 1
	case ISS_DA_IA_FSC_ADDR_SIZE_2:	  // Address size fault - level 2
	case ISS_DA_IA_FSC_ADDR_SIZE_3:	  // Address size fault - level 3
	case ISS_DA_IA_FSC_TRANSLATION_0: // Translation fault, level 0
	case ISS_DA_IA_FSC_TRANSLATION_1: // Translation fault, level 1
	case ISS_DA_IA_FSC_TRANSLATION_2: // Translation fault, level 2
	case ISS_DA_IA_FSC_TRANSLATION_3: // Translation fault, level 3
	case ISS_DA_IA_FSC_PERMISSION_1:  // Permission fault, level 1
	case ISS_DA_IA_FSC_PERMISSION_2:  // Permission fault, level 2
	case ISS_DA_IA_FSC_PERMISSION_3:  // Permission fault, level 3
	case ISS_DA_IA_FSC_ALIGNMENT: {	  // Alignment fault
#if !defined(NDEBUG)
		// Injecting an abort from the guest EL1H sync vector will
		// cause an exception inject loop, so block the vcpu instead.
		if (SPSR_EL2_A64_get_M(&spsr) == SPSR_64BIT_MODE_EL1H) {
			VBAR_EL1_t vbar = register_VBAR_EL1_read();
			uint64_t   pc	= ELR_EL2_get_ReturnAddress(
				    &thread->vcpu_regs_gpr.pc);
			uint64_t el1h_sync_vector =
				VBAR_EL1_get_VectorBase(&vbar) + 0x200U;
			if (util_balign_down(pc, 0x80U) == el1h_sync_vector) {
				VTTBR_EL2_t vttbr = register_VTTBR_EL2_read();
				TRACE_AND_LOG(
					DEBUG, INFO,
					"Detected exception inject loop from "
					"VM {:d}, original ESR_EL2 = {:#x}, "
					"ELR_EL2 = {:#x}, VBAR_EL1 = {:#x}",
					VTTBR_EL2_get_VMID(&vttbr),
					ESR_EL2_raw(esr_el2),
					ELR_EL2_raw(thread->vcpu_regs_gpr.pc),
					VBAR_EL1_raw(vbar));
				scheduler_lock(thread);
				scheduler_block(thread,
						SCHEDULER_BLOCK_VCPU_FAULT);
				scheduler_unlock(thread);
				scheduler_yield();
				break;
			}
		}
#endif
		// Inject a synchronous external abort
		spsr_64bit_mode_t mode = SPSR_EL2_A64_get_M(&spsr);
		if ((mode == SPSR_64BIT_MODE_EL1T) ||
		    (mode == SPSR_64BIT_MODE_EL1H)) {
			if (is_data_abort) {
				// Data abort from EL1
				ESR_EL1_set_EC(&esr_el1, ESR_EC_DATA_ABT);
			} else {
				// Instruction abort from EL1
				ESR_EL1_set_EC(&esr_el1, ESR_EC_INST_ABT);
			}
		} else {
			if (is_data_abort) {
				// Data abort from EL0
				ESR_EL1_set_EC(&esr_el1, ESR_EC_DATA_ABT_LO);
			} else {
				// Instruction abort from EL0
				ESR_EL1_set_EC(&esr_el1, ESR_EC_INST_ABT_LO);
			}
		}

		// Change ISS.FSC to synchronous external abort, clear ISV, SSE,
		// SF, AR, EA, S1PTW, SAS and SRT.
		if (is_data_abort) {
			ESR_EL2_ISS_DATA_ABORT_t iss;
			ESR_EL2_ISS_DATA_ABORT_init(&iss);
			ESR_EL2_ISS_DATA_ABORT_set_DFSC(
				&iss, ISS_DA_IA_FSC_SYNC_EXTERNAL);
			ESR_EL1_set_ISS(&esr_el1,
					ESR_EL2_ISS_DATA_ABORT_raw(iss));
		} else {
			ESR_EL2_ISS_INST_ABORT_t iss;
			ESR_EL2_ISS_INST_ABORT_init(&iss);
			ESR_EL2_ISS_INST_ABORT_set_IFSC(
				&iss, ISS_DA_IA_FSC_SYNC_EXTERNAL);
			ESR_EL1_set_ISS(&esr_el1,
					ESR_EL2_ISS_INST_ABORT_raw(iss));
		}

		register_ESR_EL1_write(esr_el1);
		register_FAR_EL1_write(FAR_EL1_cast(FAR_EL2_raw(far)));

		gvaddr_t va = FAR_EL2_get_VirtualAddress(&far);

		TRACE_AND_LOG(DEBUG, INFO,
			      "Injecting instruction/data abort to VM {:d}, "
			      "original ESR_EL2 = {:#x}, fault VA = {:#x}, "
			      "fault IPA = {:#x}, ELR_EL2 = {:#x}",
			      thread->addrspace->vmid, ESR_EL2_raw(esr_el2), va,
			      ipa, ELR_EL2_raw(thread->vcpu_regs_gpr.pc));

		// Inject the fault to the guest
		exception_inject();
		break;
	}
	case ISS_DA_IA_FSC_ACCESS_FLAG_1:
	case ISS_DA_IA_FSC_ACCESS_FLAG_2:
	case ISS_DA_IA_FSC_ACCESS_FLAG_3:
	case ISS_DA_IA_FSC_SYNC_EXTERNAL:
	case ISS_DA_IA_FSC_SYNC_EXTERN_WALK_0:
	case ISS_DA_IA_FSC_SYNC_EXTERN_WALK_1:
	case ISS_DA_IA_FSC_SYNC_EXTERN_WALK_2:
	case ISS_DA_IA_FSC_SYNC_EXTERN_WALK_3:
	case ISS_DA_IA_FSC_SYNC_PARITY_ECC:
	case ISS_DA_IA_FSC_SYNC_PARITY_ECC_WALK_0:
	case ISS_DA_IA_FSC_SYNC_PARITY_ECC_WALK_1:
	case ISS_DA_IA_FSC_SYNC_PARITY_ECC_WALK_2:
	case ISS_DA_IA_FSC_SYNC_PARITY_ECC_WALK_3:
	case ISS_DA_IA_FSC_SYNC_TAG_CHECK:
	case ISS_DA_IA_FSC_TLB_CONFLICT:
	case ISS_DA_IA_FSC_PAGE_DOMAIN:
	case ISS_DA_IA_FSC_SECTION_DOMAIN:
	case ISS_DA_IA_FSC_DEBUG:
	case ISS_DA_IA_FSC_IMP_DEF_LOCKDOWN:
	case ISS_DA_IA_FSC_IMP_DEF_ATOMIC:
	default: {
		gvaddr_t va = FAR_EL2_get_VirtualAddress(&far);

		TRACE_AND_LOG(ERROR, INFO,
			      "instruction/data abort from VM {:d}, "
			      "ESR_EL2 = {:#x}, fault VA = {:#x}, "
			      "fault IPA = {:#x}, ELR_EL2 = {:#x}",
			      thread->addrspace->vmid, ESR_EL2_raw(esr_el2), va,
			      ipa, ELR_EL2_raw(thread->vcpu_regs_gpr.pc));

		// We will get here if this is a:
		// - Access flag fault
		// - TLB walk fault
		// - Section domain fault
		// - Page domain fault
		// - IMPLEMENTATION DEFINED fault
		// Also the following have already been checked by the caller:
		// - TLB conflict
		// - Unsupported atomic hardware update file (ARMv8.1-TTHM)

		abort("Unhandled instruction/data abort",
		      ABORT_REASON_UNHANDLED_EXCEPTION);
	}
	}

	return true;
}

void
inject_undef_abort(ESR_EL2_t esr_el2)
{
	ESR_EL1_t esr_el1;

	ESR_EL1_init(&esr_el1);
	ESR_EL1_set_IL(&esr_el1, ESR_EL2_get_IL(&esr_el2));
	ESR_EL1_set_EC(&esr_el1, ESR_EC_UNKNOWN);
	register_ESR_EL1_write(esr_el1);

	thread_t *thread = thread_get_self();
	TRACE_AND_LOG(ERROR, INFO,
		      "Injecting unknown abort to VM {:d}, "
		      "original ESR_EL2 {:#x}",
		      thread->addrspace->vmid, ESR_EL2_raw(esr_el2),
		      ESR_EL2_raw(esr_el2));

	// Inject the fault to the guest
	exception_inject();
}
