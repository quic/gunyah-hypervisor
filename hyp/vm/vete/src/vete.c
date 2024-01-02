// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <vet.h>

#include <asm/barrier.h>
#include <asm/system_registers.h>

#include "ete.h"
#include "event_handlers.h"

#define ISS_TRFCR_EL1 ISS_OP0_OP1_CRN_CRM_OP2(3, 0, 1, 2, 1)

void
vete_handle_boot_cpu_cold_init(void)
{
	ID_AA64DFR0_EL1_t id_aa64dfr0 = register_ID_AA64DFR0_EL1_read();
	// NOTE: ID_AA64DFR0.TraceVer just indicates if trace is implemented,
	// so here we use equal for assertion.
	assert(ID_AA64DFR0_EL1_get_TraceVer(&id_aa64dfr0) == 1U);
}

void
vete_handle_boot_cpu_warm_init(void)
{
	TRFCR_EL2_t trfcr = TRFCR_EL2_default();
	// prohibit trace of EL2
	TRFCR_EL2_set_E2TRE(&trfcr, 0);
	register_TRFCR_EL2_write_ordered(trfcr, &vet_ordering);
}

void
vet_update_trace_unit_status(thread_t *self)
{
	assert(self != NULL);

	TRCPRGCTLR_t trcprgctlr = TRCPRGCTLR_cast(
		register_TRCPRGCTLR_read_ordered(&vet_ordering));
	self->vet_trace_unit_enabled = TRCPRGCTLR_get_EN(&trcprgctlr);
}

void
vet_flush_trace(thread_t *self)
{
	assert(self != NULL);

	if (compiler_unexpected(self->vet_trace_unit_enabled)) {
		__asm__ volatile("tsb csync" : "+m"(vet_ordering));
	}
}

void
vet_disable_trace(void)
{
	TRCPRGCTLR_t trcprg_ctlr = TRCPRGCTLR_default();
	TRCPRGCTLR_set_EN(&trcprg_ctlr, false);
	register_TRCPRGCTLR_write_ordered(TRCPRGCTLR_raw(trcprg_ctlr),
					  &vet_ordering);
}

static void
vete_prohibit_registers_access(bool prohibit)
{
	thread_t *current = thread_get_self();

	MDCR_EL2_set_TTRF(&current->vcpu_regs_el2.mdcr_el2, prohibit);
	register_MDCR_EL2_write_ordered(current->vcpu_regs_el2.mdcr_el2,
					&vet_ordering);
}

void
vet_save_trace_thread_context(thread_t *self)
{
	(void)self;
	// disable trace register access by set CPTR_EL2.TTA=1
	vete_prohibit_registers_access(true);
}

void
vet_restore_trace_thread_context(thread_t *self)
{
	(void)self;
	// enable trace register access by clear CPTR_EL2.TAA=0
	vete_prohibit_registers_access(false);
}

void
vet_enable_trace(void)
{
	TRCPRGCTLR_t trcprg_ctlr = TRCPRGCTLR_default();
	TRCPRGCTLR_set_EN(&trcprg_ctlr, true);
	register_TRCPRGCTLR_write_ordered(TRCPRGCTLR_raw(trcprg_ctlr),
					  &vet_ordering);
}

void
vet_restore_trace_power_context(bool was_poweroff)
{
	// enable trace register access by clear CPTR_EL2.TAA=0
	vete_prohibit_registers_access(false);
	asm_context_sync_ordered(&vet_ordering);

	ete_restore_context_percpu(cpulocal_get_index(), was_poweroff);

	// disable trace register access by clear CPTR_EL2.TAA=1
	vete_prohibit_registers_access(true);
}

void
vet_save_trace_power_context(bool was_poweroff)
{
	vete_prohibit_registers_access(false);
	asm_context_sync_ordered(&vet_ordering);

	ete_save_context_percpu(cpulocal_get_index(), was_poweroff);

	// disable trace register access by clear CPTR_EL2.TAA=1
	vete_prohibit_registers_access(true);
}

vcpu_trap_result_t
vete_handle_vcpu_trap_sysreg(ESR_EL2_ISS_MSR_MRS_t iss)
{
	thread_t	  *current = thread_get_self();
	vcpu_trap_result_t ret;

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_set_Rt(&iss, 0);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&iss, false);

	if (((ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) != 2U) ||
	     (ESR_EL2_ISS_MSR_MRS_get_Op1(&iss) != 1U)) &&
	    (ESR_EL2_ISS_MSR_MRS_raw(iss) != ISS_TRFCR_EL1)) {
		// Not a TRBE register access.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if (!vcpu_option_flags_get_trace_allowed(
			   &current->vcpu_options)) {
		// This VCPU isn't allowed to access debug. Fault immediately.
		ret = VCPU_TRAP_RESULT_FAULT;
	} else if (!current->vet_trace_unit_enabled) {
		// Lazily enable trace register access and restore context.
		current->vet_trace_unit_enabled = true;

		// only enable the register access
		vete_prohibit_registers_access(false);

		ret = VCPU_TRAP_RESULT_RETRY;
	} else {
		// Probably an attempted OS lock; fall back to default RAZ/WI.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	return ret;
}
