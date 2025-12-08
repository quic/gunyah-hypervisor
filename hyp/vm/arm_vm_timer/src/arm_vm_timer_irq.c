// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <panic.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>
#include <virq.h>

#include "arm_vm_timer.h"
#include "event_handlers.h"

static void
arm_vm_timer_inject_timer_virq(thread_t *thread, arm_vm_timer_type_t tt)
{
	if (tt == ARM_VM_TIMER_TYPE_VIRTUAL) {
		(void)virq_assert(&thread->virtual_timer_virq_src, false);
	} else if (tt == ARM_VM_TIMER_TYPE_PHYSICAL) {
		(void)virq_assert(&thread->physical_timer_virq_src, false);
	} else {
		panic("Invalid timer");
	}
}

// Handle the timer queue expiry coming from the hyp timer queue
bool
arm_vm_timer_handle_timer_action(timer_action_t action_type, timer_t *timer)
{
	thread_t *vcpu;

	if (action_type == TIMER_ACTION_VIRTUAL_TIMER) {
		vcpu = thread_container_of_virtual_timer(timer);
	} else if (action_type == TIMER_ACTION_PHYSICAL_TIMER) {
		vcpu = thread_container_of_physical_timer(timer);
	} else {
		panic("Invalid timer");
	}

	scheduler_lock_nopreempt(vcpu);
	vcpu_wakeup(vcpu);
	scheduler_unlock_nopreempt(vcpu);

	return true;
}

// Handle the VM arch timer expiry
static bool
arm_vm_timer_type_irq_received(thread_t *thread, arm_vm_timer_type_t tt)
	REQUIRE_PREEMPT_DISABLED
{
	bool injected = false;

	if (arm_vm_timer_is_irq_pending(tt)) {
		arm_vm_timer_arch_timer_hw_irq_activated(tt);
		arm_vm_timer_inject_timer_virq(thread, tt);
		injected = true;
	} else {
		TRACE(DEBUG, INFO, "Spurious VM timer IRQ");
	}

	return injected;
}

bool
arm_vm_timer_handle_irq_received(irq_t irq)
{
	bool	  injected = false;
	thread_t *thread   = thread_get_self();

	if (irq == PLATFORM_VM_ARCH_VIRTUAL_TIMER_IRQ) {
		injected = arm_vm_timer_type_irq_received(
			thread, ARM_VM_TIMER_TYPE_VIRTUAL);
	} else if (irq == PLATFORM_VM_ARCH_PHYSICAL_TIMER_IRQ) {
		injected = arm_vm_timer_type_irq_received(
			thread, ARM_VM_TIMER_TYPE_PHYSICAL);
	} else {
		panic("Invalid VM timer IRQ");
	}

	return !injected;
}

static bool
arm_vm_timer_virq_check_pending(thread_t *thread, arm_vm_timer_type_t tt)
	REQUIRE_PREEMPT_DISABLED
{
	bool ret = true;

	if (thread == thread_get_self()) {
		ret = arm_vm_timer_is_irq_pending(tt);

		if (!ret) {
			arm_vm_timer_arch_timer_hw_irq_deactivate(tt);
		}
	}

	return ret;
}

error_t
arm_vm_timer_handle_vcpu_set_virtual_timer_virq(thread_t *vcpu, virq_t virq)
{
	assert(vcpu != NULL);

	vcpu->virtual_timer_virq = virq;

	return OK;
}

error_t
arm_vm_timer_handle_vcpu_set_physical_timer_virq(thread_t *vcpu, virq_t virq)
{
	assert(vcpu != NULL);

	vcpu->physical_timer_virq = virq;

	return OK;
}

bool
arm_vm_timer_handle_virq_check_pending(virq_trigger_t trigger,
				       virq_source_t *source)
{
	bool ret = true;

	if (trigger == VIRQ_TRIGGER_VIRTUAL_TIMER) {
		ret = arm_vm_timer_virq_check_pending(
			thread_container_of_virtual_timer_virq_src(source),
			ARM_VM_TIMER_TYPE_VIRTUAL);

	} else if (trigger == VIRQ_TRIGGER_PHYSICAL_TIMER) {
		ret = arm_vm_timer_virq_check_pending(
			thread_container_of_physical_timer_virq_src(source),
			ARM_VM_TIMER_TYPE_PHYSICAL);
	} else {
		/* Do Nothing */
	}

	return ret;
}
