// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <smccc.h>

#include "event_handlers.h"

void
soc_qemu_handle_power_system_off(void)
{
	uint64_t hyp_args[6] = { 0 };
	uint64_t hyp_ret[4]  = { 0 };

	smccc_function_id_t fn_id = smccc_function_id_default();

	smccc_function_id_set_owner_id(&fn_id, SMCCC_OWNER_ID_STANDARD);
	smccc_function_id_set_function(&fn_id, PSCI_FUNCTION_SYSTEM_OFF);
	smccc_function_id_set_is_smc64(&fn_id, false);
	smccc_function_id_set_is_fast(&fn_id, true);

	smccc_1_1_call(fn_id, &hyp_args, &hyp_ret, NULL, CLIENT_ID_HYP);
}

bool
soc_qemu_handle_power_system_reset(uint64_t reset_type, uint64_t cookie,
				   error_t *error)
{
	(void)reset_type;
	(void)cookie;

	// FIXME: when doing system_reset on QEMU, hypervisor was starting in the
	// correct entry point, but the static variables did not seem to be
	// reinitialized. When this is fixed, handle this call by doing a
	// PSCI_FUNCTION_SYSTEM_RESET/2 SMC

	*error = ERROR_UNIMPLEMENTED;

	return true;
}
