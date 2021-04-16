// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <scheduler.h>

#include <events/vcpu.h>

#include "event_handlers.h"

void
vcpu_handle_object_get_defaults_thread(thread_create_t *create)
{
	assert(create != NULL);
	assert(create->kind == THREAD_KIND_NONE);

	create->kind = THREAD_KIND_VCPU;
}

error_t
vcpu_handle_object_create_thread(thread_create_t create)
{
	thread_t *thread = create.thread;
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		scheduler_block_init(thread, SCHEDULER_BLOCK_VCPU_OFF);
	}

	return OK;
}

error_t
vcpu_handle_object_activate_thread(thread_t *thread)
{
	error_t ret = OK;

	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		if (thread->cspace_cspace == NULL) {
			ret = ERROR_OBJECT_CONFIG;
			goto out;
		}
		// Reset thread's vcpu_options. Event handlers can set them
		// again. This prevents unchecked options from configure phase
		// being left in the thread options.
		vcpu_option_flags_t options = thread->vcpu_options;
		thread->vcpu_options	    = vcpu_option_flags_default();

		if (!trigger_vcpu_activate_thread_event(thread, options)) {
			ret = ERROR_OBJECT_CONFIG;
		}
	}

out:
	return ret;
}
