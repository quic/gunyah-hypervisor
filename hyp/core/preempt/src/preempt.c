// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Preemption control functions for preemptible configurations.

#include <assert.h>
#include <hyptypes.h>

#include <irq.h>
#include <preempt.h>
#include <scheduler.h>
#include <util.h>

#include <events/preempt.h>

#include <asm/interrupt.h>

#include "event_handlers.h"

static _Thread_local count_t preempt_disable_count;

enum preempt_bits {
	preempt_count_max_bit = 15,
	preempt_cpu_init_bit,
	preempt_in_interrupt_bit,
};

static const count_t preempt_count_mask =
	util_bit(preempt_count_max_bit + 1) - 1;
static const count_t preempt_count_max	  = preempt_count_mask;
static const count_t preempt_cpu_init	  = util_bit(preempt_cpu_init_bit);
static const count_t preempt_in_interrupt = util_bit(preempt_in_interrupt_bit);

void
preempt_handle_boot_cpu_early_init(void)
{
	// Prevent an accidental preempt-enable during the boot sequence.
	preempt_disable_count |= preempt_cpu_init;
}

void
preempt_handle_boot_cpu_start(void)
{
	// Boot has finished; allow preemption to be enabled.
	assert((preempt_disable_count & preempt_cpu_init) != 0U);
	preempt_disable_count &= ~preempt_cpu_init;
}

void
preempt_handle_thread_start()
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
preempt_disable(void)
{
	assert((preempt_disable_count & preempt_count_mask) <
	       preempt_count_max);
	asm_interrupt_disable_acquire(&preempt_disable_count);
	preempt_disable_count++;
	assert_preempt_disabled();
}

void
preempt_enable(void)
{
	assert_preempt_disabled();
	preempt_disable_count--;
	if (preempt_disable_count == 0U) {
		asm_interrupt_enable_release(&preempt_disable_count);
	}
}

bool
preempt_interrupt_dispatch(void)
{
	preempt_disable_count |= preempt_in_interrupt;

	if (!trigger_preempt_interrupt_event()) {
		if (irq_interrupt_dispatch()) {
			scheduler_schedule();
		}
	}

	preempt_disable_count &= ~preempt_in_interrupt;

	return false;
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
assert_preempt_disabled(void)
{
	assert(preempt_disable_count != 0U);
}

void
assert_preempt_enabled(void)
{
	assert((preempt_disable_count & preempt_count_mask) == 0U);
}
