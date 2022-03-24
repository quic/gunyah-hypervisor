// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <idle.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <vcpu.h>

#include <events/vcpu.h>

#include "event_handlers.h"

void
vcpu_handle_scheduler_selected_thread(thread_t *thread, bool *can_idle)
{
	assert(thread != NULL);

	// If idle in EL1 is allowed, this is used during context switch to
	// decide whether to enable the WFI trap; otherwise it is used in the
	// WFI trap handler to decide whether to call idle_yield() without
	// scheduling.
	thread->vcpu_can_idle = *can_idle;
}

vcpu_trap_result_t
vcpu_handle_vcpu_trap_wfi(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	assert_preempt_enabled();
	preempt_disable();

#if !defined(PREEMPT_NULL)
#if !defined(VCPU_IDLE_IN_EL1) || !VCPU_IDLE_IN_EL1
	if (current->vcpu_can_idle && !current->vcpu_interrupted) {
		if (vcpu_block_start()) {
			goto out;
		}

		bool need_schedule;
		do {
			need_schedule = idle_yield();
		} while (!need_schedule && current->vcpu_can_idle &&
			 !current->vcpu_interrupted);

		vcpu_block_finish();

		// If this thread and another thread are woken concurrently, we
		// need to reschedule with no yield hint before we return. If
		// we haven't been woken, the need_reschedule is handled by the
		// yield below after setting the WFI block flag.
		if ((need_schedule || !current->vcpu_can_idle) &&
		    current->vcpu_interrupted) {
			scheduler_schedule();
		}
	}
#endif // !VCPU_IDLE_IN_EL1
       // Check whether we were woken by an interrupt after the WFI trap was
       // taken. This could have been done either by a preemption before the
       // preempt_disable() above, or by an IPI during the idle_yield() in the
       // WFI fastpath (if it is enabled).
	if (current->vcpu_interrupted) {
		goto out;
	}
#endif // !PREEMPT_NULL

	scheduler_lock_nopreempt(current);
	scheduler_block(current, SCHEDULER_BLOCK_VCPU_WFI);
	scheduler_unlock_nopreempt(current);

	(void)scheduler_yield();

out:
	preempt_enable();

	return VCPU_TRAP_RESULT_EMULATED;
}

#if defined(VCPU_IDLE_IN_EL1) && VCPU_IDLE_IN_EL1
void
vcpu_handle_scheduler_quiescent(void)
{
	thread_t *current = thread_get_self();
	if (current->kind == THREAD_KIND_VCPU) {
		current->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		HCR_EL2_set_TWI(&current->vcpu_regs_el2.hcr_el2,
				!current->vcpu_can_idle);
		register_HCR_EL2_write(current->vcpu_regs_el2.hcr_el2);
	}
}

void
vcpu_handle_thread_context_switch_post(void)
{
	thread_t *current = thread_get_self();
	HCR_EL2_set_TWI(&current->vcpu_regs_el2.hcr_el2,
			!current->vcpu_can_idle);
}
#endif // VCPU_IDLE_IN_EL1

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

#if defined(PREEMPT_NULL)
	return trigger_vcpu_pending_wakeup_event();
#else
	return current->vcpu_interrupted || trigger_vcpu_pending_wakeup_event();
#endif
}

bool
vcpu_block_start(void)
{
	thread_t *current = thread_get_self();
	bool	  pending = vcpu_pending_wakeup();

	TRACE_LOCAL(DEBUG, INFO, "vcpu: {:#x} block start", (uintptr_t)current);

	if (!pending) {
		pending = trigger_vcpu_block_start_event();
	}

	if (compiler_unexpected(pending)) {
		TRACE_LOCAL(DEBUG, INFO, "vcpu: {:#x} block aborted, pending",
			    (uintptr_t)current);
	}
	return pending;
}

void
vcpu_block_finish(void)
{
	trigger_vcpu_block_finish_event();
	thread_t *current = thread_get_self();
	TRACE_LOCAL(DEBUG, INFO, "vcpu: {:#x} block finish",
		    (uintptr_t)current);
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
