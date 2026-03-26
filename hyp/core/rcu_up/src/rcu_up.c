// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <atomic.h>
#include <cpulocal.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>

#include <events/rcu.h>

#include "event_handlers.h"

// Simple RCU for uniprocessor configurations.
//
// This just defers updates to the next context switch.

// These are atomic so we can avoid disabling preemption during enqueue.
static rcu_entry_t *_Atomic rcu_heads[(int)RCU_UPDATE_CLASS__MAX + 1];
static _Atomic bool rcu_in_grace_period;

void
rcu_read_start(void) LOCK_IMPL
{
	preempt_disable();
	trigger_rcu_read_start_event();
}

void
rcu_read_finish(void) LOCK_IMPL
{
	trigger_rcu_read_finish_event();
	preempt_enable();
}

void
rcu_enqueue(rcu_entry_t *rcu_entry, rcu_update_class_t rcu_update_class)
{
	rcu_entry->next = atomic_load_relaxed(&rcu_heads[rcu_update_class]);
	while (!atomic_compare_exchange_weak_explicit(
		&rcu_heads[rcu_update_class], &rcu_entry->next, rcu_entry,
		memory_order_relaxed, memory_order_relaxed)) {
	}

	// Ensure that the write to rcu_in_grace_period is after the write to
	// the head, so that preemption during this function won't prematurely
	// clear rcu_in_grace_period.
	atomic_signal_fence(memory_order_release);

	atomic_store_relaxed(&rcu_in_grace_period, true);
}

void
rcu_up_handle_scheduler_quiescent(bool *must_schedule)
{
	// Local quiescence always ends the grace period for uniprocessor.
	assert_preempt_disabled();

	if (atomic_load_relaxed(&rcu_in_grace_period)) {
		rcu_update_status_t status = rcu_update_status_default();

		atomic_store_relaxed(&rcu_in_grace_period, false);

		trigger_rcu_grace_period_end_event(cpulocal_get_index());

		for (rcu_update_class_t update_class = RCU_UPDATE_CLASS__MIN;
		     update_class <= RCU_UPDATE_CLASS__MAX; update_class++) {
			rcu_entry_t *entry =
				atomic_load_relaxed(&rcu_heads[update_class]);
			atomic_store_relaxed(&rcu_heads[update_class], NULL);

			while (entry != NULL) {
				// We must read the next pointer _before_
				// triggering the update, in case the update
				// handler frees the object.
				rcu_entry_t *next = entry->next;
				status		  = rcu_update_status_union(
					trigger_rcu_update_event(update_class,
								 entry),
					status);
				entry = next;
			}
		}

		trigger_rcu_grace_period_complete_event(cpulocal_get_index(),
							status);

		*must_schedule |= rcu_update_status_get_need_schedule(&status);
	}
}

void
rcu_expedite(void)
{
	preempt_disable();
	bool must_schedule = false;
	rcu_up_handle_scheduler_quiescent(&must_schedule);
	if (must_schedule) {
		scheduler_yield();
	}
	preempt_enable();
}
