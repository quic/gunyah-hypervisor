// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <thread.h>

#include <asm/barrier.h>

#include "debug_bps.h"
#include "event_handlers.h"

static struct asm_ordering_dummy vdebug_asm_order;

void
vdebug_handle_boot_cpu_cold_init(void)
{
	ID_AA64DFR0_EL1_t aa64dfr = register_ID_AA64DFR0_EL1_read();

	// Debug version must be between ARMv8.0 (6) and ARMv8.4 (9)
	assert(ID_AA64DFR0_EL1_get_DebugVer(&aa64dfr) >= 6U);
	assert(ID_AA64DFR0_EL1_get_DebugVer(&aa64dfr) <= 9U);

	// Supported breakpoint and watchpoint counts must be correct
	assert((ID_AA64DFR0_EL1_get_BRPs(&aa64dfr) + 1U) == CPU_DEBUG_BP_COUNT);
	assert((ID_AA64DFR0_EL1_get_WRPs(&aa64dfr) + 1U) == CPU_DEBUG_WP_COUNT);
}

error_t
vdebug_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	if (thread_create.kind == THREAD_KIND_VCPU) {
		// Currently we allow debug access for all VCPUs by default
		thread->vdebug_allowed = true;

		// All VCPUs have debug initially disabled
		thread->vdebug_enabled = false;
	}

	return OK;
}

error_t
vdebug_handle_object_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		// Debug traps should all be enabled by default
		assert(MDCR_EL2_get_TDOSA(&thread->vcpu_regs_el2.mdcr_el2));
		assert(MDCR_EL2_get_TDA(&thread->vcpu_regs_el2.mdcr_el2));
		assert(MDCR_EL2_get_TDE(&thread->vcpu_regs_el2.mdcr_el2));

		// Trap debug exceptions if debug is not allowed on this VCPU
		MDCR_EL2_set_TDE(&thread->vcpu_regs_el2.mdcr_el2,
				 !thread->vdebug_allowed);
	}

	return OK;
}

void
vdebug_handle_thread_save_state(void)
{
	thread_t *current = thread_get_self();

	if (current->vdebug_enabled) {
		current->vdebug_enabled = debug_save_common(
			&current->vdebug_state, &vdebug_asm_order);

		// If debug is no longer in use, ensure register accesses will
		// be trapped when we next switch back to this VCPU, so we can
		// safely avoid restoring the registers.
		if (!current->vdebug_enabled) {
			MDCR_EL2_set_TDA(&current->vcpu_regs_el2.mdcr_el2,
					 true);
		}
	}
}

void
vdebug_handle_thread_context_switch_post(thread_t *prev)
{
	thread_t *current = thread_get_self();

	if (prev->vdebug_enabled && !current->vdebug_enabled) {
		// Write zeros to MDSCR_EL1.MDE and MDSCR_EL1.SS to disable
		// breakpoints and single-stepping, in case the previous VCPU
		// had them enabled.
		register_MDSCR_EL1_write_ordered(MDSCR_EL1_default(),
						 &vdebug_asm_order);
	}
}

void
vdebug_handle_thread_load_state()
{
	thread_t *current = thread_get_self();

	if (current->vdebug_enabled) {
		debug_load_common(&current->vdebug_state, &vdebug_asm_order);
	}
}

vcpu_trap_result_t
vdebug_handle_vcpu_trap_sysreg(ESR_EL2_ISS_MSR_MRS_t iss)
{
	thread_t *	   current = thread_get_self();
	vcpu_trap_result_t ret;

	if (ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) != 2U) {
		// Not a debug register access.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else if (!current->vdebug_allowed) {
		// This VCPU isn't allowed to access debug. Fault immediately.
		ret = VCPU_TRAP_RESULT_FAULT;
	} else if (!current->vdebug_enabled) {
		// Lazily enable debug register access and restore context.
		current->vdebug_enabled = true;
		debug_load_common(&current->vdebug_state, &vdebug_asm_order);

		// Disable the register access trap and retry.
		MDCR_EL2_set_TDA(&current->vcpu_regs_el2.mdcr_el2, false);
		register_MDCR_EL2_write(current->vcpu_regs_el2.mdcr_el2);
		ret = VCPU_TRAP_RESULT_RETRY;
	} else {
		// Probably an attempted OS lock; fall back to default RAZ/WI.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	}

	return ret;
}
