// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(HYPERCALLS)
#include <hyptypes.h>

#include <hypcall_def.h>

#include <platform_security.h>
#include <trace.h>

hypercall_trace_update_class_flags_result_t
hypercall_trace_update_class_flags(register_t set_flags, register_t clear_flags)
{
	hypercall_trace_update_class_flags_result_t res = { 0 };

	if (platform_security_state_debug_disabled()) {
		res.error = ERROR_DENIED;
		goto out;
	}

	// Clear the bits that hypercalls are not allowed to change
	register_t set	 = set_flags & trace_public_class_flags;
	register_t clear = clear_flags & trace_public_class_flags;

	trace_update_class_flags(set, clear);

	res.flags = trace_get_class_flags();
	res.error = OK;

out:
	return res;
}
#else
extern int unused;
#endif
