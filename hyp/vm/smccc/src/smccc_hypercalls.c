// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <smccc_platform.h>
#include <thread.h>

#include <events/smccc.h>

#include "smccc_hypercall.h"

bool
smccc_handle_hypercall_wrapper(smccc_function_id_t smc_id, bool is_hvc)
{
	bool handled;

	smccc_function_t smc_func  = smccc_function_id_get_function(&smc_id);
	smccc_owner_id_t smc_owner = smccc_function_id_get_owner_id(&smc_id);

	if (smc_owner != SMCCC_OWNER_ID_VENDOR_HYP) {
		handled = false;
		goto out;
	}

	bool is_smc64 = smccc_function_id_get_is_smc64(&smc_id);
	bool is_fast  = smccc_function_id_get_is_fast(&smc_id);

	thread_t   *current = thread_get_self();
	register_t *args    = &current->vcpu_regs_gpr.x[0];

	smccc_vendor_hyp_function_id_t smc_type =
		smccc_vendor_hyp_function_id_cast((uint16_t)(smc_func));

	switch (smccc_vendor_hyp_function_id_get_call_class(&smc_type)) {
	case SMCCC_VENDOR_HYP_FUNCTION_CLASS_PLATFORM_CALL:
		handled = smccc_handle_smc_platform_call(args, is_hvc);
		break;
	case SMCCC_VENDOR_HYP_FUNCTION_CLASS_HYPERCALL:
		if (is_fast && is_smc64) {
			uint32_t hyp_num =
				smccc_vendor_hyp_function_id_get_function(
					&smc_type);
			smccc_hypercall_table_wrapper(hyp_num, args);
		} else {
			args[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;
		}
		handled = true;
		break;
	case SMCCC_VENDOR_HYP_FUNCTION_CLASS_SERVICE:
		if (is_fast && !is_smc64) {
			uint16_t func =
				smccc_vendor_hyp_function_id_get_function(
					&smc_type);
			switch ((smccc_vendor_hyp_function_t)func) {
			case SMCCC_VENDOR_HYP_FUNCTION_CALL_UID:
				args[0] = SMCCC_GUNYAH_UID0;
				args[1] = SMCCC_GUNYAH_UID1;
				args[2] = SMCCC_GUNYAH_UID2;
				args[3] = SMCCC_GUNYAH_UID3;
				break;
			case SMCCC_VENDOR_HYP_FUNCTION_REVISION:
				args[0] = (register_t)hyp_api_info_raw(
					hyp_api_info_default());
				break;
			case SMCCC_VENDOR_HYP_FUNCTION_CALL_COUNT:
				// Deprecated
			default:
				args[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;
				break;
			}
		} else {
			args[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;
		}
		handled = true;
		break;
	default:
		args[0] = (register_t)SMCCC_UNKNOWN_FUNCTION64;
		handled = true;
		break;
	}

out:
	return handled;
}
