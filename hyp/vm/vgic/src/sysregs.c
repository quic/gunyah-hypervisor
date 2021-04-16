// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <scheduler.h>
#include <thread.h>
#include <vcpu.h>

#include <asm/system_registers.h>

#include "event_handlers.h"
#include "internal.h"

vcpu_trap_result_t
vgic_handle_vcpu_trap_sysreg_write(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret;
	thread_t *	   thread = thread_get_self();
	vic_t *		   vic	  = thread->vgic_vic;
	if (vic == NULL) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Read the thread's register
	uint8_t	   reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);
	register_t val	   = vcpu_gpr_read(thread, reg_num);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_set_Rt(&iss, 0);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(iss)) {
	case ISS_MRS_MSR_ICC_DIR_EL1:
		vgic_icc_irq_deactivate(
			vic, ICC_DIR_EL1_get_INTID(&ICC_DIR_EL1_cast(val)));
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;

	case ISS_MRS_MSR_ICC_ASGI1R_EL1:
		// ICC_ASGI1R_EL1 is treated as an alias of ICC_SGI0R_EL1.
		// This is because virtual accesses are always non-secure, and
		// non-secure writes generate SGIs for group 0 or secure group
		// 1, where the latter is treated as group 0 too because
		// GICD_CTLR.DS=1.
	case ISS_MRS_MSR_ICC_SGI0R_EL1:
		vgic_icc_generate_sgi(vic, ICC_SGIR_EL1_cast(val), false);
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;

	case ISS_MRS_MSR_ICC_SGI1R_EL1:
		vgic_icc_generate_sgi(vic, ICC_SGIR_EL1_cast(val), true);
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;

	case ISS_MRS_MSR_ICC_SRE_EL1:
		// WI
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;

	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

out:
	return ret;
}

vcpu_trap_result_t
vgic_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val = 0U;
	vcpu_trap_result_t ret;
	thread_t *	   thread = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_ICC_SRE_EL1: {
		// Return 1 for SRE, DFB and DIB
		ICC_SRE_EL1_t sre;
		ICC_SRE_EL1_init(&sre);
		ICC_SRE_EL1_set_SRE(&sre, true);
		ICC_SRE_EL1_set_DFB(&sre, true);
		ICC_SRE_EL1_set_DIB(&sre, true);
		val = ICC_SRE_EL1_raw(sre);
		ret = VCPU_TRAP_RESULT_EMULATED;
		break;
	}
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
