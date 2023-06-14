// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <irq.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <thread.h>
#include <trace.h>
#if defined(INTERFACE_VCPU)
#include <vcpu.h>
#endif

#include <asm/barrier.h>
#include <asm/system_registers.h>

#include "event_handlers.h"

#if defined(INTERFACE_VCPU)

// The module to trap and emulate the read accesses to the cluster register. We
// currently only emulate IMP_CLUSTERIDR_EL1 and treat the rest as RAZ.

// Before reading any cluster registers we need to apply the DSU SCLK
// gating erratum (2,313,941) workaround, which is executing a dummy
// cache maintenance operation instruction immediately prior to
// accessing the register.
static inline void
platform_dsu_apply_sclk_gating_erratum_workaround(void) REQUIRE_PREEMPT_DISABLED
{
	register_t dummy = 0;

	assert_preempt_disabled();
	__asm__ volatile("DC CIVAC, %[VA]; "
			 : "+m"(asm_ordering)
			 : [VA] "r"(&dummy));
}

static inline IMP_CLUSTERIDR_EL1_t
register_CLUSTERIDR_EL1_read(void)
{
	IMP_CLUSTERIDR_EL1_t val;

	preempt_disable();
	platform_dsu_apply_sclk_gating_erratum_workaround();
	val = register_IMP_CLUSTERIDR_EL1_read_ordered(&asm_ordering);
	preempt_enable();

	return val;
}

vcpu_trap_result_t
arm_dsu_handle_vcpu_trap_sysreg_read(ESR_EL2_ISS_MSR_MRS_t iss)
{
	register_t	   val	  = 0ULL; // Default action is RAZ
	vcpu_trap_result_t ret	  = VCPU_TRAP_RESULT_EMULATED;
	thread_t	  *thread = thread_get_self();

	// Assert this is a read
	assert(ESR_EL2_ISS_MSR_MRS_get_Direction(&iss));

	uint8_t reg_num = ESR_EL2_ISS_MSR_MRS_get_Rt(&iss);

	// Remove the fields that are not used in the comparison
	ESR_EL2_ISS_MSR_MRS_t temp_iss = iss;
	ESR_EL2_ISS_MSR_MRS_set_Rt(&temp_iss, 0U);
	ESR_EL2_ISS_MSR_MRS_set_Direction(&temp_iss, false);

	switch (ESR_EL2_ISS_MSR_MRS_raw(temp_iss)) {
	case ISS_MRS_MSR_IMP_CLUSTERIDR_EL1: {
		IMP_CLUSTERIDR_EL1_t clusteridr =
			register_CLUSTERIDR_EL1_read();
		val = IMP_CLUSTERIDR_EL1_raw(clusteridr);
		break;
	}
	default: {
		uint8_t crn, crm;

		crn = ESR_EL2_ISS_MSR_MRS_get_CRn(&iss);
		crm = ESR_EL2_ISS_MSR_MRS_get_CRm(&iss);

		if ((crn == 15U) && ((crm == 3U) || (crm == 4U))) {
			TRACE_AND_LOG(DEBUG, WARN,
				      "Emulated RAZ for cluster register: ISS "
				      "{:#x}",
				      ESR_EL2_ISS_MSR_MRS_raw(iss));
		} else {
			ret = VCPU_TRAP_RESULT_UNHANDLED;
		}
		break;
	}
	}

	// Update the thread's register
	if (ret == VCPU_TRAP_RESULT_EMULATED) {
		vcpu_gpr_write(thread, reg_num, val);
	}

	return ret;
}

#endif
