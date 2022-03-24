// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <platform_cpu.h>
#include <scheduler.h>
#include <util.h>

#include <events/vcpu.h>

#include "event_handlers.h"

void
vcpu_handle_object_get_defaults_thread(thread_create_t *create)
{
	uint32_t stack_size;

	assert(create != NULL);
	assert(create->kind == THREAD_KIND_NONE);

	// This may be 0, which will fall back to the global default
	stack_size = platform_cpu_stack_size();

	assert((stack_size == 0U) ||
	       util_is_baligned(stack_size, PGTABLE_HYP_PAGE_SIZE));
	assert(stack_size <= THREAD_STACK_MAX_SIZE);

	create->stack_size = stack_size;
	create->kind	   = THREAD_KIND_VCPU;
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

		if (((1UL << thread->scheduler_affinity) &
		     PLATFORM_USABLE_CORES) == 0) {
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
