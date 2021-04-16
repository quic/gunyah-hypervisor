// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <scheduler.h>
#include <thread.h>

#include <asm/sysregs.h>

#include "event_handlers.h"

extern uintptr_t vcpu_aarch64_vectors;

void
vcpu_context_switch_load(void)
{
	static_assert(VCPU_TRACE_CONTEXT_SAVED && VCPU_DEBUG_CONTEXT_SAVED,
		      "AArch64 VCPUs must context-switch trace & debug state");

	thread_t *thread = thread_get_self();

	CONTEXTIDR_EL2_t ctxidr = CONTEXTIDR_EL2_default();
	CONTEXTIDR_EL2_set_PROCID(&ctxidr, (uint32_t)(uintptr_t)thread);

	register_CONTEXTIDR_EL2_write(ctxidr);

	if (thread->kind == THREAD_KIND_VCPU) {
		register_CPACR_EL1_write(thread->vcpu_regs_el1.cpacr_el1);
		register_CSSELR_EL1_write(thread->vcpu_regs_el1.csselr_el1);
		register_CONTEXTIDR_EL1_write(
			thread->vcpu_regs_el1.contextidr_el1);
		register_ELR_EL1_write(thread->vcpu_regs_el1.elr_el1);
		register_ESR_EL1_write(thread->vcpu_regs_el1.esr_el1);
		register_FAR_EL1_write(thread->vcpu_regs_el1.far_el1);
		register_PAR_EL1_RAW_write(thread->vcpu_regs_el1.par_el1);
		register_MAIR_EL1_write(thread->vcpu_regs_el1.mair_el1);
		register_SCTLR_EL1_write(thread->vcpu_regs_el1.sctlr_el1);
		register_SP_EL0_write(thread->vcpu_regs_el1.sp_el0);
		register_SP_EL1_write(thread->vcpu_regs_el1.sp_el1);
		register_SPSR_EL1_A64_write(thread->vcpu_regs_el1.spsr_el1);
		register_TCR_EL1_write(thread->vcpu_regs_el1.tcr_el1);
		register_TPIDR_EL0_write(thread->vcpu_regs_el1.tpidr_el0);
		register_TPIDR_EL1_write(thread->vcpu_regs_el1.tpidr_el1);
		register_TPIDRRO_EL0_write(thread->vcpu_regs_el1.tpidrro_el0);
		register_TTBR0_EL1_write(thread->vcpu_regs_el1.ttbr0_el1);
		register_TTBR1_EL1_write(thread->vcpu_regs_el1.ttbr1_el1);
		register_VBAR_EL1_write(thread->vcpu_regs_el1.vbar_el1);
		register_VMPIDR_EL2_write(thread->vcpu_regs_mpidr_el1);
#if SCHEDULER_CAN_MIGRATE
		register_VPIDR_EL2_write(thread->vcpu_regs_midr_el1);
#endif

		register_CPTR_EL2_E2H1_write(thread->vcpu_regs_el2.cptr_el2);
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_VHE)
		assert(HCR_EL2_get_E2H(&thread->vcpu_regs_el2.hcr_el2) ==
		       ARCH_AARCH64_USE_VHE);
		assert(HCR_EL2_get_TGE(&thread->vcpu_regs_el2.hcr_el2) == 0);
#endif
		register_HCR_EL2_write(thread->vcpu_regs_el2.hcr_el2);
		register_MDCR_EL2_write(thread->vcpu_regs_el2.mdcr_el2);

		// FIXME: check disabled until Stage-2 is enabled
		// assert(HCR_EL2_get_VM(&thread->vcpu_regs_el2.hcr_el2) == 1);
		register_VBAR_EL2_write(
			VBAR_EL2_cast((uint64_t)&vcpu_aarch64_vectors));

		register_FPCR_write(thread->vcpu_regs_fpr.fpcr);
		register_FPSR_write(thread->vcpu_regs_fpr.fpsr);

		__asm__ volatile("ldp	q0, q1, [%[q]]		;"
				 "ldp	q2, q3, [%[q], 32]	;"
				 "ldp	q4, q5, [%[q], 64]	;"
				 "ldp	q6, q7, [%[q], 96]	;"
				 "ldp	q8, q9, [%[q], 128]	;"
				 "ldp	q10, q11, [%[q], 160]	;"
				 "ldp	q12, q13, [%[q], 192]	;"
				 "ldp	q14, q15, [%[q], 224]	;"
				 "ldp	q16, q17, [%[q], 256]	;"
				 "ldp	q18, q19, [%[q], 288]	;"
				 "ldp	q20, q21, [%[q], 320]	;"
				 "ldp	q22, q23, [%[q], 352]	;"
				 "ldp	q24, q25, [%[q], 384]	;"
				 "ldp	q26, q27, [%[q], 416]	;"
				 "ldp	q28, q29, [%[q], 448]	;"
				 "ldp	q30, q31, [%[q], 480]	;"
				 :
				 : [q] "r"(thread->vcpu_regs_fpr.q),
				   "m"(thread->vcpu_regs_fpr));
	} else {
		// Set the constant non-VCPU HCR
		HCR_EL2_t nonvm_hcr = HCR_EL2_default();
		HCR_EL2_set_FMO(&nonvm_hcr, true);
		HCR_EL2_set_IMO(&nonvm_hcr, true);
		HCR_EL2_set_AMO(&nonvm_hcr, true);
#if (ARCH_ARM_VER >= 81) || defined(ARCH_ARM_8_1_VHE)
		HCR_EL2_set_E2H(&nonvm_hcr, ARCH_AARCH64_USE_VHE);
#endif
		HCR_EL2_set_TGE(&nonvm_hcr, true);
		register_HCR_EL2_write(nonvm_hcr);
	}
}

void
vcpu_context_switch_save(void)
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU &&
	    !scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF)) {
		thread->vcpu_regs_el1.cpacr_el1	 = register_CPACR_EL1_read();
		thread->vcpu_regs_el1.csselr_el1 = register_CSSELR_EL1_read();
		thread->vcpu_regs_el1.contextidr_el1 =
			register_CONTEXTIDR_EL1_read();
		thread->vcpu_regs_el1.elr_el1	= register_ELR_EL1_read();
		thread->vcpu_regs_el1.esr_el1	= register_ESR_EL1_read();
		thread->vcpu_regs_el1.far_el1	= register_FAR_EL1_read();
		thread->vcpu_regs_el1.par_el1	= register_PAR_EL1_RAW_read();
		thread->vcpu_regs_el1.mair_el1	= register_MAIR_EL1_read();
		thread->vcpu_regs_el1.sctlr_el1 = register_SCTLR_EL1_read();
		thread->vcpu_regs_el1.sp_el1	= register_SP_EL1_read();
		thread->vcpu_regs_el1.sp_el0	= register_SP_EL0_read();
		thread->vcpu_regs_el1.spsr_el1	= register_SPSR_EL1_A64_read();
		thread->vcpu_regs_el1.tcr_el1	= register_TCR_EL1_read();
		thread->vcpu_regs_el1.tpidr_el0 = register_TPIDR_EL0_read();
		thread->vcpu_regs_el1.tpidr_el1 = register_TPIDR_EL1_read();
		thread->vcpu_regs_el1.tpidrro_el0 = register_TPIDRRO_EL0_read();
		thread->vcpu_regs_el1.ttbr0_el1	  = register_TTBR0_EL1_read();
		thread->vcpu_regs_el1.ttbr1_el1	  = register_TTBR1_EL1_read();
		thread->vcpu_regs_el1.vbar_el1	  = register_VBAR_EL1_read();
		thread->vcpu_regs_mpidr_el1	  = register_VMPIDR_EL2_read();
#if SCHEDULER_CAN_MIGRATE
		thread->vcpu_regs_midr_el1 = register_VPIDR_EL2_read();
#endif

		thread->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		thread->vcpu_regs_fpr.fpcr    = register_FPCR_read();
		thread->vcpu_regs_fpr.fpsr    = register_FPSR_read();

		__asm__ volatile("stp	q0, q1, [%[q]]		;"
				 "stp	q2, q3, [%[q], 32]	;"
				 "stp	q4, q5, [%[q], 64]	;"
				 "stp	q6, q7, [%[q], 96]	;"
				 "stp	q8, q9, [%[q], 128]	;"
				 "stp	q10, q11, [%[q], 160]	;"
				 "stp	q12, q13, [%[q], 192]	;"
				 "stp	q14, q15, [%[q], 224]	;"
				 "stp	q16, q17, [%[q], 256]	;"
				 "stp	q18, q19, [%[q], 288]	;"
				 "stp	q20, q21, [%[q], 320]	;"
				 "stp	q22, q23, [%[q], 352]	;"
				 "stp	q24, q25, [%[q], 384]	;"
				 "stp	q26, q27, [%[q], 416]	;"
				 "stp	q28, q29, [%[q], 448]	;"
				 "stp	q30, q31, [%[q], 480]	;"
				 : "=m"(thread->vcpu_regs_fpr)
				 : [q] "r"(thread->vcpu_regs_fpr.q));
	}
}
