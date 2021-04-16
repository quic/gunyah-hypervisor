// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <thread.h>
#include <vcpu.h>

#include <asm/system_registers.h>
#include <asm/system_registers_cpu.h>

#include "event_handlers.h"

vcpu_trap_result_t
sysreg_read_cpu(ESR_EL2_ISS_MSR_MRS_t iss)
{
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t *	   thread = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_set_Rt(&iss, 0);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(iss)) {
#if !defined(MODULE_RAS)
	// The RAS registers trapped with HCR_EL2.TERR
	case ISS_MRS_MSR_ERXPFGFR_EL1:
	case ISS_MRS_MSR_ERXPFGCTLR_EL1:
	case ISS_MRS_MSR_ERXPFGCDNR_EL1:

		// RAZ. No support for pseudo fault generation
		break;
#endif
	default:
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		break;
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, 0U);
	}

	return ret;
}

//  ACTLR_EL2 defaults to zero on reset, which disables write access to these
//  registers and traps them to EL2. We want to keep it that way for now as
//  writing to these registers generally has dangerous side effects and we don't
//  want the guest to mess with them.
vcpu_trap_result_t
sysreg_write_cpu(ESR_EL2_ISS_MSR_MRS_t iss)
{
	uint8_t		   opc0, opc1, crn, crm;
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_EMULATED;

	// Assert this is a write
	assert(!ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_CPUACTLR_EL1:
	// CPUACTLR2_EL1 does not exist on A55
	case ISS_MRS_MSR_A7X_CPUACTLR2_EL1:
	case ISS_MRS_MSR_CPUECTLR_EL1:
	case ISS_MRS_MSR_CPUPWRCTLR_EL1:

		// WI
		break;
#if !defined(MODULE_RAS)
	// HCR_EL2.TERR also has a hand in trapping these
	case ISS_MRS_MSR_ERXPFGFR_EL1:
	case ISS_MRS_MSR_ERXPFGCTLR_EL1:
	case ISS_MRS_MSR_ERXPFGCDNR_EL1:

		// WI. No support for pseudo fault generation
		break;
#endif
	default:
		opc0 = ESR_EL2_ISS_MSR_MRS_get_Op0(&iss);
		opc1 = ESR_EL2_ISS_MSR_MRS_get_Op1(&iss);
		crn  = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		crm  = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((opc0 == 3) && (opc1 == 0) && (crn == 15) && (crm >= 3) &&
		    (crm <= 4)) {
			// CLUSTER* registers, all WI.
		} else if ((opc0 == 3) && ((opc1 == 0) || (opc1 == 6)) &&
			   (crn == 15) && (crm >= 5) && (crm <= 6)) {
			// CLUSTERPM* registers, all WI.
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
		break;
	}

	return ret;
}
