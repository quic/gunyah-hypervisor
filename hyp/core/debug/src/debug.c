// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <compiler.h>
#include <log.h>
#include <platform_features.h>
#include <trace.h>

#include "event_handlers.h"

static bool debug_disabled = false;

void
debug_handle_boot_cold_init(void)
{
	platform_cpu_features_t features = platform_get_cpu_features();

	debug_disabled = platform_cpu_features_get_debug_disable(&features);
	if (debug_disabled) {
		LOG(ERROR, INFO, "debug disabled");
	}
}
