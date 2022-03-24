// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypconstants.h>

#include <panic.h>
#include <smccc.h>

#include "psci_smc.h"
#include "psci_smc_arch.h"

uint32_t
psci_smc_psci_version()
{
	psci_ret_t ret =
		psci_smc_fn_call32(PSCI_FUNCTION_PSCI_VERSION, 0, 0, 0);
	return (uint32_t)ret;
}

error_t
psci_smc_cpu_suspend(register_t power_state, paddr_t entry_point,
		     register_t context_id)
{
	error_t err;

	switch (psci_smc_fn_call(PSCI_FUNCTION_CPU_SUSPEND, power_state,
				 entry_point, context_id)) {
	case PSCI_RET_SUCCESS:
		err = OK;
		break;
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_INVALID_ADDRESS:
		err = ERROR_ARGUMENT_INVALID;
		break;
	case PSCI_RET_DENIED:
		// Note: OS-initiated mode only
		err = ERROR_DENIED;
		break;
	case PSCI_RET_NOT_SUPPORTED:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

#if defined(PLATFORM_PSCI_DEFAULT_SUSPEND)
error_t
psci_smc_cpu_default_suspend(paddr_t entry_point, register_t context_id)
{
	error_t err;

	switch (psci_smc_fn_call(PSCI_FUNCTION_CPU_DEFAULT_SUSPEND, entry_point,
				 context_id, 0U)) {
	case PSCI_RET_SUCCESS:
		err = OK;
		break;
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_INVALID_ADDRESS:
		err = ERROR_ARGUMENT_INVALID;
		break;
	case PSCI_RET_DENIED:
		// Note: OS-initiated mode only
		err = ERROR_DENIED;
		break;
	case PSCI_RET_NOT_SUPPORTED:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}
#endif

error_t
psci_smc_system_reset(void)
{
	error_t err;

	switch (psci_smc_fn_call32(PSCI_FUNCTION_SYSTEM_RESET, 0, 0, 0)) {
	case PSCI_RET_NOT_SUPPORTED:
		err = ERROR_UNIMPLEMENTED;
		break;
	case PSCI_RET_INVALID_PARAMETERS:
		err = ERROR_ARGUMENT_INVALID;
		break;
	case PSCI_RET_SUCCESS:
	case PSCI_RET_DENIED:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	case PSCI_RET_INVALID_ADDRESS:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

error_t
psci_smc_cpu_off()
{
	error_t err;

	switch (psci_smc_fn_call32(PSCI_FUNCTION_CPU_OFF, 0, 0, 0)) {
	case PSCI_RET_DENIED:
		err = ERROR_DENIED;
		break;
	case PSCI_RET_SUCCESS:
	case PSCI_RET_NOT_SUPPORTED:
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	case PSCI_RET_INVALID_ADDRESS:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

error_t
psci_smc_cpu_on(psci_mpidr_t cpu_id, paddr_t entry_point, register_t context_id)
{
	error_t err;

	switch (psci_smc_fn_call(PSCI_FUNCTION_CPU_ON, psci_mpidr_raw(cpu_id),
				 entry_point, context_id)) {
	case PSCI_RET_SUCCESS:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
		err = OK;
		break;
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_INVALID_ADDRESS:
		err = ERROR_ARGUMENT_INVALID;
		break;
	case PSCI_RET_DENIED:
	case PSCI_RET_NOT_SUPPORTED:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

sint32_result_t
psci_smc_psci_features(psci_function_t fn, bool smc64)
{
	smccc_function_id_t fn_id = smccc_function_id_default();
	smccc_function_id_set_is_fast(&fn_id, true);
	smccc_function_id_set_is_smc64(&fn_id, smc64);
	smccc_function_id_set_interface_id(&fn_id, SMCCC_INTERFACE_ID_STANDARD);
	smccc_function_id_set_function(&fn_id, (smccc_function_t)fn);

	sint32_result_t ret;

	ret.r = psci_smc_fn_call32(PSCI_FUNCTION_PSCI_FEATURES,
				   smccc_function_id_raw(fn_id), 0, 0);

	switch ((psci_ret_t)ret.r) {
	case PSCI_RET_NOT_SUPPORTED:
		ret.e = ERROR_UNIMPLEMENTED;
		break;
	case PSCI_RET_SUCCESS:
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_DENIED:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	case PSCI_RET_INVALID_ADDRESS:
	default:
		if (ret.r >= 0) {
			ret.e = OK;
			break;
		}
		panic("Unexpected PSCI result");
	}

	return ret;
}

error_t
psci_smc_cpu_freeze()
{
	error_t err;

	switch (psci_smc_fn_call32(PSCI_FUNCTION_CPU_FREEZE, 0, 0, 0)) {
	case PSCI_RET_NOT_SUPPORTED:
		err = ERROR_UNIMPLEMENTED;
		break;
	case PSCI_RET_DENIED:
		err = ERROR_DENIED;
		break;
	case PSCI_RET_SUCCESS:
	case PSCI_RET_INVALID_PARAMETERS:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	case PSCI_RET_INVALID_ADDRESS:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

error_t
psci_smc_psci_set_suspend_mode(psci_mode_t mode)
{
	error_t err;

	switch (psci_smc_fn_call32(PSCI_FUNCTION_PSCI_SET_SUSPEND_MODE, mode, 0,
				   0)) {
	case PSCI_RET_SUCCESS:
		err = OK;
		break;
	case PSCI_RET_NOT_SUPPORTED:
		err = ERROR_UNIMPLEMENTED;
		break;
	case PSCI_RET_INVALID_PARAMETERS:
		err = ERROR_ARGUMENT_INVALID;
		break;
	case PSCI_RET_DENIED:
		err = ERROR_DENIED;
		break;
	case PSCI_RET_INVALID_ADDRESS:
	case PSCI_RET_ALREADY_ON:
	case PSCI_RET_ON_PENDING:
	case PSCI_RET_INTERNAL_FAILURE:
	case PSCI_RET_NOT_PRESENT:
	case PSCI_RET_DISABLED:
	default:
		panic("Unexpected PSCI result");
	}

	return err;
}

register_t
psci_smc_psci_stat_count(psci_mpidr_t cpu_id, register_t power_state)
{
	return psci_smc_fn_call_reg(PSCI_FUNCTION_PSCI_STAT_COUNT,
				    psci_mpidr_raw(cpu_id), power_state, 0);
}
