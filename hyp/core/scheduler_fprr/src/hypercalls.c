// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <scheduler.h>
#include <thread.h>

error_t
hypercall_scheduler_yield(scheduler_yield_control_t control, register_t arg1)
{
	scheduler_yield_hint_t hint =
		scheduler_yield_control_get_hint(&control);
	error_t ret;

	if (scheduler_yield_control_get_impl_def(&control)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	switch (hint) {
	case SCHEDULER_YIELD_HINT_YIELD:
		ret = OK;
		scheduler_yield();
		break;
	case SCHEDULER_YIELD_HINT_YIELD_TO_THREAD: {
		cap_id_t obj_id = (cap_id_t)arg1;

		thread_ptr_result_t result = cspace_lookup_thread(
			cspace_get_self(), obj_id, CAP_RIGHTS_THREAD_YIELD_TO);
		if (result.e != OK) {
			ret = result.e;
			goto out;
		}

		if (result.r != thread_get_self()) {
			scheduler_yield_to(result.r);
		}

		object_put_thread(result.r);
		ret = OK;
		break;
	}
	case SCHEDULER_YIELD_HINT_YIELD_LOWER:
	default:
		ret = ERROR_ARGUMENT_INVALID;
		break;
	}

out:
	return ret;
}
