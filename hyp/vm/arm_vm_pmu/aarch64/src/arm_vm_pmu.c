// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <atomic.h>
#include <cpulocal.h>
#include <irq.h>
#include <panic.h>
#include <vcpu.h>
#include <virq.h>

#include <asm/barrier.h>
#include <asm/sysregs.h>
#include <asm/system_registers.h>

#include "arm_vm_pmu.h"
#include "arm_vm_pmu_event_regs.h"
#include "event_handlers.h"
#include "platform_pmu.h"

// For advance PMU support:
// - According to the PSCI specification bits 2 and 3 of DBGCLAIM control
// whether PMU is used by the debuggers. We need to investigate how this affects
// the context switching of the PMU registers. Currently it looks like Linux
// does not actually comply with this standard anyway, except for writing the
// debugger claim bits in the statistical profiling driver.

void
arm_vm_pmu_handle_thread_save_state(void)
{
	thread_t *thread = thread_get_self();

	if ((thread->kind == THREAD_KIND_VCPU) && thread->pmu.is_active) {
		sysreg64_read_ordered(PMINTENSET_EL1,
				      thread->pmu.pmu_regs.pmintenset_el1,
				      asm_ordering);
		sysreg64_write_ordered(PMINTENCLR_EL1,
				       ~thread->pmu.pmu_regs.pmintenset_el1,
				       asm_ordering);

		sysreg64_read_ordered(PMCNTENSET_EL0,
				      thread->pmu.pmu_regs.pmcntenset_el0,
				      asm_ordering);

		sysreg64_read_ordered(PMCR_EL0, thread->pmu.pmu_regs.pmcr_el0,
				      asm_ordering);
		sysreg64_read_ordered(PMCCNTR_EL0,
				      thread->pmu.pmu_regs.pmccntr_el0,
				      asm_ordering);
		sysreg64_read_ordered(PMSELR_EL0,
				      thread->pmu.pmu_regs.pmselr_el0,
				      asm_ordering);
		sysreg64_read_ordered(PMUSERENR_EL0,
				      thread->pmu.pmu_regs.pmuserenr_el0,
				      asm_ordering);
		sysreg64_read_ordered(PMCCFILTR_EL0,
				      thread->pmu.pmu_regs.pmccfiltr_el0,
				      asm_ordering);

		arm_vm_pmu_save_counters_state(thread);

		sysreg64_read_ordered(PMOVSSET_EL0,
				      thread->pmu.pmu_regs.pmovsset_el0,
				      asm_ordering);
	}
}

void
arm_vm_pmu_handle_thread_context_switch_post()
{
	thread_t *thread = thread_get_self();

	if (thread->kind == THREAD_KIND_VCPU) {
		bool_result_t asserted = virq_query(&thread->pmu.pmu_virq_src);
		if ((asserted.e == OK) && !asserted.r) {
			platform_pmu_hw_irq_deactivate();
		}
	}
}

void
arm_vm_pmu_handle_thread_load_state(void)
{
	thread_t *thread = thread_get_self();

	if ((thread->kind == THREAD_KIND_VCPU) && thread->pmu.is_active) {
#if (ARCH_ARM_VER < 81)
		// Event counting cannot be prohibited at EL2. We need to
		// disable the counters, ISB, restore the counters, ISB, then
		// enable the counters. Since it is unlikely that we will need
		// to support such old cores, just throw an error for now.
#error PMU context switch while EL2 counting is enabled is not implemented
#endif
		arm_vm_pmu_load_counters_state(thread);

		sysreg64_write_ordered(PMINTENCLR_EL1,
				       ~thread->pmu.pmu_regs.pmintenset_el1,
				       asm_ordering);
		sysreg64_write_ordered(PMINTENSET_EL1,
				       thread->pmu.pmu_regs.pmintenset_el1,
				       asm_ordering);

		sysreg64_write_ordered(PMOVSCLR_EL0,
				       ~thread->pmu.pmu_regs.pmovsset_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMOVSSET_EL0,
				       thread->pmu.pmu_regs.pmovsset_el0,
				       asm_ordering);

		sysreg64_write_ordered(PMCR_EL0, thread->pmu.pmu_regs.pmcr_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMCCNTR_EL0,
				       thread->pmu.pmu_regs.pmccntr_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMSELR_EL0,
				       thread->pmu.pmu_regs.pmselr_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMUSERENR_EL0,
				       thread->pmu.pmu_regs.pmuserenr_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMCCFILTR_EL0,
				       thread->pmu.pmu_regs.pmccfiltr_el0,
				       asm_ordering);

		sysreg64_write_ordered(PMCNTENCLR_EL0,
				       ~thread->pmu.pmu_regs.pmcntenset_el0,
				       asm_ordering);
		sysreg64_write_ordered(PMCNTENSET_EL0,
				       thread->pmu.pmu_regs.pmcntenset_el0,
				       asm_ordering);
	} else {
		sysreg64_write_ordered(PMINTENCLR_EL1, ~0U, asm_ordering);
		sysreg64_write_ordered(PMCNTENCLR_EL0, ~0U, asm_ordering);
	}
}

bool
arm_vm_pmu_handle_virq_check_pending(virq_source_t *source)
{
	bool ret = true;

	thread_t *thread = atomic_load_relaxed(&source->vgic_vcpu);
	assert(thread != NULL);

	if (thread == thread_get_self()) {
		ret = platform_pmu_is_hw_irq_pending();

		if (!ret) {
			platform_pmu_hw_irq_deactivate();
		}
	}
	return ret;
}

void
arm_vm_pmu_handle_platform_pmu_counter_overflow(void)
{
	thread_t *thread = thread_get_self();

	(void)virq_assert(&thread->pmu.pmu_virq_src, false);
}

error_t
arm_vm_pmu_aarch64_handle_object_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		// The setting of HPMD should be moved to platform code.

#if (ARCH_ARM_PMU_VER >= 3)
		// Set the correct number of event counters
		PMCR_EL0_t pmcr_el0 = register_PMCR_EL0_read();
		MDCR_EL2_set_HPMN(&thread->vcpu_regs_el2.mdcr_el2,
				  PMCR_EL0_get_N(&pmcr_el0));
		// Don't trap accesses to PMU registers if PMU is active
		MDCR_EL2_set_TPM(&thread->vcpu_regs_el2.mdcr_el2,
				 !thread->pmu.is_active);
		MDCR_EL2_set_TPMCR(&thread->vcpu_regs_el2.mdcr_el2,
				   !thread->pmu.is_active);
#if (ARCH_ARM_VER >= 81)
		// Prohibit event counting at EL2
		MDCR_EL2_set_HPMD(&thread->vcpu_regs_el2.mdcr_el2, true);
#endif
#endif
	}

	return OK;
}

#if defined(ARCH_ARM_PMU_HPMN_UNPREDICTABLE)
// On the majority of the ARM cores if MDCR_EL2.HPMN is set to zero the
// behaviour is CONSTRAINED UNPREDICTABLE. On these cores for the guests that
// don't have PMU access we will need to trap the PMU registers and treat them
// as RAZ/WI.

vcpu_trap_result_t
arm_vm_pmu_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val	  = 0ULL; // Default action is RAZ
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t *	   thread = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_PMCR_EL0:
	case ISS_MRS_MSR_PMCNTENSET_EL0:
	case ISS_MRS_MSR_PMCNTENCLR_EL0:
	case ISS_MRS_MSR_PMOVSCLR_EL0:
	case ISS_MRS_MSR_PMSWINC_EL0:
	case ISS_MRS_MSR_PMSELR_EL0:
	case ISS_MRS_MSR_PMCEID0_EL0:
	case ISS_MRS_MSR_PMCEID1_EL0:
	case ISS_MRS_MSR_PMCCNTR_EL0:
	case ISS_MRS_MSR_PMXEVTYPER_EL0:
	case ISS_MRS_MSR_PMXEVCNTR_EL0:
	case ISS_MRS_MSR_PMUSERENR_EL0:
	case ISS_MRS_MSR_PMINTENSET_EL1:
	case ISS_MRS_MSR_PMINTENCLR_EL1:
	case ISS_MRS_MSR_PMOVSSET_EL0:
	case ISS_MRS_MSR_PMCCFILTR_EL0:
		// RAZ
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	default: {
		uint8_t opc0, opc1, crn, crm;

		opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
		crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((opc0 == 3) && (opc1 == 3) && (crn == 14) && (crm >= 8)) {
			// PMEVCNTR and PMEVTYPER registers, RAZ
			ret = VCPU_TRAP_RESULT_EMULATED;
		}
		break;
	}
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
	}

	return ret;
}

vcpu_trap_result_t
arm_vm_pmu_handle_vcpu_trap_sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_UNHANDLED;

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_set_Rt(&iss, 0);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(iss)) {
	case ISS_MRS_MSR_PMCR_EL0:
	case ISS_MRS_MSR_PMCNTENSET_EL0:
	case ISS_MRS_MSR_PMCNTENCLR_EL0:
	case ISS_MRS_MSR_PMOVSCLR_EL0:
	case ISS_MRS_MSR_PMSWINC_EL0:
	case ISS_MRS_MSR_PMSELR_EL0:
	case ISS_MRS_MSR_PMCEID0_EL0:
	case ISS_MRS_MSR_PMCEID1_EL0:
	case ISS_MRS_MSR_PMCCNTR_EL0:
	case ISS_MRS_MSR_PMXEVTYPER_EL0:
	case ISS_MRS_MSR_PMXEVCNTR_EL0:
	case ISS_MRS_MSR_PMUSERENR_EL0:
	case ISS_MRS_MSR_PMINTENSET_EL1:
	case ISS_MRS_MSR_PMINTENCLR_EL1:
	case ISS_MRS_MSR_PMOVSSET_EL0:
	case ISS_MRS_MSR_PMCCFILTR_EL0:
		// WI
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	default: {
		uint8_t opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		uint8_t opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
		uint8_t crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		uint8_t crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((opc0 == 3) && (opc1 == 3) && (crn == 14) && (crm >= 8)) {
			// PMEVCNTR and PMEVTYPER registers, WI
			ret = VCPU_TRAP_RESULT_EMULATED;
		}
		break;
	}
	}

	return ret;
}
#endif
