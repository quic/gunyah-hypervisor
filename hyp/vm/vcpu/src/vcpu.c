// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <cpulocal.h>
#include <panic.h>
#include <platform_cpu.h>
#include <preempt.h>
#include <scheduler.h>
#include <util.h>
#include <vcpu.h>
#include <vic.h>
#include <virq.h>

#include <events/vcpu.h>

#include "event_handlers.h"
#include "vcpu.h"

void
vcpu_handle_object_get_defaults_thread(thread_create_t *create)
{
	uint32_t stack_size;

	assert(create != NULL);

	// This may be 0, which will fall back to the global default
	stack_size = platform_cpu_stack_size();

#if defined(VCPU_MIN_STACK_SIZE)
	stack_size = util_max(stack_size, VCPU_MIN_STACK_SIZE);
#endif

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
	error_t ret;

	if (thread->kind == THREAD_KIND_VCPU) {
		scheduler_block_init(thread, SCHEDULER_BLOCK_VCPU_OFF);
	}

	if (create.scheduler_priority_valid &&
	    (create.scheduler_priority > VCPU_MAX_PRIORITY)) {
		ret = ERROR_DENIED;
	} else {
		ret = OK;
	}

	return ret;
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

		if (cpulocal_index_valid(thread->scheduler_affinity) &&
		    !platform_cpu_exists(thread->scheduler_affinity)) {
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

void
vcpu_handle_thread_exited(void)
{
	thread_t *current = thread_get_self();
	assert(current != NULL);

	assert_preempt_disabled();

	if (current->kind == THREAD_KIND_VCPU) {
		if (vcpu_option_flags_get_critical(&current->vcpu_options)) {
			panic("Critical VCPU exited");
		}

		trigger_vcpu_stopped_event();
	}
}

bool
vcpu_handle_vcpu_activate_thread(thread_t *thread, vcpu_option_flags_t options)
{
	bool ret = false;

	assert(thread != NULL);
	assert(thread->kind == THREAD_KIND_VCPU);

	// Check that the partition has the right to mark the VCPU as critical.
	if (vcpu_option_flags_get_critical(&options) ||
	    vcpu_option_flags_get_hlos_vm(&options)) {
		if (!partition_option_flags_get_privileged(
			    &thread->header.partition->options)) {
			goto out;
		}

		vcpu_option_flags_set_critical(&thread->vcpu_options, true);
	}

	ret = true;

out:
	return ret;
}

void
vcpu_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->kind == THREAD_KIND_VCPU) {
		vic_unbind(&thread->vcpu_halt_virq_src);
	}
}

error_t
vcpu_bind_virq(thread_t *vcpu, vic_t *vic, virq_t virq,
	       vcpu_virq_type_t virq_type)
{
	return trigger_vcpu_bind_virq_event(virq_type, vcpu, vic, virq);
}

error_t
vcpu_unbind_virq(thread_t *vcpu, vcpu_virq_type_t virq_type)
{
	return trigger_vcpu_unbind_virq_event(virq_type, vcpu);
}

error_t
vcpu_handle_vcpu_bind_virq(thread_t *vcpu, vic_t *vic, virq_t virq)
{
	error_t err = vic_bind_shared(&vcpu->vcpu_halt_virq_src, vic, virq,
				      VIRQ_TRIGGER_VCPU_HALT);

	return err;
}

error_t
vcpu_handle_vcpu_unbind_virq(thread_t *vcpu)
{
	vic_unbind_sync(&vcpu->vcpu_halt_virq_src);

	return OK;
}

irq_trigger_result_t
vcpu_handle_virq_set_mode(void)
{
	return irq_trigger_result_ok(IRQ_TRIGGER_EDGE_RISING);
}
