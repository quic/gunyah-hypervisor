// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <vet.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "tbre.h"

void
vtbre_handle_boot_cpu_cold_init(void)
{
	ID_AA64DFR0_EL1_t id_aa64dfr0 = register_ID_AA64DFR0_EL1_read();
	// NOTE: ID_AA64DFR0.TraceBuffer just indicates if trace buffer is
	// implemented, so here we use equal for assertion.
	assert(ID_AA64DFR0_EL1_get_TraceBuffer(&id_aa64dfr0) == 1U);
}

error_t
vtbre_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;

	// MDCR_EL2.E2TB == 0b10 to prohibit trace EL2
	MDCR_EL2_set_E2TB(&thread->vcpu_regs_el2.mdcr_el2, 0x2);

	return OK;
}

void
vet_update_trace_buffer_status(thread_t *self)
{
	assert(self != NULL);

#if !DISABLE_TBRE
	// check/set by reading TBRLIMITR.EN == 1
	TRBLIMITR_EL1_t trb_limitr =
		register_TRBLIMITR_EL1_read_ordered(&vet_ordering);
	self->vet_trace_buffer_enabled = TRBLIMITR_EL1_get_E(&trb_limitr);
#endif
}

void
vet_flush_buffer(thread_t *self)
{
	assert(self != NULL);

	if (compiler_unexpected(self->vet_trace_buffer_enabled)) {
		__asm__ volatile("tsb csync" : "+m"(vet_ordering));
	}
}

void
vet_disable_buffer(void)
{
	TRBLIMITR_EL1_t trb_limitr =
		register_TRBLIMITR_EL1_read_ordered(&vet_ordering);
	TRBLIMITR_EL1_set_E(&trb_limitr, false);
	register_TRBLIMITR_EL1_write_ordered(trb_limitr, &vet_ordering);
}

static void
vtbre_prohibit_registers_access(thread_t *self, bool prohibit)
{
	assert(self != NULL);

	// MDCR_EL2.E2TB == 0b11 to enable access to TBRE
	// MDCR_EL2.E2TB == 0b10 to disable access to TBRE
	uint8_t expect = prohibit ? 0x2U : 0x3U;

	MDCR_EL2_set_E2TB(&self->vcpu_regs_el2.mdcr_el2, expect);
	register_MDCR_EL2_write_ordered(self->vcpu_regs_el2.mdcr_el2,
					&vet_ordering);
}

void
vet_save_buffer_thread_context(thread_t *self)
{
	(void)self;
	vtbre_prohibit_registers_access(self, true);
}

void
vet_restore_buffer_thread_context(thread_t *self)
{
	vtbre_prohibit_registers_access(self, false);
}

void
vet_enable_buffer(void)
{
	TRBLIMITR_EL1_t trb_limitr =
		register_TRBLIMITR_EL1_read_ordered(&vet_ordering);
	TRBLIMITR_EL1_set_E(&trb_limitr, true);
	register_TRBLIMITR_EL1_write_ordered(trb_limitr, &vet_ordering);
}

void
vet_save_buffer_power_context(void)
{
	MDCR_EL2_t mdcr_el2;

	// Enable E2TB access
	mdcr_el2 = register_MDCR_EL2_read_ordered(&vet_ordering);
	MDCR_EL2_set_E2TB(&mdcr_el2, 3);
	register_MDCR_EL2_write_ordered(mdcr_el2, &vet_ordering);

	asm_context_sync_ordered(&vet_ordering);

	tbre_save_context_percpu(cpulocal_get_index());

	// Disable E2TB access
	MDCR_EL2_set_E2TB(&mdcr_el2, 2);
	register_MDCR_EL2_write_ordered(mdcr_el2, &vet_ordering);
}

void
vet_restore_buffer_power_context(void)
{
	MDCR_EL2_t mdcr_el2;

	// Enable E2TB access
	mdcr_el2 = register_MDCR_EL2_read_ordered(&vet_ordering);
	MDCR_EL2_set_E2TB(&mdcr_el2, 3);
	register_MDCR_EL2_write_ordered(mdcr_el2, &vet_ordering);

	asm_context_sync_ordered(&vet_ordering);

	tbre_restore_context_percpu(cpulocal_get_index());

	// Disable E2TB access
	MDCR_EL2_set_E2TB(&mdcr_el2, 2);
	register_MDCR_EL2_write_ordered(mdcr_el2, &vet_ordering);
}

vcpu_trap_result_t
vtbre_handle_vcpu_trap_sysreg(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret;

#if DISABLE_TBRE
	(void)iss;

	ret = VCPU_TRAP_RESULT_UNHANDLED;
#else
	thread_t *current = thread_get_self();

	if (compiler_expected((ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) != 3U) ||
			      (ESR_EL2_ISS_MSR_MRS_get_Op1(&iss) != 0U) ||
			      (ESR_EL2_ISS_MSR_MRS_get_CRn(&iss) != 9U) ||
			      (ESR_EL2_ISS_MSR_MRS_get_CRm(&iss) != 11U))) {
		// Not a TBRE register access.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if (!vcpu_option_flags_get_trace_allowed(
			   &thread->vcpu_options)) {
		// This VCPU isn't allowed to trace. Fault immediately.
		ret = VCPU_TRAP_RESULT_FAULT;
	} else if (!current->vet_trace_buffer_enabled) {
		// Lazily enable trace buffer register access and restore
		// context.
		current->vet_trace_buffer_enabled = true;

		// only enable the register access
		vtbre_prohibit_registers_access(false);

		ret = VCPU_TRAP_RESULT_RETRY;
	} else {
		// Probably an attempted OS lock; fall back to default RAZ/WI.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}
#endif

	return ret;
}
