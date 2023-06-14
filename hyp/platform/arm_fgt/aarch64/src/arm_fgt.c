// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#if defined(ARCH_ARM_FEAT_FGT) && ARCH_ARM_FEAT_FGT

#include <hypregisters.h>

#include <arm_fgt.h>
#include <compiler.h>
#include <globals.h>
#include <platform_features.h>

#include "event_handlers.h"

bool
arm_fgt_is_allowed(void)
{
#if defined(PLATFORM_FGT_OPTIONAL)
	const global_options_t *global_options = globals_get_options();
	return compiler_expected(global_options_get_fgt(global_options));
#else
	return true;
#endif
}

void
arm_fgt_handle_boot_cold_init(void)
{
	global_options_t options = global_options_default();
	global_options_set_fgt(&options, true);
	globals_set_options(options);

#if defined(PLATFORM_FGT_OPTIONAL)
	// TZ might be restricting access to FGT, check first
	platform_cpu_features_t features = platform_get_cpu_features();
	if (platform_cpu_features_get_fgt_disable(&features)) {
		globals_clear_options(options);
	}
#endif
}

#if defined(INTERFACE_VCPU)
void
arm_fgt_handle_thread_load_state(void)
{
	thread_t *thread = thread_get_self();
	if (compiler_expected((thread->kind == THREAD_KIND_VCPU) &&
			      arm_fgt_is_allowed())) {
		register_HFGWTR_EL2_write(thread->vcpu_regs_el2.hfgwtr_el2);
	}
}
#endif

#endif
