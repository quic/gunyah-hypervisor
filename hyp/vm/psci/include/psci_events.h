// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// clang-format off

#define PSCI_FUNCTION(fn, feat, h, ...)				\
subscribe smccc_call_fast_32_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h ## _32(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features32[PSCI_FUNCTION_ ## fn];		\
	constant feat.						\
subscribe smccc_call_fast_64_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h ## _64(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features64[PSCI_FUNCTION_ ## fn];		\
	constant feat.

#define PSCI_FUNCTION32(fn, feat, h, ...)			\
subscribe smccc_call_fast_32_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features32[PSCI_FUNCTION_ ## fn];		\
	constant feat.

#define PSCI_FUNCTION_PERVM(fn, h, ...)				\
subscribe smccc_call_fast_32_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h ## _32(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features32[PSCI_FUNCTION_ ## fn];		\
	handler psci_ ## h ## _32_features().			\
subscribe smccc_call_fast_64_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h ## _64(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features64[PSCI_FUNCTION_ ## fn];		\
	handler psci_ ## h ## _64_features().			\

#define PSCI_FUNCTION32_PERVM(fn, h, ...)			\
subscribe smccc_call_fast_32_standard[(smccc_function_t)PSCI_FUNCTION_ ## fn];	\
	handler psci_ ## h(__VA_ARGS__);			\
	exclude_preempt_disabled.				\
subscribe psci_features32[PSCI_FUNCTION_ ## fn];		\
	handler psci_ ## h ## _features().
