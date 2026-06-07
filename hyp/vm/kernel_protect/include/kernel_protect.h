// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#define KP_FUNCTION_PERVM(fn, h, ...)                                          \
	subscribe smccc_call_fast_64_vendor_hyp[(                              \
		smccc_function_t)KERNEL_PROTECT_FUNCTION_##fn];                \
	handler	  kernel_protect_##h(__VA_ARGS__);                             \
	exclude_preempt_disabled
