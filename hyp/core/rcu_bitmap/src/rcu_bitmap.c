// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <enum.h>
#include <idle.h>
#include <ipi.h>
#include <misc.h>
#include <platform_cpu.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <util.h>

#include <events/rcu.h>

#include "event_handlers.h"

static_assert(PLATFORM_MAX_CORES <= 32U, "PLATFORM_MAX_CORES > 32");

static rcu_state_t rcu_state;
CPULOCAL_DECLARE_STATIC(rcu_cpu_state_t, rcu_state);

void
rcu_read_start(void) LOCK_IMPL
{
	preempt_disable();
	assert_debug(CPULOCAL(rcu_state).is_active);
	trigger_rcu_read_start_event();
}

void
rcu_read_finish(void) LOCK_IMPL
{
	trigger_rcu_read_finish_event();
	preempt_enable();
}

static void
rcu_bitmap_refresh_active(cpu_index_t caller) REQUIRE_PREEMPT_DISABLED
{
	uint32_t active_cpus = atomic_load_relaxed(&rcu_state.active_cpus);

	// The calling CPU is truly active, so there is no need to refresh it.
	active_cpus &= ~(uint32_t)util_bit(caller);

	while (active_cpus != 0U) {
		cpu_index_t cpu = (cpu_index_t)compiler_ctz(active_cpus);
		// Ask the CPU to quiesce. This will also deactivate the CPU
		// when it exits or blocks the hypervisor afterwards.
		ipi_one(IPI_REASON_RCU_QUIESCE, cpu);
		active_cpus &= ~(uint32_t)util_bit(cpu);
	}
}

static inline bool
rcu_bitmap_should_run(void)
{
	bool should_run = compiler_unexpected(
		atomic_load_relaxed(&rcu_state.waiter_count) > 0U);
	if (should_run) {
		atomic_thread_fence(memory_order_acquire);
	}
	return should_run;
}

void
rcu_enqueue(rcu_entry_t *rcu_entry, rcu_update_class_t rcu_update_class)
{
	preempt_disable();

	cpu_index_t	 cpu	  = cpulocal_get_index();
	rcu_cpu_state_t *my_state = &CPULOCAL_BY_INDEX(rcu_state, cpu);
	rcu_batch_t	*batch	  = &my_state->next_batch;

	if (atomic_fetch_add_explicit(&my_state->update_count, 1U,
				      memory_order_relaxed) == 0U) {
		if (compiler_unexpected(atomic_fetch_add_explicit(
						&rcu_state.waiter_count, 1U,
						memory_order_relaxed) == 0U)) {
			// Ensure that we only read the set of active CPUs after
			// RCU has been marked globally enabled, so that any
			// remote rcu_bitmap_maybe_deactivate_cpu() either sees
			// RCU as active and deactivates itself, or else has
			// already marked itself active so we refresh it below.
			// This must be a seq_cst fence to order the above store
			// before the load of the active set. It matches the
			// fence in rcu_bitmap_activate_cpu().
			atomic_thread_fence(memory_order_seq_cst);

			// CPUs may have stopped tracking quiescent states
			// because there were no waiters, so prod them all.
			//
			// Note that any CPU sitting in idle or running in
			// a lower EL will take itself out of both the current
			// and active sets in response to this, allowing us
			// to ignore it until it starts doing something.
			rcu_bitmap_refresh_active(cpu);
		}
	}

	rcu_entry->next		       = batch->heads[rcu_update_class];
	batch->heads[rcu_update_class] = rcu_entry;

	// Trigger a relaxed IPI to request a new GP if possible. We could call
	// rcu_bitmap_notify() directly here, but using an IPI to defer it will
	// improve batching when there is no GP already in progress.
	ipi_one_relaxed(IPI_REASON_RCU_NOTIFY, cpu);

	preempt_enable();
}

void
rcu_expedite(void)
{
	preempt_disable();

	// Our goal is to try to complete any updates that are currently in
	// next_batch, without a context switch. If any earlier RCU update
	// handler forces a reschedule and switches contexts, we have failed to
	// do that, so we bail out afterwards.

	// The rcu_enqueue() that added our target updates to next_batch should
	// have triggered a notify to request a new GP. Handle it now.
	if (ipi_clear(IPI_REASON_RCU_NOTIFY)) {
		if (rcu_bitmap_notify()) {
			if (scheduler_schedule()) {
				goto out;
			}
		}
	}

	// Try to end the GP in which the updates were enqueued. If successful,
	// the updates will move to waiting_batch.
	(void)ipi_clear(IPI_REASON_RCU_QUIESCE);
	if (rcu_bitmap_quiesce()) {
		if (scheduler_schedule()) {
			goto out;
		}
	}

	// If the first GP ended, there will be another quiesce pending. Try to
	// end the GP that the updates are waiting for. If successful, the
	// updates will move to ready_batch.
	if (ipi_clear(IPI_REASON_RCU_QUIESCE)) {
		if (rcu_bitmap_quiesce()) {
			if (scheduler_schedule()) {
				goto out;
			}
		}
	}

	// If we have successfully expedited our updates, they will be in
	// ready_batch now, so try to process them.
	if (ipi_clear(IPI_REASON_RCU_UPDATE)) {
		if (rcu_bitmap_update()) {
			(void)scheduler_schedule();
		}
	}

out:
	preempt_enable();
}

// Events that activate a CPU (i.e. mark it as needing to ack GPs)
//
// Note: this may be called when the CPU is already active, and is expected in
// cases such as the warm-boot handler called during cold boot.
static void
rcu_bitmap_activate_cpu(cpu_index_t cpu_index) REQUIRE_PREEMPT_DISABLED
{
	uint32_t	 cpu_bit  = (uint32_t)util_bit(cpu_index);
	rcu_cpu_state_t *my_state = &CPULOCAL_BY_INDEX(rcu_state, cpu_index);

	if (compiler_unexpected(!my_state->is_active)) {
		// We're not in the active CPU set. Add ourselves.
		my_state->is_active = true;

		(void)atomic_fetch_or_explicit(&rcu_state.active_cpus, cpu_bit,
					       memory_order_relaxed);

		// Ensure that we are in the active CPU set before any critical
		// sections start, so that any new grace period that starts
		// after such accesses will see this CPU as active. This must be
		// a seq_cst fence to order loads in critical sections after the
		// above store. The matching fence is in rcu_bitmap_quiesce(),
		// before active_cpus is read.
		//
		// Also, ensure that we are in the active CPU set before any
		// future deactivation of this CPU checks whether RCU should
		// run, so that we are guaranteed to either see RCU as enabled
		// and deactivate this CPU, or else get an IPI from the next CPU
		// that enables RCU. The matching fence is in rcu_enqueue(),
		// after RCU is enabled.
		atomic_thread_fence(memory_order_seq_cst);
	}
}

void
rcu_bitmap_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	rcu_bitmap_activate_cpu(boot_cpu_index);
}

void
rcu_bitmap_activate_current(void)
{
	assert_cpulocal_safe();
	cpu_index_t cpu = cpulocal_get_index();
	rcu_bitmap_activate_cpu(cpu);
}

bool
rcu_bitmap_handle_preempt_interrupt(void)
{
	rcu_bitmap_activate_current();

	return false;
}

// Events that deactivate a CPU (i.e. mark it as not needing to ack GPs)
static void
rcu_bitmap_deactivate_cpu(void) REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();
	cpu_index_t	 cpu	  = cpulocal_get_index();
	rcu_cpu_state_t *my_state = &CPULOCAL_BY_INDEX(rcu_state, cpu);

	if (compiler_expected(my_state->is_active)) {
		uint32_t cpu_bit    = (uint32_t)util_bit(cpu);
		my_state->is_active = false;

		// Remove ourselves from the active set. Release ordering is
		// needed to ensure that it is done after the end of any
		// critical sections. However, it does not need ordering
		// relative to the quiesce below; if it happens late then at
		// worst we might get a redundant IPI.
		(void)atomic_fetch_and_explicit(&rcu_state.active_cpus,
						~cpu_bit, memory_order_relaxed);

		// This sequential consistency fence matches the one in
		// rcu_bitmap_quiesce when a new grace period starts, to ensure
		// that either this CPU goes first and clears its active bit
		// (and the other CPU sends us a quiesce IPI), or the other CPU
		// goes first and starts the new grace period before the
		// quiesce.
		atomic_thread_fence(memory_order_seq_cst);
	}

	(void)ipi_clear(IPI_REASON_RCU_QUIESCE);
	if (rcu_bitmap_quiesce()) {
		scheduler_trigger();
	}
}

void
rcu_bitmap_maybe_deactivate_cpu(void)
{
	if (compiler_unexpected(rcu_bitmap_should_run())) {
		rcu_bitmap_deactivate_cpu();
	}
}

error_t
rcu_bitmap_try_deactivate_cpu(void)
{
	error_t ret = OK;

	rcu_cpu_state_t *my_state = &CPULOCAL(rcu_state);
	if (atomic_load_relaxed(&my_state->update_count) != 0U) {
		// Delay suspend, we still have pending updates on this CPU.
		ret = ERROR_BUSY;
	} else {
		// Always run update processing, even if there are currently no
		// pending updates. This is to prevent us being woken spuriously
		// later, which is much more expensive than a redundant
		// quiesce().
		rcu_bitmap_deactivate_cpu();
	}

	return ret;
}

// Events that quiesce a CPU but don't activate or deactivate it
error_t
rcu_bitmap_handle_thread_context_switch_pre(void)
{
	rcu_bitmap_handle_scheduler_quiescent();

	return OK;
}

void
rcu_bitmap_handle_scheduler_quiescent(void)
{
	if (compiler_unexpected(rcu_bitmap_should_run())) {
		(void)ipi_clear(IPI_REASON_RCU_QUIESCE);
		if (rcu_bitmap_quiesce()) {
			scheduler_trigger();
		}
	}
}

// Handlers for internal IPIs
bool
rcu_bitmap_quiesce(void)
{
	assert_preempt_disabled();
	cpu_index_t this_cpu = cpulocal_get_index();
	uint32_t    cpu_bit  = (uint32_t)util_bit(this_cpu);
	bool	    new_period;
	bool	    reschedule = false;

	rcu_grace_period_t current_period =
		atomic_load_acquire(&rcu_state.current_period);
	rcu_grace_period_t next_period;

	do {
		next_period = current_period;

		next_period.cpu_bitmap &= ~cpu_bit;

		if (next_period.cpu_bitmap != 0U) {
			// There are still other CPUs to wait for, so we are not
			// starting a new period.
			new_period = false;
		} else {
			// We're the last CPU to acknowledge the current period.
			// Start a new one if there is a CPU that hasn't reached
			// its target yet.
			new_period =
				atomic_load_relaxed(&rcu_state.max_target) !=
				current_period.generation;

			if (new_period) {
				// Fence to ensure that the load of the new
				// active CPU set occurs after any stores on
				// this CPU that must occur before a new grace
				// period starts. This matches the fence in
				// rcu_bitmap_activate_cpu().
				//
				// Note that stores on other CPUs are ordered by
				// the acquire operation on the CPU bitmap load
				// on this CPU and the release operation on the
				// CPU bitmap store on the other CPUs (below).
				atomic_thread_fence(memory_order_seq_cst);

				next_period.cpu_bitmap = atomic_load_relaxed(
					&rcu_state.active_cpus);
				next_period.generation++;
			}
		}
	} while (!atomic_compare_exchange_strong_explicit(
		&rcu_state.current_period, &current_period, next_period,
		memory_order_acq_rel, memory_order_acquire));

	if (new_period) {
		// This matches the thread fences in rcu_bitmap_deactivate_cpu
		// and rcu_bitmap_request_grace_period.
		atomic_thread_fence(memory_order_seq_cst);

		// Check the CPUs that have raced with us in deactivate.
		uint32_t cpus_needing_quiesce =
			next_period.cpu_bitmap &
			~atomic_load_relaxed(&rcu_state.active_cpus);

		// Successfully started a new period. Look for any remote CPUs
		// that may be waiting for it, and IPI them.
		for (cpu_index_t cpu = 0U; cpu < PLATFORM_MAX_CORES; cpu++) {
			if ((cpu == this_cpu) || !platform_cpu_exists(cpu)) {
				continue;
			}
			count_t target = atomic_load_relaxed(
				&CPULOCAL_BY_INDEX(rcu_state, cpu).target);
			if (target == next_period.generation) {
				ipi_one(IPI_REASON_RCU_NOTIFY, cpu);
			}
			// Handle any new CPUs needing quiesce due to a race
			// where they are deactivating themselves and us
			// reading the active_cpus for the next grace period
			// above.
			if ((cpus_needing_quiesce & util_bit(cpu)) != 0U) {
				ipi_one(IPI_REASON_RCU_QUIESCE, cpu);
			}
		}

		rcu_cpu_state_t *my_state = &CPULOCAL(rcu_state);
		if (compiler_expected(my_state->is_active)) {
			// Process the grace period completion.
			reschedule = rcu_bitmap_notify();

			// Trigger another quiesce on the current CPU.
			ipi_one_relaxed(IPI_REASON_RCU_QUIESCE, this_cpu);
		} else if (atomic_load_relaxed(&my_state->update_count) != 0U) {
			// Trigger an IRQ to reactivate the CPU and process the
			// grace period completion. We can't do this while
			// inactive because the updates might have RCU critical
			// sections in them.
			ipi_one(IPI_REASON_RCU_NOTIFY, this_cpu);
		} else {
			// Inactive with no updates; nothing more to do.
		}
	}

	return reschedule;
}

static void
rcu_bitmap_request_grace_period(rcu_cpu_state_t *my_state, count_t current_gen)
	REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	// We need to wait for the next grace period (not the current one) to
	// end, because we may have enqueued new updates during the current
	// period. Therefore our target is the period after the next.
	count_t target = current_gen + 2U;
	atomic_store_relaxed(&my_state->target, target);

	// Since other CPUs may have completed one or more grace periods
	// between our read of current_gen and the target update above, we need
	// to check the current generation again and either notify the current
	// CPU (in case our target was reached before we updated it) or quiesce
	// the current CPU (in case the old max_target was reached).
	//
	// We need a fence here to order the new_period read after the target
	// update. This synchronises with the fence in rcu_bitmap_quiesce().
	atomic_thread_fence(memory_order_seq_cst);

	rcu_grace_period_t new_period =
		atomic_load_relaxed(&rcu_state.current_period);

	if (compiler_unexpected(
		    !util_is_before(new_period.generation, target))) {
		// Another core has already completed our requested grace
		// period. It may have done so before this core updated the
		// target variable, in which case it won't have sent a
		// GP end notification. So, send a local notification.
		ipi_one_relaxed(IPI_REASON_RCU_NOTIFY, cpulocal_get_index());
	} else {
		// Update the max target period to be at least our new target.
		count_t old_max_target =
			atomic_load_relaxed(&rcu_state.max_target);
		do {
			if (util_is_before(target, old_max_target)) {
				// We don't need to update the max target.
				break;
			}
		} while (!atomic_compare_exchange_weak_explicit(
			&rcu_state.max_target, &old_max_target, target,
			memory_order_relaxed, memory_order_relaxed));

		if (new_period.cpu_bitmap == 0U) {
			// RCU reached its old max target on another core; we
			// may need to start the new grace period ourselves.
			ipi_one_relaxed(IPI_REASON_RCU_QUIESCE,
					cpulocal_get_index());
		}
	}
}

bool
rcu_bitmap_notify(void)
{
	bool reschedule = false;

	assert_preempt_disabled();

	rcu_cpu_state_t *my_state = &CPULOCAL(rcu_state);

	// If there are no updates queued on this CPU, do nothing.
	if (atomic_load_relaxed(&my_state->update_count) == 0U) {
		goto out;
	}

	// Update always needs to be handled before notify, to avoid having to
	// merge the ready batches. Note that we can't check the result of
	// ipi_clear() here, because that is not safe in an IPI handler.
	if (my_state->ready_updates) {
		(void)ipi_clear(IPI_REASON_RCU_UPDATE);
		reschedule = rcu_bitmap_update();
	}

	// Check whether the grace period we're currently waiting for (if any)
	// has expired. The acquire here matches the release in
	// rcu_bitmap_quiesce().
	count_t		   target = atomic_load_relaxed(&my_state->target);
	rcu_grace_period_t current_period =
		atomic_load_acquire(&rcu_state.current_period);
	if (util_is_before(current_period.generation, target)) {
		goto out;
	}

	// Advance the batches
	bool waiting_updates = false;
	ENUM_FOREACH(RCU_UPDATE_CLASS, update_class)
	{
		// Ready batch should have been emptied by rcu_bitmap_update()
		assert(my_state->ready_batch.heads[update_class] == NULL);

		// Collect the heads to be shifted for this class
		rcu_entry_t *waiting_head =
			my_state->waiting_batch.heads[update_class];
		rcu_entry_t *next_head =
			my_state->next_batch.heads[update_class];

		// Trigger further batch processing if necessary
		if (waiting_head != NULL) {
			my_state->ready_updates = true;
		}
		if (next_head != NULL) {
			waiting_updates = true;
		}

		// Advance the heads
		my_state->next_batch.heads[update_class]    = NULL;
		my_state->waiting_batch.heads[update_class] = next_head;
		my_state->ready_batch.heads[update_class]   = waiting_head;
	}

	// Request processing of updates if any are ready
	if (my_state->ready_updates) {
		ipi_one_relaxed(IPI_REASON_RCU_UPDATE, cpulocal_get_index());
	}

	// Start a new grace period if we still have updates waiting
	if (waiting_updates) {
		rcu_bitmap_request_grace_period(my_state,
						current_period.generation);
	}

out:
	return reschedule;
}

bool
rcu_bitmap_update(void)
{
	// Call all the callbacks queued in the previous grace period
	count_t		 update_count = 0;
	rcu_cpu_state_t *my_state     = &CPULOCAL(rcu_state);

	rcu_update_status_t status = rcu_update_status_default();

	if (!my_state->ready_updates) {
		goto out;
	}

	ENUM_FOREACH(RCU_UPDATE_CLASS, update_class)
	{
		rcu_entry_t *entry = my_state->ready_batch.heads[update_class];
		my_state->ready_batch.heads[update_class] = NULL;

		while (entry != NULL) {
			// We must read the next pointer _before_ triggering
			// the update, in case the update handler frees the
			// object.
			rcu_entry_t *next = entry->next;
			status		  = rcu_update_status_union(
				trigger_rcu_update_event(
					(rcu_update_class_t)update_class,
					entry),
				status);
			entry = next;
			update_count++;
		}
	}

	if ((update_count != 0U) &&
	    (atomic_fetch_sub_explicit(&my_state->update_count, update_count,
				       memory_order_relaxed) == update_count)) {
		(void)atomic_fetch_sub_explicit(&rcu_state.waiter_count, 1U,
						memory_order_relaxed);
	}

	my_state->ready_updates = false;

out:
	return rcu_update_status_get_need_schedule(&status);
}

void
rcu_bitmap_handle_power_cpu_offline(void)
{
	// We shouldn't get here if there are any pending updates on this CPU.
	// The power aggregation code should have checked this by calling
	// rcu_has_pending_updates() before deciding to offline the core.
	assert(atomic_load_relaxed(&CPULOCAL(rcu_state).update_count) == 0U);

	// Always deactivate & quiesce the CPU, even if RCU doesn't need to run
	// at the moment. This is because the CPU might have been left active
	// when the last update was run, and it won't be able to deactivate
	// once it goes offline.
	rcu_bitmap_deactivate_cpu();
}

bool
rcu_has_pending_updates(void)
{
	return compiler_unexpected(rcu_bitmap_should_run()) &&
	       (atomic_load_relaxed(&CPULOCAL(rcu_state).update_count) != 0U);
}
