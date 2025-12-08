// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <thread.h>
#include <vcpu.h>

#include <asm/system_registers.h>

#include "event_handlers.h"

// A simple LOR module that treats all the registers as RAZ/WI

#if defined(ARCH_ARM_FEAT_LOR)
bool
arm_vm_lor_null_handle_vcpu_activate_thread(thread_t *thread)
{
	assert_debug(thread != NULL);
	assert_debug(vcpu_is_vcpu(thread));

	// Trap accesses to the LOR registers
	HCR_EL2_set_TLOR(&thread->vcpu_regs_el2.hcr_el2, true);

	return true;
}

vcpu_trap_result_t
arm_vm_lor_null_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val = 0;
	vcpu_trap_result_t ret;
	thread_t	  *thread = thread_get_self();

	// Assert this is a read
	assert_debug(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_LORSA_EL1:
	case ISS_MRS_MSR_LOREA_EL1:
	case ISS_MRS_MSR_LORN_EL1:
	case ISS_MRS_MSR_LORC_EL1:
	case ISS_MRS_MSR_LORID_EL1:
		// RAZ
		val = 0U;
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
	}

	return ret;
}

vcpu_trap_result_t
arm_vm_lor_null_handle_vcpu_trap_sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret;

	// Assert this is a write
	assert_debug(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	// Only lorSERNR_EL0 may be written by the guest
	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_LORSA_EL1:
	case ISS_MRS_MSR_LOREA_EL1:
	case ISS_MRS_MSR_LORN_EL1:
	case ISS_MRS_MSR_LORC_EL1:
	case ISS_MRS_MSR_LORID_EL1:
		// WI
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

	return ret;
}
#endif
