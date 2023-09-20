// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <scheduler.h>
#include <thread.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>

#include "event_handlers.h"
#include "vectors_vcpu.h"

// VCPU_TRACE_CONTEXT_SAVED and VCPU_DEBUG_CONTEXT_SAVED defines are used as
// sanity checks to test whether the configuration is correct and we don't leak
// trace and debug context registers between VMs, or permit tracing the
// hypervisor.
#if defined(MODULE_VM_VETM) || defined(MODULE_VM_VETM_NULL)
#define VCPU_TRACE_CONTEXT_SAVED 1
#elif defined(PLATFORM_HAS_NO_ETM_BASE) && (PLATFORM_HAS_NO_ETM_BASE != 0)
#pragma message(                                                               \
	"PLATFORM_HAS_NO_ETM_BASE is nonzero; if an ETM is present it may be"  \
	" accessible and could trace the hypervisor.")
#define VCPU_TRACE_CONTEXT_SAVED 1
#endif

void
vcpu_context_switch_load(void)
{
	static_assert(VCPU_TRACE_CONTEXT_SAVED && VCPU_DEBUG_CONTEXT_SAVED,
		      "AArch64 VCPUs must context-switch trace & debug state");

	thread_t *thread = thread_get_self();

#if defined(ARCH_ARM_FEAT_VHE)
	CONTEXTIDR_EL2_t ctxidr = CONTEXTIDR_EL2_default();
	CONTEXTIDR_EL2_set_PROCID(&ctxidr, (uint32_t)(uintptr_t)thread);
	register_CONTEXTIDR_EL2_write(ctxidr);
#endif

	if (compiler_expected(thread->kind == THREAD_KIND_VCPU)) {
		register_CPACR_EL1_write(thread->vcpu_regs_el1.cpacr_el1);
		register_CSSELR_EL1_write(thread->vcpu_regs_el1.csselr_el1);
		register_CONTEXTIDR_EL1_write(
			thread->vcpu_regs_el1.contextidr_el1);
		register_ELR_EL1_write(thread->vcpu_regs_el1.elr_el1);
		register_ESR_EL1_write(thread->vcpu_regs_el1.esr_el1);
		register_FAR_EL1_write(thread->vcpu_regs_el1.far_el1);
		register_PAR_EL1_base_write(thread->vcpu_regs_el1.par_el1.base);
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
#if !defined(CPU_HAS_NO_ACTLR_EL1)
		register_ACTLR_EL1_write(thread->vcpu_regs_el1.actlr_el1);
#endif
#if !defined(CPU_HAS_NO_AMAIR_EL1)
		register_AMAIR_EL1_write(thread->vcpu_regs_el1.amair_el1);
#endif
#if !defined(CPU_HAS_NO_AFSR0_EL1)
		register_AFSR0_EL1_write(thread->vcpu_regs_el1.afsr0_el1);
#endif
#if !defined(CPU_HAS_NO_AFSR1_EL1)
		register_AFSR1_EL1_write(thread->vcpu_regs_el1.afsr1_el1);
#endif

		// Floating-point access should not be disabled for any VM
#if defined(ARCH_ARM_FEAT_VHE)
		assert_debug(CPTR_EL2_E2H1_get_FPEN(
				     &thread->vcpu_regs_el2.cptr_el2) == 3);
		register_CPTR_EL2_E2H1_write(thread->vcpu_regs_el2.cptr_el2);
#else
		assert_debug(CPTR_EL2_E2H0_get_TFP(
				     &thread->vcpu_regs_el2.cptr_el2) == 0);
		register_CPTR_EL2_E2H0_write(thread->vcpu_regs_el2.cptr_el2);
#endif

#if defined(VERBOSE) && VERBOSE
#if defined(ARCH_ARM_FEAT_VHE)
		assert_debug(HCR_EL2_get_E2H(&thread->vcpu_regs_el2.hcr_el2));
		assert_debug(!HCR_EL2_get_TGE(&thread->vcpu_regs_el2.hcr_el2));
#endif
		assert_debug(HCR_EL2_get_VM(&thread->vcpu_regs_el2.hcr_el2));
#endif
		register_HCR_EL2_write(thread->vcpu_regs_el2.hcr_el2);

		register_MDCR_EL2_write(thread->vcpu_regs_el2.mdcr_el2);

		register_VBAR_EL2_write(
			VBAR_EL2_cast(CPULOCAL(vcpu_aarch64_vectors)));

		register_FPCR_write(thread->vcpu_regs_fpr.fpcr);
		register_FPSR_write(thread->vcpu_regs_fpr.fpsr);

#if defined(ARCH_ARM_HAVE_SCXT)
		if (vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
			register_SCXTNUM_EL0_write(
				thread->vcpu_regs_el1.scxtnum_el0);
			register_SCXTNUM_EL1_write(
				thread->vcpu_regs_el1.scxtnum_el1);
		}
#endif
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
#if defined(ARCH_ARM_FEAT_VHE)
		HCR_EL2_set_E2H(&nonvm_hcr, true);
#endif
		HCR_EL2_set_TGE(&nonvm_hcr, true);
		register_HCR_EL2_write(nonvm_hcr);
	}
}

void
vcpu_context_switch_save(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_expected(
		    (thread->kind == THREAD_KIND_VCPU) &&
		    !scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_OFF))) {
		thread->vcpu_regs_el1.cpacr_el1	 = register_CPACR_EL1_read();
		thread->vcpu_regs_el1.csselr_el1 = register_CSSELR_EL1_read();
		thread->vcpu_regs_el1.contextidr_el1 =
			register_CONTEXTIDR_EL1_read();
		thread->vcpu_regs_el1.elr_el1 = register_ELR_EL1_read();
		thread->vcpu_regs_el1.esr_el1 = register_ESR_EL1_read();
		thread->vcpu_regs_el1.far_el1 = register_FAR_EL1_read();
		thread->vcpu_regs_el1.par_el1.base =
			register_PAR_EL1_base_read();
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
#if !defined(CPU_HAS_NO_ACTLR_EL1)
		thread->vcpu_regs_el1.actlr_el1 = register_ACTLR_EL1_read();
#endif
#if !defined(CPU_HAS_NO_AMAIR_EL1)
		thread->vcpu_regs_el1.amair_el1 = register_AMAIR_EL1_read();
#endif
#if !defined(CPU_HAS_NO_AFSR0_EL1)
		thread->vcpu_regs_el1.afsr0_el1 = register_AFSR0_EL1_read();
#endif
#if !defined(CPU_HAS_NO_AFSR1_EL1)
		thread->vcpu_regs_el1.afsr1_el1 = register_AFSR1_EL1_read();
#endif

		// Read back HCR_EL2 as VSE may have been cleared.
		thread->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		thread->vcpu_regs_fpr.fpcr    = register_FPCR_read();
		thread->vcpu_regs_fpr.fpsr    = register_FPSR_read();

#if defined(ARCH_ARM_HAVE_SCXT)
		if (vcpu_runtime_flags_get_scxt_allowed(&thread->vcpu_flags)) {
			thread->vcpu_regs_el1.scxtnum_el0 =
				register_SCXTNUM_EL0_read();
			thread->vcpu_regs_el1.scxtnum_el1 =
				register_SCXTNUM_EL1_read();
		}
#endif
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

#if SCHEDULER_CAN_MIGRATE
		if (!vcpu_option_flags_get_pinned(&thread->vcpu_options)) {
			// We need a DSB to ensure that any cache or TLB op
			// executed by the VCPU in EL1 is complete before the
			// VCPU potentially migrates. Otherwise the VCPU may
			// execute its own DSB on the wrong CPU, and proceed
			// before the maintenance operation completes.
			__asm__ volatile("dsb ish" ::"m"(asm_ordering));
		}
#endif
	}
}
