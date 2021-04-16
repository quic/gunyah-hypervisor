// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <idle.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <vcpu.h>

#include <events/vcpu.h>

#include "event_handlers.h"

vcpu_trap_result_t
vcpu_handle_vcpu_trap_wfi(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	assert_preempt_enabled();
	preempt_disable();

#if !defined(PREEMPT_NULL)
	// It is possible for a virtual IRQ to be asserted by preemption between
	// the WFI trap and the preempt_disable() above. The vcpu_wakeup()
	// function will set a flag if that happens.
	if (compiler_unexpected(current->vcpu_interrupted)) {
		// The delivered IRQ may or may not assert the virtual interrupt
		// bit that would wake this WFI, depending on its priority and
		// the current GICV state. Unfortunately there is no efficient
		// way to query the CPU or GICH to find out whether we really
		// need to wake up, so we must always wake in this case.
		//
		// Note that the ARMv8 spec permits WFI to spuriously wake, so
		// the guest must be able to cope with this.
		goto out;
	}

	idle_state_t state = trigger_vcpu_idle_fastpath_event();

	if (current->vcpu_interrupted && (state == IDLE_STATE_RESCHEDULE)) {
		scheduler_schedule();
	}

	if (current->vcpu_interrupted || (state == IDLE_STATE_WAKEUP)) {
		goto out;
	}
#endif

	scheduler_lock(current);
	scheduler_block(current, SCHEDULER_BLOCK_VCPU_WFI);
	scheduler_unlock(current);

	(void)scheduler_yield();

#if !defined(PREEMPT_NULL)
out:
#endif

	preempt_enable();

	return VCPU_TRAP_RESULT_EMULATED;
}

void
vcpu_wakeup(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);

#if !defined(PREEMPT_NULL)
	// Inhibit sleep in preempted WFI handlers (see above)
	vcpu->vcpu_interrupted = true;
#endif

	trigger_vcpu_wakeup_event(vcpu);

	if (scheduler_unblock(vcpu, SCHEDULER_BLOCK_VCPU_WFI)) {
		scheduler_trigger();
	}
}

void
vcpu_wakeup_self(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

#if !defined(PREEMPT_NULL)
	// Inhibit sleep in preempted WFI handlers (see above)
	current->vcpu_interrupted = true;
#endif

	trigger_vcpu_wakeup_self_event();
}

bool
vcpu_expects_wakeup(const thread_t *thread)
{
	assert(thread->kind == THREAD_KIND_VCPU);

	return scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_WFI) ||
	       trigger_vcpu_expects_wakeup_event(thread);
}

bool
vcpu_pending_wakeup(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	return current->vcpu_interrupted || trigger_vcpu_pending_wakeup_event();
}

void
vcpu_handle_thread_exit_to_user(void)
{
#if !defined(PREEMPT_NULL)
	thread_t *current = thread_get_self();

	// Don't inhibit sleep in new WFI traps
	current->vcpu_interrupted = false;
#endif
}
