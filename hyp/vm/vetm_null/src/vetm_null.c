// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <cpulocal.h>
#include <scheduler.h>
#include <trace.h>
#include <trace_helpers.h>

#include "etm.h"
#include "event_handlers.h"

bool
vetm_null_handle_vdevice_access(vmaddr_t ipa, size_t access_size,
				register_t *value, bool is_write)
{
	bool ret;
	(void)access_size;

	if ((ipa >= PLATFORM_ETM_BASE) &&
	    (ipa < (PLATFORM_ETM_BASE + (ETM_STRIDE * PLATFORM_MAX_CORES)))) {
		// Treat the entire ETM region as RAZ/WI
		if (!is_write) {
			*value = 0U;
		}
		ret = true;
	} else {
		ret = false;
	}

	return ret;
}
