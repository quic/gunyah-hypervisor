// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <platform_features.h>
#include <smccc.h>

platform_cpu_features_t
platform_get_cpu_features(void)
{
	platform_cpu_features_t features = platform_cpu_features_default();

#if defined(MODULE_VM_ARM_VM_MTE)
	platform_cpu_features_set_mte_disable(&features, false);
#endif
#if defined(INTERFACE_VET)
	platform_cpu_features_set_trace_disable(&features, false);
#endif
#if defined(INTERFACE_DEBUG)
	platform_cpu_features_set_debug_disable(&features, false);
#endif
#if defined(MODULE_VM_ARM_VM_SVE_SIMPLE)
	platform_cpu_features_set_sve_disable(&features, false);
#endif

	return features;
}
