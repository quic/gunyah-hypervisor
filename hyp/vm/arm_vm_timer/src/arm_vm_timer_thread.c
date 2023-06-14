// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
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
		timer_init_object(&thread->virtual_timer,
				  TIMER_ACTION_VIRTUAL_TIMER);
		timer_init_object(&thread->physical_timer,
				  TIMER_ACTION_PHYSICAL_TIMER);
	}

	return OK;
}

error_t
arm_vm_timer_handle_object_activate_thread(thread_t *thread)
{
	error_t ret = OK;

	if (thread->kind == THREAD_KIND_VCPU) {
		ret = vic_bind_private_vcpu(&thread->virtual_timer_virq_src,
					    thread,
					    PLATFORM_VM_ARCH_VIRTUAL_TIMER_IRQ,
					    VIRQ_TRIGGER_VIRTUAL_TIMER);
		if (ret == OK) {
			ret = vic_bind_private_vcpu(
				&thread->physical_timer_virq_src, thread,
				PLATFORM_VM_ARCH_PHYSICAL_TIMER_IRQ,
				VIRQ_TRIGGER_PHYSICAL_TIMER);

			if (ret != OK) {
				vic_unbind(&thread->virtual_timer_virq_src);
			}
		}
	}

	return ret;
}

void
arm_vm_timer_handle_object_deactivate_thread(thread_t *thread)
{
	if (thread->kind == THREAD_KIND_VCPU) {
		vic_unbind(&thread->virtual_timer_virq_src);
		timer_dequeue(&thread->virtual_timer);

		vic_unbind(&thread->physical_timer_virq_src);
		timer_dequeue(&thread->physical_timer);
	}
}

error_t
arm_vm_timer_handle_thread_context_switch_pre(void)
{
	thread_t *thread = thread_get_self();

	// Enqueue thread's timeout if it is enabled, not already queued, and is
	// capable of waking the VCPU
	if ((compiler_expected(thread->kind == THREAD_KIND_VCPU) &&
	     vcpu_expects_wakeup(thread))) {
		if (arm_vm_timer_is_irq_enabled_thread(
			    thread, ARM_VM_TIMER_TYPE_VIRTUAL)) {
			timer_update(&thread->virtual_timer,
				     arm_vm_timer_get_timeout_thread(
					     thread,
					     ARM_VM_TIMER_TYPE_VIRTUAL));
		}

		if (arm_vm_timer_is_irq_enabled_thread(
			    thread, ARM_VM_TIMER_TYPE_PHYSICAL)) {
			timer_update(&thread->physical_timer,
				     arm_vm_timer_get_timeout_thread(
					     thread,
					     ARM_VM_TIMER_TYPE_PHYSICAL));
		}
	}

	return OK;
}

void
arm_vm_timer_handle_thread_context_switch_post(void)
{
	thread_t *thread = thread_get_self();
	if (compiler_expected(thread->kind == THREAD_KIND_VCPU)) {
		arm_vm_timer_load_state(thread);

		bool_result_t asserted;

		asserted = virq_query(&thread->virtual_timer_virq_src);
		if ((asserted.e == OK) && !asserted.r) {
			arm_vm_timer_arch_timer_hw_irq_deactivate(
				ARM_VM_TIMER_TYPE_VIRTUAL);
		}

		asserted = virq_query(&thread->physical_timer_virq_src);
		if ((asserted.e == OK) && !asserted.r) {
			arm_vm_timer_arch_timer_hw_irq_deactivate(
				ARM_VM_TIMER_TYPE_PHYSICAL);
		}
	} else {
		// Disable the timer and its IRQ
		arm_vm_timer_cancel_timeout(ARM_VM_TIMER_TYPE_VIRTUAL);
		arm_vm_timer_cancel_timeout(ARM_VM_TIMER_TYPE_PHYSICAL);
	}
}

void
arm_vm_timer_handle_vcpu_stopped(void)
{
	thread_t *thread = thread_get_self();

	// Disable the timer and its IRQ, so that context switch will not
	// lead us to enqueue an EL2 timer for a VCPU that can't be woken.
	arm_vm_timer_cancel_timeout(ARM_VM_TIMER_TYPE_VIRTUAL);
	arm_vm_timer_cancel_timeout(ARM_VM_TIMER_TYPE_PHYSICAL);

	// Ensure that the EL2 timer has not been lazily left queued.
	timer_dequeue(&thread->virtual_timer);
	timer_dequeue(&thread->physical_timer);
}

error_t
arm_vm_timer_handle_vcpu_suspend(void)
{
	thread_t *thread = thread_get_self();

	// Ensure that the EL2 timer has not been lazily left queued.
	if (timer_is_queued(&thread->virtual_timer) &&
	    !arm_vm_timer_is_irq_enabled(ARM_VM_TIMER_TYPE_VIRTUAL)) {
		timer_dequeue(&thread->virtual_timer);
	}

	if (timer_is_queued(&thread->physical_timer) &&
	    !arm_vm_timer_is_irq_enabled(ARM_VM_TIMER_TYPE_PHYSICAL)) {
		timer_dequeue(&thread->physical_timer);
	}

	return OK;
}
