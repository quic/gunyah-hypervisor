// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <panic.h>
#include <thread.h>
#include <vcpu.h>

#include "event_handlers.h"

#if defined(ARCH_ARM_FEAT_MPAM) && ARCH_ARM_FEAT_MPAM

#include <arm_mpam.h>
#include <compiler.h>
#include <globals.h>
#include <log.h>
#include <platform_features.h>
#include <trace.h>

#include <asm/vfp_helpers.h>

// A simple implementation of MPAM:
// - Only designated VMs have access to MPAM
// - No PARTID virtualisation
// - Not used in EL2

static bool mpam_has_hcr  = false;
static bool mpam_has_tidr = false;

bool
arm_mpam_is_allowed(void)
{
	const global_options_t *global_options = globals_get_options();
	return compiler_expected(global_options_get_mpam(global_options));
}

void
arm_vm_mpam_direct_handle_boot_cold_init(void)
{
	bool mpam_enabled = false;

	platform_cpu_features_t features = platform_get_cpu_features();
	if (!platform_cpu_features_get_mpam_disable(&features)) {
		MPAM2_EL2_t mpam2 = register_MPAM2_EL2_read_volatile();
		mpam_enabled	  = MPAM2_EL2_get_MPAMEN(&mpam2);
	}

	global_options_t options = global_options_default();
	global_options_set_mpam(&options, true);
	if (mpam_enabled) {
		globals_set_options(options);
	} else {
		globals_clear_options(options);
	}

	if (mpam_enabled) {
		MPAMIDR_EL1_t mpamidr = register_MPAMIDR_EL1_read();
		mpam_has_hcr	      = MPAMIDR_EL1_get_HAS_HCR(&mpamidr);
#if defined(ARCH_ARM_FEAT_MPAMv0p1) || defined(ARCH_ARM_FEAT_MPAMv1p1)
		mpam_has_tidr = MPAMIDR_EL1_get_HAS_TIDR(&mpamidr);
#endif
		if (!mpam_has_hcr && !mpam_has_tidr) {
			TRACE_AND_LOG(
				DEBUG, WARN,
				"Trapping MPAMIDR not supported in the hardware.\n");
		}
	}
}

void
arm_vm_mpam_direct_handle_boot_cpu_warm_init(void)
{
	// Initialise the MPAM registers
	if (arm_mpam_is_allowed()) {
		register_MPAM0_EL1_write(MPAM0_EL1_cast(0));
		register_MPAM1_EL1_write(MPAM1_EL1_cast(0));
#if defined(ARCH_ARM_FEAT_SME)
		if (vfp_sme_implemented()) {
			register_MPAMSM_EL1_write(MPAMSM_EL1_default());
		}
#endif

		MPAM2_EL2_t mpam2_el2 = MPAM2_EL2_default();
		MPAM2_EL2_set_TRAPMPAM0EL1(&mpam2_el2, true);
		MPAM2_EL2_set_TRAPMPAM1EL1(&mpam2_el2, true);
#if defined(ARCH_ARM_FEAT_MPAMv0p1) || defined(ARCH_ARM_FEAT_MPAMv1p1)
		if (mpam_has_tidr) {
			MPAM2_EL2_set_TIDR(&mpam2_el2, true);
		}
#endif
		register_MPAM2_EL2_write(mpam2_el2);

		if (mpam_has_hcr) {
			MPAMHCR_EL2_t mpamhcr_el2 = MPAMHCR_EL2_default();
			MPAMHCR_EL2_set_TRAP_MPAMIDR_EL1(&mpamhcr_el2, true);
			MPAMHCR_EL2_set_EL0_VPMEN(&mpamhcr_el2, false);
			MPAMHCR_EL2_set_EL1_VPMEN(&mpamhcr_el2, false);
			register_MPAMHCR_EL2_write(mpamhcr_el2);
		}
	}
}

bool
arm_vm_mpam_direct_handle_vcpu_activate_thread(thread_t		  *thread,
					       vcpu_option_flags_t options)
{
	if (!arm_mpam_is_allowed()) {
		goto out;
	}

	assert(thread != NULL);

	if (vcpu_is_vcpu(thread)) {
		thread->mpam.mpam_regs.mpam2_el2 = MPAM2_EL2_default();
		if (vcpu_option_flags_get_mpam_allowed(&options)) {
			// MPAM VM, give access to the registers
			MPAM2_EL2_set_TRAPMPAM0EL1(
				&thread->mpam.mpam_regs.mpam2_el2, false);
			MPAM2_EL2_set_TRAPMPAM1EL1(
				&thread->mpam.mpam_regs.mpam2_el2, false);
#if defined(ARCH_ARM_FEAT_SME)
			// If sme_allowed is set then it necessarily means that
			// SME is implemented.
			bool sme_allowed = vcpu_option_flags_get_sme_allowed(
				&thread->vcpu_options);
			MPAM2_EL2_set_EnMPAMSM(
				&thread->mpam.mpam_regs.mpam2_el2, sme_allowed);
#endif
#if defined(ARCH_ARM_FEAT_MPAMv0p1) || defined(ARCH_ARM_FEAT_MPAMv1p1)
			if (mpam_has_tidr) {
				MPAM2_EL2_set_TIDR(
					&thread->mpam.mpam_regs.mpam2_el2,
					false);
			}
#endif
			if (mpam_has_hcr) {
				MPAMHCR_EL2_set_TRAP_MPAMIDR_EL1(
					&thread->mpam.mpam_regs.mpamhcr_el2,
					false);
			}
			vcpu_option_flags_set_mpam_allowed(
				&thread->vcpu_options, true);
		} else {
			// No MPAM access, enable all the traps
			MPAM2_EL2_set_TRAPMPAM0EL1(
				&thread->mpam.mpam_regs.mpam2_el2, true);
			MPAM2_EL2_set_TRAPMPAM1EL1(
				&thread->mpam.mpam_regs.mpam2_el2, true);
#if defined(ARCH_ARM_FEAT_SME)
			MPAM2_EL2_set_EnMPAMSM(
				&thread->mpam.mpam_regs.mpam2_el2, false);
#endif
#if defined(ARCH_ARM_FEAT_MPAMv0p1) || defined(ARCH_ARM_FEAT_MPAMv1p1)
			if (mpam_has_tidr) {
				MPAM2_EL2_set_TIDR(
					&thread->mpam.mpam_regs.mpam2_el2,
					true);
			}
#endif
			if (mpam_has_hcr) {
				MPAMHCR_EL2_set_TRAP_MPAMIDR_EL1(
					&thread->mpam.mpam_regs.mpamhcr_el2,
					true);
			}
		}

		// Turn off MPAM virtualisation
		if (mpam_has_hcr) {
			MPAMHCR_EL2_set_EL0_VPMEN(
				&thread->mpam.mpam_regs.mpamhcr_el2, false);
			MPAMHCR_EL2_set_EL1_VPMEN(
				&thread->mpam.mpam_regs.mpamhcr_el2, false);
		}
	}

out:
	return true;
}

void
arm_vm_mpam_direct_handle_vcpu_save_state(void)
{
	thread_t *thread = thread_get_self();

	// Save the MPAM state only if MPAM is enabled and either the current or
	// the next thread has MPAM access. If neither has MPAM access, no need
	// to save anything.
	if (arm_mpam_is_allowed()) {
		if (vcpu_option_flags_get_mpam_allowed(&thread->vcpu_options)) {
			thread->mpam.mpam_regs.mpam0_el1 =
				register_MPAM0_EL1_read_volatile();
			thread->mpam.mpam_regs.mpam1_el1 =
				register_MPAM1_EL1_read_volatile();
#if defined(ARCH_ARM_FEAT_SME)
			if (vcpu_option_flags_get_sme_allowed(
				    &thread->vcpu_options)) {
				thread->mpam.mpam_regs.mpamsm_el1 =
					register_MPAMSM_EL1_read_volatile();
			}
#endif
		}
	}
}

void
arm_vm_mpam_direct_handle_vcpu_load_state(void)
{
	// Load the MPAM state only if MPAM is enabled and either the current or
	// the previous thread has MPAM access. If neither has MPAM access, no
	// need to load anything.
	if (arm_mpam_is_allowed()) {
		thread_t *thread = thread_get_self();

		register_MPAM0_EL1_write(thread->mpam.mpam_regs.mpam0_el1);
		register_MPAM1_EL1_write(thread->mpam.mpam_regs.mpam1_el1);
		register_MPAM2_EL2_write(thread->mpam.mpam_regs.mpam2_el2);
		if (mpam_has_hcr) {
			register_MPAMHCR_EL2_write(
				thread->mpam.mpam_regs.mpamhcr_el2);
		}
#if defined(ARCH_ARM_FEAT_SME)
		if (vcpu_option_flags_get_sme_allowed(&thread->vcpu_options)) {
			register_MPAMSM_EL1_write(
				thread->mpam.mpam_regs.mpamsm_el1);
		}
#endif
	}
}

#else // FEAT_MPAM not defined

void
arm_vm_mpam_direct_handle_boot_cold_init(void)
{
	ID_AA64PFR0_EL1_t pfr0 = register_ID_AA64PFR0_EL1_read();
	ID_AA64PFR1_EL1_t pfr1 = register_ID_AA64PFR1_EL1_read();

	// Panic if the hardware has MPAM, but FEAT_MPAM is missing from the
	// configuration by mistake
	if ((ID_AA64PFR0_EL1_get_MPAM(&pfr0) != 0) ||
	    (ID_AA64PFR1_EL1_get_MPAM_frac(&pfr1) != 0)) {
		panic("MPAM exists in hardware but FEAT_MPAM is not defined");
	}
}
#endif
