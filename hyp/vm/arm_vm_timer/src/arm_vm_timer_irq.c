// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <scheduler.h>
#include <trace.h>
#include <virq.h>

#include "arm_vm_timer.h"
#include "event_handlers.h"

static void
arm_vm_timer_inject_timer_virq(thread_t *thread)
{
	(void)virq_assert(&thread->timer_virq_src, false);
}

// Handle the timer queue expiry coming from the hyp arch timer
bool
arm_vm_timer_handle_timer_action(timer_t *timer)
{
	thread_t *thread     = thread_container_of_timer(timer);
	bool	  is_current = thread_get_self() == thread;

	if (is_current && arm_vm_timer_is_irq_pending()) {
		arm_vm_timer_inject_timer_virq(thread);
	} else if (!is_current && arm_vm_timer_is_irq_enabled_thread(thread)) {
		arm_vm_timer_inject_timer_virq(thread);
	} else {
		TRACE(DEBUG, INFO, "spurious VM hyp timeout");
	}

	return true;
}

// Handle the VM arch timer expiry
bool
arm_vm_timer_handle_irq_received(void)
{
	bool	  injected = false;
	thread_t *thread   = thread_get_self();

	if (arm_vm_timer_is_irq_pending()) {
		arm_vm_timer_inject_timer_virq(thread);
		arm_vm_timer_arch_timer_hw_irq_activated();
		injected = true;
	} else {
		TRACE(DEBUG, INFO, "spurious VM timer IRQ");
	}

	return !injected;
}

bool
arm_vm_timer_handle_virq_check_pending(virq_source_t *source)
{
	bool ret = true;

	thread_t *vcpu = atomic_load_relaxed(&source->vgic_vcpu);
	if (vcpu == thread_get_self()) {
		ret = arm_vm_timer_is_irq_pending();

		if (!ret) {
			arm_vm_timer_arch_timer_hw_irq_deactivate();
		}
	}

	return ret;
}
