// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#define SMCCC_ARCH_FUNCTION_32(fn, feat, h, ...)                               \
	subscribe smccc_call_fast_32_arch[SMCCC_ARCH_FUNCTION_##fn];           \
	handler	  smccc_##h(__VA_ARGS__);                                      \
	exclude_preempt_disabled.subscribe                                     \
		 smccc_arch_features_fast32[SMCCC_ARCH_FUNCTION_##fn];         \
	constant feat.

#define SMCCC_ARCH_FUNCTION_64(fn, feat, h, ...)                               \
	subscribe smccc_call_fast_64_arch[SMCCC_ARCH_FUNCTION_##fn];           \
	handler	  smccc_##h(__VA_ARGS__);                                      \
	exclude_preempt_disabled.subscribe                                     \
		 smccc_arch_features_fast64[SMCCC_ARCH_FUNCTION_##fn];         \
	constant feat.

#define SMCCC_STANDARD_HYP_FUNCTION_32(fn, feat, h, ...)                       \
	subscribe smccc_call_fast_32_standard_hyp                              \
		[SMCCC_STANDARD_HYP_FUNCTION_##fn];                            \
	handler				   smccc_##h(__VA_ARGS__);             \
	exclude_preempt_disabled.subscribe smccc_standard_hyp_features_fast32  \
		[SMCCC_STANDARD_HYP_FUNCTION_##fn];                            \
	constant feat.

#define SMCCC_STANDARD_HYP_FUNCTION_64(fn, feat, h, ...)                       \
	subscribe smccc_call_fast_64_standard_hyp                              \
		[SMCCC_STANDARD_HYP_FUNCTION_##fn];                            \
	handler				   smccc_##h(__VA_ARGS__);             \
	exclude_preempt_disabled.subscribe smccc_standard_hyp_features_fast64  \
		[SMCCC_STANDARD_HYP_FUNCTION_##fn];                            \
	constant feat.
