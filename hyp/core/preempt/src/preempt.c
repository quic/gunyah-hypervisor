// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Preemption control functions for preemptible configurations.

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <irq.h>
#include <preempt.h>
#include <scheduler.h>
#include <trace.h>
#include <util.h>

#include <events/preempt.h>

#include <asm/interrupt.h>

#include "event_handlers.h"

static _Thread_local count_t preempt_disable_count;

static const count_t preempt_count_mask =
	util_bit(PREEMPT_BITS_COUNT_MAX + 1) - 1;
static const count_t preempt_count_max	  = preempt_count_mask;
static const count_t preempt_cpu_init	  = util_bit(PREEMPT_BITS_CPU_INIT);
static const count_t preempt_in_interrupt = util_bit(PREEMPT_BITS_IN_INTERRUPT);
static const count_t preempt_abort_kernel = util_bit(PREEMPT_BITS_ABORT_KERNEL);

void
preempt_handle_boot_cpu_early_init(void) LOCK_IMPL
{
	// Prevent an accidental preempt-enable during the boot sequence.
	preempt_disable_count |= preempt_cpu_init;
}

void
preempt_handle_boot_cpu_start(void) LOCK_IMPL
{
	// Boot has finished; allow preemption to be enabled.
	assert((preempt_disable_count & preempt_cpu_init) != 0U);
	preempt_disable_count &= ~preempt_cpu_init;
}

void
preempt_handle_thread_start() LOCK_IMPL
{
	// Arrange for preemption to be enabled by the first
	// preempt_enable() call.
	//
	// Note that preempt_disable_count is briefly 0 in each newly
	// started thread even though preemption is always disabled
	// across context switches. To avoid problems we must ensure
	// that this setup is done as early as possible in new threads,
	// before anything that might call preempt_disable().
	preempt_disable_count = 1U;
}

void
preempt_handle_thread_entry_from_user(thread_entry_reason_t reason)
{
	assert(preempt_disable_count == 1U);

	if (reason == THREAD_ENTRY_REASON_INTERRUPT) {
		preempt_disable_count |= preempt_in_interrupt;
	}

	preempt_enable();
	assert_preempt_enabled();
}

void
preempt_handle_thread_exit_to_user(thread_entry_reason_t reason)
{
	assert_preempt_enabled();
	preempt_disable();

	if (reason == THREAD_ENTRY_REASON_INTERRUPT) {
		preempt_disable_count &= ~preempt_in_interrupt;
	}

	assert(preempt_disable_count == 1U);
}

void
preempt_disable(void) LOCK_IMPL
{
	assert((preempt_disable_count & preempt_count_mask) <
	       preempt_count_max);
	asm_interrupt_disable_acquire(&preempt_disable_count);
	preempt_disable_count++;
	assert_preempt_disabled();
}

void
preempt_enable(void) LOCK_IMPL
{
	assert((preempt_disable_count & preempt_count_mask) > 0U);
	preempt_disable_count--;
	if (preempt_disable_count == 0U) {
		asm_interrupt_enable_release(&preempt_disable_count);
	}
}

bool
preempt_interrupt_dispatch(void) LOCK_IMPL
{
	preempt_disable_count |= preempt_in_interrupt;

	if (!trigger_preempt_interrupt_event() && irq_interrupt_dispatch()) {
		if ((preempt_disable_count & preempt_count_mask) > 0U) {
			// Preemption is disabled; we are in some context that
			// needs to enable interrupts but can't permit a context
			// switch, e.g. the idle loop. Trigger a deferred
			// reschedule.
			scheduler_trigger();
		} else {
			scheduler_schedule();
		}
	}

	preempt_disable_count &= ~preempt_in_interrupt;

	return false;
}

void
preempt_disable_in_irq(void) LOCK_IMPL
{
	assert((preempt_disable_count & preempt_in_interrupt) != 0U);
}

void
preempt_enable_in_irq(void) LOCK_IMPL
{
	assert((preempt_disable_count & preempt_in_interrupt) != 0U);
}

bool
preempt_abort_dispatch(void)
{
	preempt_disable_count |= preempt_in_interrupt;

	bool ret = trigger_preempt_abort_event();

	preempt_disable_count &= ~preempt_in_interrupt;

	return ret;
}

void
preempt_handle_scheduler_stop(void)
{
	count_t old_count = preempt_disable_count;
	asm_interrupt_disable_acquire(&preempt_disable_count);

	// Set the abort bit and clear the current count, to avoid an unbounded
	// recursion in case preempt_disable() fails the count overflow
	// assertion and the abort path calls preempt_disable() again.
	preempt_disable_count = preempt_abort_kernel;

	// Log the original preempt count.
	TRACE(DEBUG, INFO, "preempt: force disabled; previous count was {:#x}",
	      old_count);
}

void
assert_preempt_disabled(void)
{
	assert(preempt_disable_count != 0U);
}

void
assert_preempt_enabled(void)
{
	assert((preempt_disable_count & preempt_count_mask) == 0U);
}
