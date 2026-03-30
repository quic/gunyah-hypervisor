// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(ARCH_ARM_FEAT_SPE)
#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_irq.h>
#include <thread.h>
#include <vcpu.h>
#include <vic.h>
#include <virq.h>

#include <asm/barrier.h>
#include <asm/system_registers.h>

#include "event_handlers.h"

// Global SPE HWIRQ handle
static hwirq_t *spe_hwirq;

// Initialize MDCR_EL2.E2PB to EL2 mode and disable EL2 SPE profiling to
// prevent spurious interrupts before VCPUs are activated.

void
arm_vm_spe_handle_boot_cpu_warm_init(void)
{
#if !defined(ARCH_ARM_FEAT_SPE_FnE)
	// Hardware must not have FEAT_SPE_FnE without compile-time support
	PMSIDR_EL1_t pmsidr = register_PMSIDR_EL1_read();
	if (PMSIDR_EL1_get_FnE(&pmsidr)) {
		panic("SPE CPU config invalid");
	}
#endif

	MDCR_EL2_t  mdcr_el2  = register_MDCR_EL2_read();
	PMSCR_EL2_t pmscr_el2 = PMSCR_EL2_default();

	MDCR_EL2_set_E2PB(&mdcr_el2, MDCR_EL2_E2PB_EL2);
	register_MDCR_EL2_write(mdcr_el2);

	PMSCR_EL2_set_E2SPE(&pmscr_el2, false);
	register_PMSCR_EL2_write_ordered(pmscr_el2, &asm_ordering);

#if defined(ARCH_ARM_FEAT_FGT) && defined(ARCH_ARM_FEAT_SPE_FnE)
	// Set nPMSNEVFR_EL1 once at boot time. PMSNEVFR_EL1 is context-switched
	// and trapped by MDCR_EL2.TPMS; these bits prevent spurious FGT traps.
	HDFGWTR_EL2_t hdfgwtr = register_HDFGWTR_EL2_read();
	HDFGWTR_EL2_set_nPMSNEVFR_EL1(&hdfgwtr, true);
	register_HDFGWTR_EL2_write(hdfgwtr);

	HDFGRTR_EL2_t hdfgrtr = register_HDFGRTR_EL2_read();
	HDFGRTR_EL2_set_nPMSNEVFR_EL1(&hdfgrtr, true);
	register_HDFGRTR_EL2_write(hdfgrtr);
#endif

	if (spe_hwirq != NULL) {
		irq_enable_local(spe_hwirq);
	}
}

// Create and activate the SPE hardware interrupt (PMBIRQ).

void
arm_vm_spe_handle_boot_hypervisor_start(void)
{
	hwirq_create_t params = {
		.irq	= PLATFORM_STATISTICAL_PROFILING_IRQ,
		.action = HWIRQ_ACTION_SPE_BUFFER,
	};

	hwirq_ptr_result_t ret =
		partition_allocate_hwirq(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Failed to create SPE buffer IRQ");
	}

	if (object_activate_hwirq(ret.r) != OK) {
		panic("Failed to activate SPE buffer IRQ");
	}

	spe_hwirq = ret.r;

	irq_enable_local(spe_hwirq);
}

// Set default SPE interrupt number from platform configuration (typically
// IRQ 21). Can be overridden via hypercall_vcpu_set_local_virq().

error_t
arm_vm_spe_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert_debug(thread != NULL);

	if (vcpu_is_vcpu(thread)) {
		thread->spe.spe_virq = PLATFORM_STATISTICAL_PROFILING_IRQ;
	}

	return OK;
}

// SPE uses semi-lazy context switching:
// - Traps enabled by default (MDCR_EL2.TPMS)
// - On first access: load state, disable traps while SPE is enabled
// - Traps remain disabled as long as SPE profiling is active (E0SPE or E1SPE)
// - On context switch out: save state if traps were disabled
// - On context switch in: check if actively profiling, load if needed
// - When guest disables SPE: re-enable traps to avoid unnecessary saves
//
// This ensures complete profiling statistics are collected for the VCPU
// while it has SPE enabled, not just for a single timeslice.
//
// Interrupt Handling: The PMBIRQ (Profiling Buffer Interrupt Request) is
// forwarded to the guest as a virtual interrupt via virq_source.

static bool
arm_vm_spe_is_profiling_enabled(thread_t *thread)
{
	return (PMSCR_EL1_get_E0SPE(&thread->spe.spe_regs.pmscr_el1) ||
		PMSCR_EL1_get_E1SPE(&thread->spe.spe_regs.pmscr_el1));
}

static bool
arm_vm_spe_is_el1_trap_enabled(thread_t *thread)
{
	return MDCR_EL2_get_TPMS(&thread->vcpu_regs_el2.mdcr_el2);
}

// IMPORTANT: Keep TPMS and E2PB synchronized. When TPMS=1, E2PB must be
// EL1_TRAP to trap buffer registers. When TPMS=0, E2PB must be EL1_NO_TRAP.
static void
arm_vm_spe_el1_trap_set_enable(thread_t *thread, bool enable)
{
	MDCR_EL2_set_TPMS(&thread->vcpu_regs_el2.mdcr_el2, enable);

	if (enable) {
		MDCR_EL2_set_E2PB(&thread->vcpu_regs_el2.mdcr_el2,
				  MDCR_EL2_E2PB_EL1_TRAP);
	} else {
		MDCR_EL2_set_E2PB(&thread->vcpu_regs_el2.mdcr_el2,
				  MDCR_EL2_E2PB_EL1_NO_TRAP);
	}

	register_MDCR_EL2_write(thread->vcpu_regs_el2.mdcr_el2);
}

static void
arm_vm_spe_save_state(thread_t *thread)
{
	thread->spe.spe_regs.pmscr_el1 =
		register_PMSCR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmsicr_el1 =
		register_PMSICR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmsirr_el1 =
		register_PMSIRR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmsfcr_el1 =
		register_PMSFCR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmsevfr_el1 =
		register_PMSEVFR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmslatfr_el1 =
		register_PMSLATFR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmblimitr_el1 =
		register_PMBLIMITR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmbptr_el1 =
		register_PMBPTR_EL1_read_ordered(&asm_ordering);
	thread->spe.spe_regs.pmbsr_el1 =
		register_PMBSR_EL1_read_ordered(&asm_ordering);
#if defined(ARCH_ARM_FEAT_SPEv1p2)
	thread->spe.spe_regs.pmsnevfr_el1 =
		register_PMSNEVFR_EL1_read_ordered(&asm_ordering);
#endif
}

static void
arm_vm_spe_load_state(thread_t *thread)
{
	register_PMSCR_EL1_write_ordered(thread->spe.spe_regs.pmscr_el1,
					 &asm_ordering);
	register_PMSICR_EL1_write_ordered(thread->spe.spe_regs.pmsicr_el1,
					  &asm_ordering);
	register_PMSIRR_EL1_write_ordered(thread->spe.spe_regs.pmsirr_el1,
					  &asm_ordering);
	register_PMSFCR_EL1_write_ordered(thread->spe.spe_regs.pmsfcr_el1,
					  &asm_ordering);
	register_PMSEVFR_EL1_write_ordered(thread->spe.spe_regs.pmsevfr_el1,
					   &asm_ordering);
	register_PMSLATFR_EL1_write_ordered(thread->spe.spe_regs.pmslatfr_el1,
					    &asm_ordering);
	register_PMBLIMITR_EL1_write_ordered(thread->spe.spe_regs.pmblimitr_el1,
					     &asm_ordering);
	register_PMBPTR_EL1_write_ordered(thread->spe.spe_regs.pmbptr_el1,
					  &asm_ordering);
	register_PMBSR_EL1_write_ordered(thread->spe.spe_regs.pmbsr_el1,
					 &asm_ordering);
#if defined(ARCH_ARM_FEAT_SPEv1p2)
	register_PMSNEVFR_EL1_write_ordered(thread->spe.spe_regs.pmsnevfr_el1,
					    &asm_ordering);
#endif
}

bool
arm_vm_spe_handle_vcpu_activate_thread(thread_t *thread)
{
	error_t ret;

	assert_debug(vcpu_is_vcpu(thread));

	ret = vic_bind_private_vcpu(&thread->spe.spe_virq_src, thread,
				    thread->spe.spe_virq,
				    VIRQ_TRIGGER_SPE_BUFFER);

	return ret == OK;
}

void
arm_vm_spe_handle_object_deactivate_thread(thread_t *thread)
{
	if (vcpu_is_vcpu(thread)) {
		vic_unbind(&thread->spe.spe_virq_src);
	}
}

error_t
arm_vm_spe_handle_vcpu_set_local_virq(thread_t *vcpu, virq_t virq)
{
	vcpu->spe.spe_virq = virq;
	return OK;
}

// IMPORTANT: Set E2PB to EL1_TRAP when TPMS=1 to ensure buffer register
// writes also trap, preventing state loss.
error_t
arm_vm_spe_handle_object_activate_thread(thread_t *thread)
{
	MDCR_EL2_set_TPMS(&thread->vcpu_regs_el2.mdcr_el2, true);
	MDCR_EL2_set_E2PB(&thread->vcpu_regs_el2.mdcr_el2,
			  MDCR_EL2_E2PB_EL1_TRAP);

	return OK;
}

void
arm_vm_spe_handle_vcpu_save_state(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_unexpected(!arm_vm_spe_is_el1_trap_enabled(thread))) {
		arm_vm_spe_save_state(thread);

		if (!arm_vm_spe_is_profiling_enabled(thread)) {
			arm_vm_spe_el1_trap_set_enable(thread, true);
		}
	}
}

void
arm_vm_spe_handle_vcpu_load_state(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_unexpected(arm_vm_spe_is_profiling_enabled(thread))) {
		arm_vm_spe_load_state(thread);
	} else {
		register_PMSCR_EL1_write_ordered(PMSCR_EL1_default(),
						 &asm_ordering);
	}
}

void
arm_vm_spe_handle_thread_context_switch_post(void)
{
	thread_t *thread = thread_get_self();

	if (compiler_expected(vcpu_is_vcpu(thread))) {
		bool_result_t asserted = virq_query(&thread->spe.spe_virq_src);
		if ((asserted.e == OK) && !asserted.r) {
			platform_irq_deactivate_percpu(
				PLATFORM_STATISTICAL_PROFILING_IRQ,
				cpulocal_get_index());
		}

		// IMPORTANT: Keep TPMS and E2PB synchronized
		if (arm_vm_spe_is_profiling_enabled(thread)) {
			MDCR_EL2_set_TPMS(&thread->vcpu_regs_el2.mdcr_el2,
					  false);
			MDCR_EL2_set_E2PB(&thread->vcpu_regs_el2.mdcr_el2,
					  MDCR_EL2_E2PB_EL1_NO_TRAP);
		} else {
			MDCR_EL2_set_TPMS(&thread->vcpu_regs_el2.mdcr_el2,
					  true);
			MDCR_EL2_set_E2PB(&thread->vcpu_regs_el2.mdcr_el2,
					  MDCR_EL2_E2PB_EL1_TRAP);
		}
	}
}

void
arm_vm_spe_handle_vcpu_disable_state(void)
{
	register_PMSCR_EL1_write_ordered(PMSCR_EL1_default(), &asm_ordering);
}

// Forward PMBIRQ to guest as virtual interrupt. The hypervisor doesn't
// process the buffer - the guest is responsible for servicing it.

bool
arm_vm_spe_handle_irq_received(const hwirq_t *hwirq)
{
	bool ret = false;

	if (hwirq->irq == PLATFORM_STATISTICAL_PROFILING_IRQ) {
		thread_t *thread = thread_get_self();

		if (vcpu_is_vcpu(thread)) {
			(void)virq_assert(&thread->spe.spe_virq_src, false);
			ret = true;
		}
	}

	return ret;
}

// Trap handler for semi-lazy context switching. On first SPE register access,
// load VCPU state and disable traps while profiling is active. Handles all
// SPE registers identically: load state and retry instruction.

vcpu_trap_result_t
arm_vm_spe_handle_vcpu_trap_sysreg_access(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	thread_t	  *thread = thread_get_self();

	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_PMSCR_EL1:
	case ISS_MRS_MSR_PMSICR_EL1:
	case ISS_MRS_MSR_PMSIDR_EL1:
	case ISS_MRS_MSR_PMSIRR_EL1:
	case ISS_MRS_MSR_PMSFCR_EL1:
	case ISS_MRS_MSR_PMSEVFR_EL1:
	case ISS_MRS_MSR_PMSLATFR_EL1:
	case ISS_MRS_MSR_PMBLIMITR_EL1:
	case ISS_MRS_MSR_PMBPTR_EL1:
	case ISS_MRS_MSR_PMBSR_EL1:
	case ISS_MRS_MSR_PMBIDR_EL1:
#if defined(ARCH_ARM_FEAT_SPEv1p2)
	case ISS_MRS_MSR_PMSNEVFR_EL1:
#endif
		ret = VCPU_TRAP_RESULT_RETRY;
		break;
	default:
		break;
	}

	if (ret == VCPU_TRAP_RESULT_RETRY) {
		arm_vm_spe_el1_trap_set_enable(thread, false);
		arm_vm_spe_load_state(thread);
	}

	return ret;
}
#else
// Dummy declaration to avoid empty translation unit warning
typedef int arm_vm_spe_dummy_t;
#endif
