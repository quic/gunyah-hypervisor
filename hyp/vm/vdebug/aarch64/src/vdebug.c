// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <log.h>
#include <platform_security.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>

#include <asm/barrier.h>

#include "debug_bps.h"
#include "event_handlers.h"

static asm_ordering_dummy_t vdebug_asm_order;

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

bool
vdebug_handle_vcpu_activate_thread(thread_t	      *thread,
				   vcpu_option_flags_t options)
{
	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	// Debug traps should all be enabled by default
	assert(MDCR_EL2_get_TDOSA(&thread->vcpu_regs_el2.mdcr_el2));
	assert(MDCR_EL2_get_TDA(&thread->vcpu_regs_el2.mdcr_el2));

	// TODO: Currently we allow debug access for all VCPUs by
	// default and ignore any vcpu config.
	(void)options;
	vcpu_option_flags_set_debug_allowed(&thread->vcpu_options, true);
	vcpu_runtime_flags_set_debug_active(&thread->vcpu_flags, false);

	return true;
}

void
vdebug_handle_thread_save_state(void)
{
	thread_t *current = thread_get_self();

	if (compiler_unexpected(vcpu_runtime_flags_get_debug_active(
		    &current->vcpu_flags))) {
		assert(current->kind == THREAD_KIND_VCPU);

		// Context-switch the debug registers only if
		// - The device security state disallows debugging, or
		// - The device security state allows debugging and the
		//   external debugger has not claimed the debug module.
		bool need_save = platform_security_state_debug_disabled();
		if (compiler_unexpected(!need_save)) {
#if defined(PLATFORM_HAS_NO_DBGCLAIM_EL1) && PLATFORM_HAS_NO_DBGCLAIM_EL1
			DBGCLAIM_EL1_t dbgclaim = DBGCLAIM_EL1_default();
#else
			DBGCLAIM_EL1_t dbgclaim =
				register_DBGCLAIMCLR_EL1_read_ordered(
					&vdebug_asm_order);
#endif
			need_save = !DBGCLAIM_EL1_get_debug_ext(&dbgclaim);
		}

		bool vdebug_enabled = false;
		if (compiler_expected(need_save)) {
			vdebug_enabled = debug_save_common(
				&current->vdebug_state, &vdebug_asm_order);
		}

		// If debug is no longer in use, ensure register accesses will
		// be trapped when we next switch back to this VCPU, so we can
		// safely avoid restoring the registers.
		if (!vdebug_enabled) {
			MDCR_EL2_set_TDA(&current->vcpu_regs_el2.mdcr_el2,
					 true);
			vcpu_runtime_flags_set_debug_active(
				&current->vcpu_flags, false);
		}
	}
}

void
vdebug_handle_thread_context_switch_post(thread_t *prev)
{
	thread_t *current = thread_get_self();

	if (compiler_unexpected(
		    vcpu_runtime_flags_get_debug_active(&prev->vcpu_flags) &&
		    !vcpu_runtime_flags_get_debug_active(
			    &current->vcpu_flags))) {
		// Write zeros to MDSCR_EL1.MDE and MDSCR_EL1.SS to disable
		// breakpoints and single-stepping, in case the previous VCPU
		// had them enabled.
		register_MDSCR_EL1_write_ordered(MDSCR_EL1_default(),
						 &vdebug_asm_order);
	}
}

void
vdebug_handle_thread_load_state(void)
{
	thread_t *current = thread_get_self();

	if (compiler_unexpected(vcpu_runtime_flags_get_debug_active(
		    &current->vcpu_flags))) {
		// Context-switch the debug registers only if
		// - The device security state disallows debugging, or
		// - The device security state allows debugging and the
		//   external debugger has not claimed the debug module.
		bool need_load = platform_security_state_debug_disabled();
		if (compiler_unexpected(!need_load)) {
#if defined(PLATFORM_HAS_NO_DBGCLAIM_EL1) && PLATFORM_HAS_NO_DBGCLAIM_EL1
			DBGCLAIM_EL1_t dbgclaim = DBGCLAIM_EL1_default();
#else
			DBGCLAIM_EL1_t dbgclaim =
				register_DBGCLAIMCLR_EL1_read_ordered(
					&vdebug_asm_order);
#endif
			need_load = !DBGCLAIM_EL1_get_debug_ext(&dbgclaim);
		}
		if (compiler_expected(need_load)) {
			debug_load_common(&current->vdebug_state,
					  &vdebug_asm_order);
		}
	}
}

// Common vcpu debug access handling.
//
// When this returns VCPU_TRAP_RESULT_EMULATED, the caller must emulate the
// instruction, which may include RAZ/WI.
static vcpu_trap_result_t
vdebug_handle_vcpu_debug_trap(void)
{
	vcpu_trap_result_t ret;
	thread_t	  *current = thread_get_self();

	bool external_debug = !platform_security_state_debug_disabled();
	if (compiler_unexpected(external_debug)) {
#if defined(PLATFORM_HAS_NO_DBGCLAIM_EL1) && PLATFORM_HAS_NO_DBGCLAIM_EL1
		DBGCLAIM_EL1_t dbgclaim = DBGCLAIM_EL1_default();
#else
		DBGCLAIM_EL1_t dbgclaim = register_DBGCLAIMCLR_EL1_read_ordered(
			&vdebug_asm_order);
#endif
		external_debug = DBGCLAIM_EL1_get_debug_ext(&dbgclaim);
	}

	if (!vcpu_option_flags_get_debug_allowed(&current->vcpu_options)) {
		// This VCPU isn't allowed to access debug. Fault immediately.
		ret = VCPU_TRAP_RESULT_FAULT;
	} else if (external_debug) {
		// The device security state allows debugging and the external
		// debugger has claimed the debug module.
		ret = VCPU_TRAP_RESULT_EMULATED;
	} else if (!vcpu_runtime_flags_get_debug_active(&current->vcpu_flags)) {
		// Lazily enable debug register access and restore context.
		vcpu_runtime_flags_set_debug_active(&current->vcpu_flags, true);
		debug_load_common(&current->vdebug_state, &vdebug_asm_order);

		// Disable the register access trap and retry.
		MDCR_EL2_set_TDA(&current->vcpu_regs_el2.mdcr_el2, false);
		register_MDCR_EL2_write(current->vcpu_regs_el2.mdcr_el2);
		ret = VCPU_TRAP_RESULT_RETRY;
	} else {
		// Possibly attempted OS lock or MDCR_EL2.TDCC is set?
		ret = VCPU_TRAP_RESULT_EMULATED;
	}

	return ret;
}

vcpu_trap_result_t
vdebug_handle_vcpu_trap_sysreg(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret;

	if (compiler_expected(ESR_EL2_ISS_MSR_MRS_get_Op0(&iss) != 2U)) {
		// Not a debug register access.
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else {
		ret = vdebug_handle_vcpu_debug_trap();

		if (ret == VCPU_TRAP_RESULT_EMULATED) {
			// Use default debug handler implementing RAZ/WI.
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
	}

	return ret;
}

#if ARCH_AARCH64_32BIT_EL0
vcpu_trap_result_t
vdebug_handle_vcpu_trap_ldcstc_guest(ESR_EL2_ISS_LDC_STC_t iss)
{
	vcpu_trap_result_t ret = vdebug_handle_vcpu_debug_trap();

	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		// Its complicated to emulate a load/store, just warn for now.
		(void)iss;

		TRACE_AND_LOG(ERROR, WARN,
			      "Warning, trapped AArch32 LDC/STC 0 ignored");
	}

	return ret;
}

vcpu_trap_result_t
vdebug_handle_vcpu_trap_mcrmrc14_guest(ESR_EL2_ISS_MCR_MRC_t iss)
{
	vcpu_trap_result_t ret;

	if (ESR_EL2_ISS_MCR_MRC_get_Opc1(&iss) != 0U) {
		// Not a debug register
		ret = VCPU_TRAP_RESULT_UNHANDLED;
	} else {
		ret = vdebug_handle_vcpu_debug_trap();
	}

	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		thread_t *current = thread_get_self();

		if (ESR_EL2_ISS_MCR_MRC_get_Direction(&iss) == 1) {
			if ((ESR_EL2_ISS_MCR_MRC_get_CV(&iss) == 0) ||
			    (ESR_EL2_ISS_MCR_MRC_get_COND(&iss) != 0xeU)) {
				// TODO: Need to read COND/ITState/condition
				// flags to determined whether to emulate or
				// ignore.
				TRACE_AND_LOG(
					ERROR, WARN,
					"Warning, trapped conditional AArch32 debug register");
				ret = VCPU_TRAP_RESULT_UNHANDLED;
			} else {
				// Debug registers read RAZ by default
				vcpu_gpr_write(current,
					       ESR_EL2_ISS_MCR_MRC_get_Rt(&iss),
					       0U);
			}
		} else {
			// Write Ignored
		}
	}

	return ret;
}
#endif
