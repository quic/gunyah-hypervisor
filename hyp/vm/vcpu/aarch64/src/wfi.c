// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypregisters.h>

#include <compiler.h>
#include <idle.h>
#include <log.h>
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
	vcpu_runtime_flags_set_vcpu_can_idle(&thread->vcpu_flags, *can_idle);
}

vcpu_trap_result_t
vcpu_handle_vcpu_trap_wfi(ESR_EL2_ISS_WFI_WFE_t iss)
{
	vcpu_trap_result_t ret = VCPU_TRAP_RESULT_EMULATED;

	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

	assert_preempt_enabled();
	preempt_disable();

#if defined(ARCH_ARM_FEAT_WFxT)
	// Inject a trap to the guest if it uses the WFIT instruction
	// without checking their availability in the ID registers first.
	// Remove once support for FEAT_WFxT is added to the hypervisor.
	// FIXME:
	if (ESR_EL2_ISS_WFI_WFE_get_TI(&iss) == ISS_WFX_TI_WFIT) {
		TRACE_AND_LOG(ERROR, WARN, "WFIT trap from thread {:#x}",
			      (register_t)current);
		ret = VCPU_TRAP_RESULT_FAULT;
		goto out;
	}
#else
	(void)iss;
#endif

#if !defined(PREEMPT_NULL)
	bool vcpu_interrupted =
		vcpu_runtime_flags_get_vcpu_interrupted(&current->vcpu_flags);
#if !defined(VCPU_IDLE_IN_EL1) || !VCPU_IDLE_IN_EL1
	if (vcpu_runtime_flags_get_vcpu_can_idle(&current->vcpu_flags) &&
	    !vcpu_interrupted) {
		if (vcpu_block_start()) {
			goto out;
		}

		bool need_schedule;
		do {
			need_schedule = idle_yield();
			// We may have received a wakeup while idle, so recheck
			// the interrupted flag.
			vcpu_interrupted =
				vcpu_runtime_flags_get_vcpu_interrupted(
					&current->vcpu_flags);
		} while (!need_schedule && !vcpu_interrupted);

		vcpu_block_finish();

		// We only need to reschedule here if the VCPU was interrupted;
		// otherwise the reschedule is handled by the yield below after
		// setting the WFI block flag.
		if (need_schedule && vcpu_interrupted) {
			scheduler_schedule();
		}
	}
#endif // !VCPU_IDLE_IN_EL1
       // Check whether we were woken by an interrupt after the WFI trap was
       // taken. This could have been done either by a preemption before the
       // preempt_disable() above, or by an IPI during the idle_yield() in the
       // WFI fastpath (if it is enabled).
	if (vcpu_interrupted) {
		goto out;
	}
#endif // !PREEMPT_NULL

	scheduler_lock_nopreempt(current);
	scheduler_block(current, SCHEDULER_BLOCK_VCPU_WFI);
	scheduler_unlock_nopreempt(current);

	(void)scheduler_yield();

out:
	preempt_enable();

	return ret;
}

#if defined(VCPU_IDLE_IN_EL1) && VCPU_IDLE_IN_EL1
void
vcpu_handle_scheduler_quiescent(void)
{
	thread_t *current = thread_get_self();
	if (compiler_expected(current->kind == THREAD_KIND_VCPU)) {
		current->vcpu_regs_el2.hcr_el2 = register_HCR_EL2_read();
		HCR_EL2_set_TWI(&current->vcpu_regs_el2.hcr_el2,
				!vcpu_runtime_flags_get_vcpu_can_idle(
					&current->vcpu_flags));
		register_HCR_EL2_write(current->vcpu_regs_el2.hcr_el2);
	}
}

void
vcpu_handle_thread_context_switch_post(void)
{
	thread_t *current = thread_get_self();
	HCR_EL2_set_TWI(
		&current->vcpu_regs_el2.hcr_el2,
		!vcpu_runtime_flags_get_vcpu_can_idle(&current->vcpu_flags));
}
#endif // VCPU_IDLE_IN_EL1

void
vcpu_wakeup(thread_t *vcpu)
{
	assert(vcpu != NULL);
	assert(vcpu->kind == THREAD_KIND_VCPU);

#if !defined(PREEMPT_NULL)
	if (vcpu == thread_get_self()) {
		// Inhibit sleep in preempted WFI handlers (see above)
		vcpu_runtime_flags_set_vcpu_interrupted(&vcpu->vcpu_flags,
							true);
	}
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
	vcpu_runtime_flags_set_vcpu_interrupted(&current->vcpu_flags, true);
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

#if defined(MODULE_VM_VCPU_RUN)
vcpu_run_state_t
vcpu_arch_handle_vcpu_run_check(const thread_t *thread,
				register_t     *state_data_0,
				register_t     *state_data_1)
{
	vcpu_run_state_t state = VCPU_RUN_STATE_BLOCKED;
	if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_VCPU_WFI)) {
		state	      = VCPU_RUN_STATE_EXPECTS_WAKEUP;
		*state_data_0 = 0U;
		*state_data_1 = (register_t)VCPU_RUN_WAKEUP_FROM_STATE_WFI;
	}
	return state;
}
#endif

bool
vcpu_pending_wakeup(void)
{
	thread_t *current = thread_get_self();
	assert(current->kind == THREAD_KIND_VCPU);

#if defined(PREEMPT_NULL)
	return trigger_vcpu_pending_wakeup_event();
#else
	return vcpu_runtime_flags_get_vcpu_interrupted(&current->vcpu_flags) ||
	       trigger_vcpu_pending_wakeup_event();
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
	vcpu_runtime_flags_set_vcpu_interrupted(&current->vcpu_flags, false);
#endif
}
