// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>
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

// Design: "semi-lazy" context-switching. The aim is to context switch the PMU
// registers only when the VM is actively using PMU (a PMU register is accessed
// or at least one counter is enabled). This way if Linux accesses PMU only at
// boot time and never again, we won't be context-switching its PMU registers
// for the rest of its lifetime.
// All threads will initially have access to PMU registers disabled (MDCR_EL2).
// When a PMU access is trapped, the thread will be given access to PMU for the
// time-slice. When this thread is context-switched out, its PMU registers will
// be saved.
// When switching to a thread, the thread's state of PMU counters is checked to
// see if the thread is actively using PMU. If yes, the thread will be given PMU
// access for the time-slice and its PMU context is loaded. If no, the PMU
// access traps are enabled until the next access happens, as explained above.

// Debugger considerations:
// According to the PSCI specification bit 2 of DBGCLAIM says whether PMU is
// being used by the external debuggers. We need to investigate how this affects
// the context switching of the PMU registers. Currently it looks like Linux
// does not actually comply with this standard anyway, except for writing the
// debugger claim bits in the statistical profiling driver.

#if (ARCH_ARM_PMU_VER < 3)
#error Only PMUv3 and above can be implemented in ARMv8/ARMv9.
#endif

static bool
arm_vm_pmu_counters_enabled(thread_t *current)
{
	// Check if the global enable flag is set and at least one counter is
	// enabled.
	return (PMCR_EL0_get_E(&current->pmu.pmu_regs.pmcr_el0) &&
		(current->pmu.pmu_regs.pmcntenset_el0 != 0));
}

static bool
arm_vm_pmu_is_el1_trap_enabled(thread_t *current)
{
	MDCR_EL2_t mdcr = current->vcpu_regs_el2.mdcr_el2;
	return (MDCR_EL2_get_TPM(&mdcr) || MDCR_EL2_get_TPMCR(&mdcr));
}

static void
arm_vm_pmu_el1_trap_set_enable(thread_t *current, bool enable)
{
	MDCR_EL2_set_TPM(&current->vcpu_regs_el2.mdcr_el2, enable);
	MDCR_EL2_set_TPMCR(&current->vcpu_regs_el2.mdcr_el2, enable);
	register_MDCR_EL2_write(current->vcpu_regs_el2.mdcr_el2);
}

error_t
arm_vm_pmu_aarch64_handle_object_activate_thread(thread_t *thread)
{
	// Set the correct number of event counters
	PMCR_EL0_t pmcr_el0 = register_PMCR_EL0_read();
	MDCR_EL2_set_HPMN(&thread->vcpu_regs_el2.mdcr_el2,
			  PMCR_EL0_get_N(&pmcr_el0));
#if defined(ARCH_ARM_8_1_PMU)
	// Prohibit event counting at EL2
	MDCR_EL2_set_HPMD(&thread->vcpu_regs_el2.mdcr_el2, true);
#endif

	// Enable PMU access traps
	MDCR_EL2_set_TPM(&thread->vcpu_regs_el2.mdcr_el2, true);
	MDCR_EL2_set_TPMCR(&thread->vcpu_regs_el2.mdcr_el2, true);

	return OK;
}

static void
arm_vm_pmu_save_state(thread_t *thread)
{
	sysreg64_read_ordered(PMINTENSET_EL1,
			      thread->pmu.pmu_regs.pmintenset_el1,
			      asm_ordering);
	sysreg64_read_ordered(PMCNTENSET_EL0,
			      thread->pmu.pmu_regs.pmcntenset_el0,
			      asm_ordering);

	thread->pmu.pmu_regs.pmcr_el0 =
		register_PMCR_EL0_read_ordered(&asm_ordering);
	sysreg64_read_ordered(PMCCNTR_EL0, thread->pmu.pmu_regs.pmccntr_el0,
			      asm_ordering);
	sysreg64_read_ordered(PMSELR_EL0, thread->pmu.pmu_regs.pmselr_el0,
			      asm_ordering);
	sysreg64_read_ordered(PMUSERENR_EL0, thread->pmu.pmu_regs.pmuserenr_el0,
			      asm_ordering);
	sysreg64_read_ordered(PMCCFILTR_EL0, thread->pmu.pmu_regs.pmccfiltr_el0,
			      asm_ordering);

	arm_vm_pmu_save_counters_state(thread);

	sysreg64_read_ordered(PMOVSSET_EL0, thread->pmu.pmu_regs.pmovsset_el0,
			      asm_ordering);

#if !defined(ARCH_ARM_8_1_PMU)
	// Event counting cannot be prohibited at EL2. Do an ISB to make sure
	// the operation above completes before we continue. This to ensure that
	// the register reads above are not delayed until after some sensitive
	// operation.
	asm_context_sync_ordered(&asm_ordering);
#endif
}

static void
arm_vm_pmu_load_state(thread_t *thread)
{
	arm_vm_pmu_load_counters_state(thread);

	sysreg64_write_ordered(PMINTENCLR_EL1,
			       ~thread->pmu.pmu_regs.pmintenset_el1,
			       asm_ordering);
	sysreg64_write_ordered(PMINTENSET_EL1,
			       thread->pmu.pmu_regs.pmintenset_el1,
			       asm_ordering);

	sysreg64_write_ordered(PMOVSCLR_EL0, ~thread->pmu.pmu_regs.pmovsset_el0,
			       asm_ordering);
	sysreg64_write_ordered(PMOVSSET_EL0, thread->pmu.pmu_regs.pmovsset_el0,
			       asm_ordering);

	register_PMCR_EL0_write_ordered(thread->pmu.pmu_regs.pmcr_el0,
					&asm_ordering);
	sysreg64_write_ordered(PMCCNTR_EL0, thread->pmu.pmu_regs.pmccntr_el0,
			       asm_ordering);
	sysreg64_write_ordered(PMSELR_EL0, thread->pmu.pmu_regs.pmselr_el0,
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
}

void
arm_vm_pmu_handle_thread_save_state(void)
{
	thread_t *thread = thread_get_self();

	if ((thread->kind == THREAD_KIND_VCPU) &&
	    (!arm_vm_pmu_is_el1_trap_enabled(thread))) {
		// PMU access was enabled for this timeslice, save the state
		arm_vm_pmu_save_state(thread);
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

		// If the thread is actively using PMU, grant it access
		if (arm_vm_pmu_counters_enabled(thread)) {
			MDCR_EL2_set_TPM(&thread->vcpu_regs_el2.mdcr_el2,
					 false);
			MDCR_EL2_set_TPMCR(&thread->vcpu_regs_el2.mdcr_el2,
					   false);
		} else {
			MDCR_EL2_set_TPM(&thread->vcpu_regs_el2.mdcr_el2, true);
			MDCR_EL2_set_TPMCR(&thread->vcpu_regs_el2.mdcr_el2,
					   true);
		}
	}
}

void
arm_vm_pmu_handle_thread_load_state(void)
{
	thread_t *thread = thread_get_self();

	if ((thread->kind == THREAD_KIND_VCPU) &&
	    arm_vm_pmu_counters_enabled(thread)) {
		// The thread is actively using PMU. The context_switch_post
		// has already disabled traps for this thread above, and it will
		// get loaded into MDCR_EL2 during the context switch load
		// process in the generic module. Load its PMU context here.
		arm_vm_pmu_load_state(thread);
	} else if (thread->kind == THREAD_KIND_VCPU) {
		// The thread is not actively using PMU. The context_switch_post
		// has already enabled traps for this thread above, and it will
		// get loaded into MDCR_EL2 during the context switch load
		// process in the generic module. If it tries to access PMU,
		// in the trap handler we load its context and give it access.
		//
		// No need to sanitise the PMU registers (even though they might
		// have the values from the old thread), because this thread
		// doesn't currently have access to thems.
		//
		// Turn off the counters and the interrupts.
		sysreg64_write_ordered(PMINTENCLR_EL1, ~0U, asm_ordering);
		sysreg64_write_ordered(PMCNTENCLR_EL0, ~0U, asm_ordering);
	} else {
		// Idle thread. Turn off the counters and the interrupts.
		sysreg64_write_ordered(PMINTENCLR_EL1, ~0U, asm_ordering);
		sysreg64_write_ordered(PMCNTENCLR_EL0, ~0U, asm_ordering);
	}
}

bool
arm_vm_pmu_handle_virq_check_pending(virq_source_t *source)
{
	bool ret = true;

	pmu_t    *pmu	 = pmu_container_of_pmu_virq_src(source);
	thread_t *thread = thread_container_of_pmu(pmu);
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

vcpu_trap_result_t
arm_vm_pmu_handle_vcpu_trap_sysreg_access(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t		 *thread = thread_get_self();

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
		ret = VCPU_TRAP_RESULT_RETRY;
		break;
	default: {
		uint8_t opc0, opc1, crn, crm;

		opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
		crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((opc0 == 3) && (opc1 == 3) && (crn == 14) && (crm >= 8)) {
			// PMEVCNTR and PMEVTYPER registers
			ret = VCPU_TRAP_RESULT_RETRY;
		}
		break;
	}
	}

	if (ret == VCPU_TRAP_RESULT_RETRY) {
		// The thread is trying to access PMU. Allow access for this
		// time-slice by disabling the PMU traps.
		arm_vm_pmu_el1_trap_set_enable(thread, false);
		// If The thread has already accessed PMU in the past, load its
		// PMU state. Otherwise the load below acts as a sanitiser.
		arm_vm_pmu_load_state(thread);
	}

	return ret;
}
