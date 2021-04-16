// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <irq.h>
#include <timer_queue.h>
#include <vcpu.h>
#include <vic.h>
#include <virq.h>

#include <asm/barrier.h>

#include "arm_vm_timer.h"
#include "event_handlers.h"

error_t
arm_vm_timer_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_VCPU) {
		timer_init_object(&thread->timer, TIMER_ACTION_VIRTUAL_TIMER);
	}

	return OK;
}

error_t
arm_vm_timer_handle_object_activate_thread(thread_t *thread)
{
	error_t ret = OK;

	if (thread->kind == THREAD_KIND_VCPU) {
		ret = vic_bind_private_vcpu(&thread->timer_virq_src, thread,
					    PLATFORM_VM_ARCH_TIMER_IRQ,
					    VIRQ_TRIGGER_TIMER);
	}

	return ret;
}

void
arm_vm_timer_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->kind == THREAD_KIND_VCPU) {
		vic_unbind(&thread->timer_virq_src);
		timer_dequeue(&thread->timer);
	}
}

error_t
arm_vm_timer_handle_thread_context_switch_pre(void)
{
	thread_t *thread = thread_get_self();

	// Enqueue thread's timeout if it is enabled, not already queued, and is
	// capable of waking the VCPU
	if ((thread->kind == THREAD_KIND_VCPU) && vcpu_expects_wakeup(thread) &&
	    arm_vm_timer_is_irq_enabled_thread(thread)) {
		timer_update(&thread->timer,
			     arm_vm_timer_get_timeout_thread(thread));
	}

	return OK;
}

void
arm_vm_timer_handle_thread_context_switch_post(void)
{
	thread_t *thread = thread_get_self();
	if (thread->kind == THREAD_KIND_VCPU) {
		arm_vm_timer_load_state(thread);

		bool_result_t asserted = virq_query(&thread->timer_virq_src);
		if ((asserted.e == OK) && !asserted.r) {
			arm_vm_timer_arch_timer_hw_irq_deactivate();
		}
	} else {
		// Disable the timer and its IRQ
		arm_vm_timer_cancel_timeout();
	}
}

error_t
arm_vm_timer_handle_vcpu_poweroff(void)
{
	thread_t *thread = thread_get_self();

	// Disable the timer and its IRQ, so that context switch will not
	// lead us to enqueue an EL2 timer for a VCPU that can't be woken.
	arm_vm_timer_cancel_timeout();

	// Ensure that the EL2 timer has not been lazily left queued.
	timer_dequeue(&thread->timer);

	return OK;
}

error_t
arm_vm_timer_handle_vcpu_suspend(void)
{
	thread_t *thread = thread_get_self();

	// Ensure that the EL2 timer has not been lazily left queued.
	if (timer_is_queued(&thread->timer) && !arm_vm_timer_is_irq_enabled()) {
		timer_dequeue(&thread->timer);
	}

	return OK;
}
