// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

static inline psci_ret_t
psci_smc_fn_call(psci_function_t fn, register_t arg_0, register_t arg_1,
		 register_t arg_2)
{
	smccc_function_id_t fn_id = smccc_function_id_default();
	smccc_function_id_set_is_fast(&fn_id, true);
	smccc_function_id_set_is_smc64(&fn_id, true);
	smccc_function_id_set_interface_id(&fn_id, SMCCC_INTERFACE_ID_STANDARD);
	smccc_function_id_set_function(&fn_id, (smccc_function_t)fn);

	uint64_t hyp_args[6] = { arg_0, arg_1, arg_2, 0, 0, 0 };
	uint64_t hyp_ret[4]  = { 0 };

	smccc_1_1_call(fn_id, &hyp_args, &hyp_ret, NULL, CLIENT_ID_HYP);

	return (psci_ret_t)hyp_ret[0];
}

static inline psci_ret_t
psci_smc_fn_call32(psci_function_t fn, uint32_t arg_0, uint32_t arg_1,
		   uint32_t arg_2)
{
	smccc_function_id_t fn_id = smccc_function_id_default();
	smccc_function_id_set_is_fast(&fn_id, true);
	smccc_function_id_set_is_smc64(&fn_id, false);
	smccc_function_id_set_interface_id(&fn_id, SMCCC_INTERFACE_ID_STANDARD);
	smccc_function_id_set_function(&fn_id, (smccc_function_t)fn);

	uint64_t hyp_args[6] = { arg_0, arg_1, arg_2, 0, 0, 0 };
	uint64_t hyp_ret[4]  = { 0 };

	smccc_1_1_call(fn_id, &hyp_args, &hyp_ret, NULL, CLIENT_ID_HYP);

	return (psci_ret_t)hyp_ret[0];
}

static inline register_t
psci_smc_fn_call_reg(psci_function_t fn, register_t arg_0, register_t arg_1,
		     register_t arg_2)
{
	smccc_function_id_t fn_id = smccc_function_id_default();
	smccc_function_id_set_is_fast(&fn_id, true);
	smccc_function_id_set_is_smc64(&fn_id, true);
	smccc_function_id_set_interface_id(&fn_id, SMCCC_INTERFACE_ID_STANDARD);
	smccc_function_id_set_function(&fn_id, (smccc_function_t)fn);

	uint64_t hyp_args[6] = { arg_0, arg_1, arg_2, 0, 0, 0 };
	uint64_t hyp_ret[4]  = { 0 };

	smccc_1_1_call(fn_id, &hyp_args, &hyp_ret, NULL, CLIENT_ID_HYP);

	return hyp_ret[0];
}
