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

	register uint32_t   w0 __asm__("w0") = smccc_function_id_raw(fn_id);
	register register_t x1 __asm__("x1") = arg_0;
	register register_t x2 __asm__("x2") = arg_1;
	register register_t x3 __asm__("x3") = arg_2;

	// Assumes SMCCC v1.0 compatibility with X4-X17 as scratch registers.
	__asm__ volatile("smc    #0\n"
			 : "+r"(w0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17");

	return (psci_ret_t)w0;
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

	register uint32_t w0 __asm__("w0") = smccc_function_id_raw(fn_id);
	register uint32_t w1 __asm__("w1") = arg_0;
	register uint32_t w2 __asm__("w2") = arg_1;
	register uint32_t w3 __asm__("w3") = arg_2;

	// Assumes SMCCC v1.0 compatibility with X4-X17 as scratch registers.
	__asm__ volatile("smc    #0\n"
			 : "+r"(w0), "+r"(w1), "+r"(w2), "+r"(w3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17");

	return (psci_ret_t)w0;
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

	register register_t x0 __asm__("x0") = smccc_function_id_raw(fn_id);
	register register_t x1 __asm__("x1") = arg_0;
	register register_t x2 __asm__("x2") = arg_1;
	register register_t x3 __asm__("x3") = arg_2;

	// Assumes SMCCC v1.0 compatibility with X4-X17 as scratch registers.
	__asm__ volatile("smc    #0\n"
			 : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3)
			 :
			 : "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
			   "x12", "x13", "x14", "x15", "x16", "x17");

	return x0;
}
