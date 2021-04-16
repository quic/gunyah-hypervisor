// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <ipi.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <timer_queue.h>
#include <trace.h>

#include <events/scheduler.h>

#include <asm/event.h>

#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(scheduler_t, scheduler);
CPULOCAL_DECLARE_STATIC(thread_t *_Atomic, primary_thread);
CPULOCAL_DECLARE_STATIC(thread_t *, running_thread);

static_assert((SCHEDULER_DEFAULT_PRIORITY >= SCHEDULER_MIN_PRIORITY) &&
		      (SCHEDULER_DEFAULT_PRIORITY <= SCHEDULER_MAX_PRIORITY),
	      "Default priority is invalid.");
static_assert((SCHEDULER_DEFAULT_TIMESLICE <= SCHEDULER_MAX_TIMESLICE) &&
		      (SCHEDULER_DEFAULT_TIMESLICE >= SCHEDULER_MIN_TIMESLICE),
	      "Default timeslice is invalid.");

static ticks_t
get_target_timeout(thread_t *target)
{
	return target->scheduler_schedtime + target->scheduler_active_timeslice;
}

static void
reset_sched_params(thread_t *target)
{
	target->scheduler_active_timeslice = target->scheduler_base_timeslice;
}

static void
set_yield_to(thread_t *target, thread_t *yield_to)
{
	assert(target != yield_to);
	assert(target->scheduler_yield_to == NULL);

	target->scheduler_yield_to = object_get_thread_additional(yield_to);
}

static void
discard_yield_to(thread_t *target)
{
	assert(target->scheduler_yield_to != NULL);

	object_put_thread(target->scheduler_yield_to);
	target->scheduler_yield_to = NULL;
}

static bool
update_timeslice(thread_t *target, ticks_t curticks)
{
	assert(target != NULL);

	ticks_t timeout = get_target_timeout(target);
	bool	expired = timeout <= curticks;

	if (expired) {
		reset_sched_params(target);
		if (target->scheduler_yield_to != NULL) {
			discard_yield_to(target);
		}
	} else {
		// Account for the time the target has used.
		target->scheduler_active_timeslice = timeout - curticks;
	}

	return expired;
}

static void
add_to_runqueue(scheduler_t *scheduler, thread_t *target)
{
	assert_preempt_disabled();

	index_t i	  = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
	list_t *list	  = &scheduler->runqueue[i];
	bool	was_empty = list_is_empty(list);

	assert(was_empty || bitmap_isset(scheduler->prio_bitmap, i));

	list_insert_at_tail(list, &target->scheduler_list_node);
	if (was_empty) {
		bitmap_set(scheduler->prio_bitmap, i);
	}
}

static void
remove_from_runqueue(scheduler_t *scheduler, thread_t *target)
{
	assert_preempt_disabled();

	index_t	     i	  = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
	list_t *     list = &scheduler->runqueue[i];
	list_node_t *node = &target->scheduler_list_node;
	bool	     was_head = node == list_get_head(list);

	assert(bitmap_isset(scheduler->prio_bitmap, i));

	if (!list_delete_node(list, node) && was_head) {
		assert(list_is_empty(list));
		bitmap_clear(scheduler->prio_bitmap, i);
	}
}

static thread_t *
pop_runqueue_head(scheduler_t *scheduler, index_t i)
{
	assert_preempt_disabled();
	assert(bitmap_isset(scheduler->prio_bitmap, i));

	list_t *list = &scheduler->runqueue[i];
	assert(!list_is_empty(list));

	list_node_t *node = list_get_head(list);
	assert(node != NULL);

	thread_t *head = thread_container_of_scheduler_list_node(node);
	assert(head->scheduler_priority == (SCHEDULER_MAX_PRIORITY - i));
	remove_from_runqueue(scheduler, head);

	return head;
}

void
scheduler_fprr_handle_boot_cold_init(void)
{
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		scheduler_t *scheduler = &CPULOCAL_BY_INDEX(scheduler, i);
		spinlock_init(&scheduler->lock);
		timer_init_object(&scheduler->timer, TIMER_ACTION_RESCHEDULE);
		for (index_t j = 0U; j < SCHEDULER_NUM_PRIORITIES; j++) {
			list_init(&scheduler->runqueue[j]);
		}
	}
}

error_t
scheduler_fprr_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;

	assert(thread != NULL);
	assert(thread->state == THREAD_STATE_INIT);
	assert(!sched_state_get_init(&thread->scheduler_state));
	assert(!scheduler_is_runnable(thread));

	spinlock_init(&thread->scheduler_lock);
	atomic_init(&thread->scheduler_active_affinity, CPU_INDEX_INVALID);
	thread->scheduler_prev_affinity = CPU_INDEX_INVALID;

	cpu_index_t cpu = thread_create.scheduler_affinity_valid
				  ? thread_create.scheduler_affinity
				  : CPU_INDEX_INVALID;
	thread->scheduler_affinity = cpu;

	priority_t prio = thread_create.scheduler_priority_valid
				  ? thread_create.scheduler_priority
				  : SCHEDULER_DEFAULT_PRIORITY;
	assert((prio <= SCHEDULER_MAX_PRIORITY) &&
	       (prio >= SCHEDULER_MIN_PRIORITY));
	thread->scheduler_priority = prio;

	nanoseconds_t timeslice = thread_create.scheduler_timeslice_valid
					  ? thread_create.scheduler_timeslice
					  : SCHEDULER_DEFAULT_TIMESLICE;
	assert((timeslice <= SCHEDULER_MAX_TIMESLICE) &&
	       (timeslice >= SCHEDULER_MIN_TIMESLICE));
	thread->scheduler_base_timeslice = timer_convert_ns_to_ticks(timeslice);

	sched_state_set_init(&thread->scheduler_state, true);

	return OK;
}

error_t
scheduler_fprr_handle_object_activate_thread(thread_t *thread)
{
	error_t err = OK;

#if !SCHEDULER_CAN_MIGRATE
	scheduler_lock(thread);
	// The thread must have a valid affinity if scheduler cannot migrate.
	if (!cpulocal_index_valid(thread->scheduler_affinity)) {
		err = ERROR_OBJECT_CONFIG;
	}
	scheduler_unlock(thread);
#else
	(void)thread;
#endif

	return err;
}

#if defined(HYPERCALLS)
bool
scheduler_fprr_handle_vcpu_activate_thread(thread_t *	       thread,
					   vcpu_option_flags_t options)
{
	bool ret = false, pin = false;

	assert(thread->kind == THREAD_KIND_VCPU);

	scheduler_lock(thread);

	// Assert that the platform soc_* handler ran before us
	assert(vcpu_option_flags_get_hlos_vm(&thread->vcpu_options) ==
	       vcpu_option_flags_get_hlos_vm(&options));

	if (vcpu_option_flags_get_hlos_vm(&thread->vcpu_options)) {
		if (!cpulocal_index_valid(thread->scheduler_affinity)) {
			goto out;
		}

		thread_t *_Atomic *primary_thread_p = &CPULOCAL_BY_INDEX(
			primary_thread, thread->scheduler_affinity);
		thread_t *expected = NULL;
		if (atomic_compare_exchange_strong_explicit(
			    primary_thread_p, &expected, thread,
			    memory_order_relaxed, memory_order_relaxed)) {
			// The primary thread can't be migrated.
			pin = true;
		} else {
			goto out;
		}
	}

	if (vcpu_option_flags_get_pinned(&options)) {
		if (cpulocal_index_valid(thread->scheduler_affinity)) {
			pin = true;
		} else {
			goto out;
		}
	}

	if (pin) {
		scheduler_pin(thread);
		vcpu_option_flags_set_pinned(&thread->vcpu_options, true);
	}

	ret = true;
out:
	scheduler_unlock(thread);
	return ret;
}
#endif

void
scheduler_fprr_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	if (cpulocal_index_valid(thread->scheduler_affinity)) {
		thread_t *_Atomic *primary_thread_p = &CPULOCAL_BY_INDEX(
			primary_thread, thread->scheduler_affinity);
		if (atomic_load_relaxed(primary_thread_p) == thread) {
			atomic_store_relaxed(primary_thread_p, NULL);
		}
	}

	if (thread->scheduler_yield_to != NULL) {
		discard_yield_to(thread);
	}
}

bool
scheduler_fprr_handle_ipi_reschedule(void)
{
	return true;
}

bool
scheduler_fppr_handle_timer_reschedule(void)
{
	assert_preempt_disabled();

	CPULOCAL(scheduler).timeout_set = false;
	scheduler_trigger();
	return true;
}

rcu_update_status_t
scheduler_fppr_handle_affinity_change_update(rcu_entry_t *entry)
{
	rcu_update_status_t ret = rcu_update_status_default();

	thread_t *  thread = thread_container_of_scheduler_rcu_entry(entry);
	cpu_index_t prev_cpu, next_cpu;

	scheduler_lock(thread);
	assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED));
	prev_cpu = thread->scheduler_prev_affinity;
	next_cpu = thread->scheduler_affinity;
	scheduler_unlock(thread);

	trigger_scheduler_affinity_changed_sync_event(thread, prev_cpu,
						      next_cpu);

	scheduler_lock(thread);
	if (scheduler_unblock(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED)) {
		rcu_update_status_set_need_schedule(&ret, true);
	}
	scheduler_unlock(thread);

	object_put_thread(thread);

	return ret;
}

static void
set_next_timeout(scheduler_t *scheduler, thread_t *target)
{
	assert_preempt_disabled();

	bool need_timeout = false;

	if (target != idle_thread()) {
		// A timeout needs to be set if the scheduler queue
		// for the current priority is not empty, or if we
		// may yield to another target.
		index_t i = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
		need_timeout = bitmap_isset(scheduler->prio_bitmap, i) ||
			       (target->scheduler_yield_to != NULL);
	}

	if (need_timeout) {
		ticks_t timeout = get_target_timeout(target);
		if (scheduler->timeout_set) {
			// The timer only needs to be updated
			// if the timeout actually changed.
			if (timeout != scheduler->timeout) {
				timer_update(&scheduler->timer, timeout);
				scheduler->timeout = timeout;
			}
		} else {
			timer_enqueue(&scheduler->timer, timeout);
			scheduler->timeout     = timeout;
			scheduler->timeout_set = true;
		}
	} else if (scheduler->timeout_set) {
		timer_dequeue(&scheduler->timer);
		scheduler->timeout_set = false;
	} else {
		// No timeout set, nothing to do.
	}
}

static thread_t *
get_next_target(scheduler_t *scheduler)
{
	assert_preempt_disabled();

	thread_t *prev		    = scheduler->active_thread;
	thread_t *target	    = prev;
	ticks_t	  curticks	    = timer_get_current_timer_ticks();
	bool	  timeslice_expired = false;
	index_t	  i;

	if (target != NULL) {
		timeslice_expired = update_timeslice(target, curticks);
	}

	if (bitmap_ffs(scheduler->prio_bitmap, SCHEDULER_NUM_PRIORITIES, &i)) {
		priority_t prio = SCHEDULER_MAX_PRIORITY - i;
		// Always prefer targets with higher priority, and if timeslice
		// has been used up, targets with the same priority.
		bool should_switch =
			(target == NULL) ||
			(timeslice_expired
				 ? (prio >= target->scheduler_priority)
				 : (prio > target->scheduler_priority));
		if (should_switch) {
			target = pop_runqueue_head(scheduler, i);
		}
	}

	if (target != NULL) {
		target->scheduler_schedtime = curticks;
		scheduler->active_thread    = target;
	} else {
		target			 = idle_thread();
		scheduler->active_thread = NULL;
	}

	if ((prev != NULL) && (target != prev)) {
		add_to_runqueue(scheduler, prev);
	}

	return target;
}

static bool
can_yield_to(thread_t *yield_to)
{
	assert_preempt_disabled();

	thread_t *  current  = thread_get_self();
	cpu_index_t cpu	     = cpulocal_get_index();
	cpu_index_t affinity = yield_to->scheduler_affinity;
	bool	    yield    = true;

	// The target's affinity must be equal to this cpu or invalid.
	if (cpulocal_index_valid(affinity) && (affinity != cpu)) {
		yield = false;
		goto out;
	}

	// If yielded_from is set and yield_to isn't the current thread,
	// then yield_to must already be selected to run on another cpu.
	if ((yield_to->scheduler_yielded_from != NULL) &&
	    (yield_to != current)) {
		yield = false;
		goto out;
	}

	if (!scheduler_is_runnable(yield_to)) {
		yield = false;
	}
out:
	return yield;
}

static thread_t *
select_yield_target(thread_t *target)
{
	assert_preempt_disabled();

	thread_t *next = target;

	thread_t *yield_to = target->scheduler_yield_to;
	if (yield_to != NULL) {
		scheduler_lock(yield_to);
		if (can_yield_to(yield_to)) {
			yield_to->scheduler_yielded_from = target;
			next				 = yield_to;
		} else {
			discard_yield_to(target);
		}
		scheduler_unlock(yield_to);
	}

	return next;
}

bool
scheduler_schedule(void)
{
	bool must_schedule = true;
	bool switched	   = false;

	preempt_disable();

	while (must_schedule) {
		scheduler_t *scheduler = &CPULOCAL(scheduler);
		thread_t *   target;

		spinlock_acquire(&scheduler->lock);
		target = get_next_target(scheduler);
		set_next_timeout(scheduler, target);
		spinlock_release(&scheduler->lock);

		target = select_yield_target(target);

		if (target == thread_get_self()) {
			trigger_scheduler_quiescent_event();
			must_schedule = false;
		} else {
			// Get an additional reference which will be released
			// when the thread stops running.
			(void)object_get_thread_additional(target);
			if (compiler_expected(thread_switch_to(target) == OK)) {
				switched = true;
				must_schedule =
					ipi_clear(IPI_REASON_RESCHEDULE);
			} else {
				must_schedule = true;
			}
		}
	}

	preempt_enable();

	return switched;
}

void
scheduler_trigger(void)
{
	cpu_index_t cpu = cpulocal_get_index();
	ipi_one_relaxed(IPI_REASON_RESCHEDULE, cpu);
}

void
scheduler_yield(void)
{
	thread_t *current = thread_get_self();

	preempt_disable();
	thread_t *yielded_from = current->scheduler_yielded_from;
	if (yielded_from != NULL) {
		discard_yield_to(yielded_from);
	} else {
		current->scheduler_active_timeslice = 0U;
	}
	(void)scheduler_schedule();
	preempt_enable();
}

void
scheduler_yield_to(thread_t *target)
{
	thread_t *current = thread_get_self();

	assert(current != target);

	preempt_disable();

	// Pin the current thread while yielding.
	// We don't support migration while yielding to,
	// and this allows the yield_to pointer to be
	// safely accessed without the thread lock.
	scheduler_lock(current);
	scheduler_pin(current);
	scheduler_unlock(current);

	thread_t *yielded_from = current->scheduler_yielded_from;
	if (yielded_from == target) {
		discard_yield_to(yielded_from);
	} else if (yielded_from != NULL) {
		discard_yield_to(yielded_from);
		set_yield_to(yielded_from, target);
	} else {
		set_yield_to(current, target);
	}

	(void)scheduler_schedule();

	scheduler_lock(current);
	scheduler_unpin(current);
	scheduler_unlock(current);

	preempt_enable();
}

void
scheduler_lock(thread_t *thread)
{
	spinlock_acquire(&thread->scheduler_lock);
}

void
scheduler_unlock(thread_t *thread)
{
	spinlock_release(&thread->scheduler_lock);
}

static bool
add_thread_to_scheduler(thread_t *thread)
{
	assert_preempt_disabled();
	assert(scheduler_is_runnable(thread));
	assert(!sched_state_get_running(&thread->scheduler_state));
	assert(!sched_state_get_queued(&thread->scheduler_state));

	bool	    need_schedule = true;
	cpu_index_t affinity	  = thread->scheduler_affinity;

	if (cpulocal_index_valid(affinity)) {
		cpu_index_t  cpu = cpulocal_get_index();
		scheduler_t *scheduler =
			&CPULOCAL_BY_INDEX(scheduler, affinity);

		spinlock_acquire(&scheduler->lock);

		(void)object_get_thread_additional(thread);
		reset_sched_params(thread);
		sched_state_set_queued(&thread->scheduler_state, true);
		add_to_runqueue(scheduler, thread);

		if ((scheduler->active_thread != NULL) &&
		    (thread->scheduler_priority <
		     scheduler->active_thread->scheduler_priority)) {
			// The unblocked thread has lower priority than the
			// active thread; there is no need to schedule until
			// the active thread is blocked.
			need_schedule = false;
		}

		spinlock_release(&scheduler->lock);

		if (need_schedule && (cpu != affinity)) {
			ipi_one(IPI_REASON_RESCHEDULE, affinity);
			need_schedule = false;
		}
	} else {
		need_schedule = false;
	}

	return need_schedule;
}

static void
remove_thread_from_scheduler(thread_t *thread)
{
	assert_preempt_disabled();

	cpu_index_t affinity = thread->scheduler_affinity;
	if (cpulocal_index_valid(affinity)) {
		assert(sched_state_get_queued(&thread->scheduler_state));

		scheduler_t *scheduler =
			&CPULOCAL_BY_INDEX(scheduler, affinity);
		spinlock_acquire(&scheduler->lock);
		if (scheduler->active_thread == thread) {
			scheduler->active_thread = NULL;
		} else {
			remove_from_runqueue(scheduler, thread);
		}
		spinlock_release(&scheduler->lock);

		sched_state_set_queued(&thread->scheduler_state, false);
		object_put_thread(thread);
	}
}

static bool
resched_running_thread(thread_t *thread)
{
	assert_preempt_disabled();
	assert(sched_state_get_running(&thread->scheduler_state));
	assert(!sched_state_get_queued(&thread->scheduler_state));

	bool	    need_schedule = true;
	cpu_index_t cpu =
		atomic_load_relaxed(&thread->scheduler_active_affinity);

	assert(cpulocal_index_valid(cpu));

	if (cpu != cpulocal_get_index()) {
		ipi_one(IPI_REASON_RESCHEDULE, cpu);
		need_schedule = false;
	}

	return need_schedule;
}

static bool
start_affinity_changed_events(thread_t *thread)
{
	assert_preempt_disabled();
	assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED));

	bool need_sync = false, need_schedule = false;

	trigger_scheduler_affinity_changed_event(
		thread, thread->scheduler_prev_affinity,
		thread->scheduler_affinity, &need_sync);

	if (need_sync) {
		rcu_enqueue(&thread->scheduler_rcu_entry,
			    RCU_UPDATE_CLASS_AFFINITY_CHANGED);
	} else {
		need_schedule = scheduler_unblock(
			thread, SCHEDULER_BLOCK_AFFINITY_CHANGED);
		object_put_thread(thread);
	}

	return need_schedule;
}

error_t
scheduler_fprr_handle_thread_context_switch_pre(thread_t *next)
{
	assert_preempt_disabled();

	assert(next != thread_get_self());

	error_t	    err = OK;
	cpu_index_t cpu = cpulocal_get_index();

	scheduler_lock(next);
	cpu_index_t affinity	 = next->scheduler_affinity;
	thread_t *  yielded_from = next->scheduler_yielded_from;

	// The next thread's affinity could have changed between target
	// selection and now; it may have been blocked by or is already running
	// on another CPU. Only set it running if it is still valid to do so.
	bool runnable =
		!sched_state_get_running(&next->scheduler_state) &&
		(scheduler_is_runnable(next) || (next == idle_thread()));
	bool affinity_valid =
		(affinity == cpu) ||
		(!cpulocal_index_valid(affinity) && (yielded_from != NULL));

	if (compiler_expected(runnable && affinity_valid)) {
		assert(next->state != THREAD_STATE_INIT);
		assert(next->state != THREAD_STATE_EXITED);
		assert(!sched_state_get_need_requeue(&next->scheduler_state));
		sched_state_set_running(&next->scheduler_state, true);
		CPULOCAL(running_thread) = next;
		atomic_store_relaxed(&next->scheduler_active_affinity, cpu);
	} else {
		err = ERROR_DENIED;
		if (yielded_from != NULL) {
			discard_yield_to(yielded_from);
			next->scheduler_yielded_from = NULL;
		}
	}
	scheduler_unlock(next);

	return err;
}

noreturn void
scheduler_fprr_unwind_thread_context_switch_pre(void)
{
	panic("Context switch pre failed!");
}

void
scheduler_fprr_handle_thread_context_switch_post(thread_t *prev)
{
	assert_preempt_disabled();

	bool need_schedule = false;

	scheduler_lock(prev);
	prev->scheduler_yielded_from = NULL;
	sched_state_set_running(&prev->scheduler_state, false);

	if (sched_state_get_need_requeue(&prev->scheduler_state)) {
		// The thread may have blocked after being marked for a
		// requeue. Ensure it is still runnable prior to adding
		// it to a scheduler queue.
		if (scheduler_is_runnable(prev)) {
			need_schedule = add_thread_to_scheduler(prev);
		}
		sched_state_set_need_requeue(&prev->scheduler_state, false);
	}

	if (scheduler_is_blocked(prev, SCHEDULER_BLOCK_AFFINITY_CHANGED)) {
		need_schedule = start_affinity_changed_events(prev);
	}

	// Store and wake for scheduler_sync().
	asm_event_store_and_wake(&prev->scheduler_active_affinity,
				 CPU_INDEX_INVALID);
	scheduler_unlock(prev);

	if (need_schedule) {
		scheduler_trigger();
	}
}

void
scheduler_block(thread_t *thread, scheduler_block_t block)
{
	TRACE(DEBUG, INFO,
	      "scheduler: block {:#x}, reason: {:d}, others: {:#x}",
	      (uintptr_t)thread, block, thread->scheduler_block_bits[0]);

	assert_preempt_disabled();
	assert(block <= SCHEDULER_BLOCK__MAX);
	bitmap_set(thread->scheduler_block_bits, block);
	if (sched_state_get_queued(&thread->scheduler_state)) {
		remove_thread_from_scheduler(thread);
	}
}

void
scheduler_block_init(thread_t *thread, scheduler_block_t block)
{
	assert(!sched_state_get_init(&thread->scheduler_state));
	assert(block <= SCHEDULER_BLOCK__MAX);
	bitmap_set(thread->scheduler_block_bits, block);
}

bool
scheduler_unblock(thread_t *thread, scheduler_block_t block)
{
	assert_preempt_disabled();
	assert(block <= SCHEDULER_BLOCK__MAX);
	bool was_blocked = bitmap_isset(thread->scheduler_block_bits, block);
	bitmap_clear(thread->scheduler_block_bits, block);
	bool need_schedule = was_blocked && scheduler_is_runnable(thread);

	if (need_schedule) {
		assert(!sched_state_get_queued(&thread->scheduler_state));
		// The thread may have not finished running after the block.
		// If so, mark for requeue. Otherwise it is safe to directly
		// queue the thread.
		if (compiler_unexpected(sched_state_get_running(
			    &thread->scheduler_state))) {
			sched_state_set_need_requeue(&thread->scheduler_state,
						     true);
			need_schedule = resched_running_thread(thread);
		} else {
			need_schedule = add_thread_to_scheduler(thread);
		}
	}

	TRACE(DEBUG, INFO,
	      "scheduler: unblock {:#x}, reason: {:d}, others: {:#x}, local run: {:d}",
	      (uintptr_t)thread, block, thread->scheduler_block_bits[0],
	      need_schedule);

	return need_schedule;
}

bool
scheduler_is_blocked(const thread_t *thread, scheduler_block_t block)
{
	assert(block <= SCHEDULER_BLOCK__MAX);
	return bitmap_isset(thread->scheduler_block_bits, block);
}

bool
scheduler_is_runnable(const thread_t *thread)
{
	return bitmap_empty(thread->scheduler_block_bits,
			    SCHEDULER_NUM_BLOCK_BITS);
}

thread_t *
scheduler_get_primary_vcpu(cpu_index_t cpu)
{
	return atomic_load_consume(&CPULOCAL_BY_INDEX(primary_thread, cpu));
}

void
scheduler_sync(thread_t *thread)
{
	_Atomic cpu_index_t *affinity_p = &thread->scheduler_active_affinity;

	cpu_index_t cpu = atomic_load_acquire(affinity_p);
	if (cpulocal_index_valid(cpu)) {
		ipi_one(IPI_REASON_RESCHEDULE, cpu);
		while (cpulocal_index_valid(
			asm_event_load_before_wait(affinity_p))) {
			asm_event_wait(affinity_p);
		}
	}
}

void
scheduler_pin(thread_t *thread)
{
	assert_preempt_disabled();
	thread->scheduler_pin_count++;
}

void
scheduler_unpin(thread_t *thread)
{
	assert_preempt_disabled();
	assert(thread->scheduler_pin_count > 0U);
	thread->scheduler_pin_count--;
}

cpu_index_t
scheduler_get_affinity(thread_t *thread)
{
	assert_preempt_disabled();

	return thread->scheduler_affinity;
}

cpu_index_t
scheduler_get_active_affinity(thread_t *thread)
{
	assert_preempt_disabled();

	cpu_index_t cpu =
		atomic_load_relaxed(&thread->scheduler_active_affinity);

	return cpulocal_index_valid(cpu) ? cpu : thread->scheduler_affinity;
}

error_t
scheduler_set_affinity(thread_t *thread, cpu_index_t target_cpu)
{
	assert_preempt_disabled();

	error_t	    err		  = OK;
	bool	    need_schedule = false;
	cpu_index_t prev_cpu	  = thread->scheduler_affinity;

	if (prev_cpu == target_cpu) {
		goto out;
	}

	if (thread->scheduler_pin_count != 0U) {
		err = ERROR_DENIED;
		goto out;
	}

	if (scheduler_is_blocked(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED)) {
		err = ERROR_RETRY;
		goto out;
	}

	// Block the thread so affinity changes are serialised. We need to get
	// an additional reference to the thread, otherwise it may be deleted
	// prior to the completion of the affinity change.
	(void)object_get_thread_additional(thread);
	scheduler_block(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED);

	thread->scheduler_prev_affinity = prev_cpu;
	thread->scheduler_affinity	= target_cpu;

	if (sched_state_get_running(&thread->scheduler_state)) {
		// Trigger a reschedule on the running thread's CPU; the
		// context switch will trigger the affinity changed event.
		need_schedule = resched_running_thread(thread);
	} else {
		need_schedule = start_affinity_changed_events(thread);
	}

	if (need_schedule) {
		scheduler_trigger();
	}

out:
	return err;
}

static void
update_sched_params(thread_t *thread, priority_t priority, ticks_t timeslice)
{
	assert_preempt_disabled();

	// If the thread is blocked, or is still running and has been marked for
	// a requeue, then it is safe to update the scheduler parameters without
	// any queue operations. If not, it first needs to be removed from its
	// queue before the update, then added back when it is safe to do so.
	bool requeue = scheduler_is_runnable(thread) &&
		       !sched_state_get_need_requeue(&thread->scheduler_state);

	if (requeue) {
		remove_thread_from_scheduler(thread);
	}

	thread->scheduler_priority	 = priority;
	thread->scheduler_base_timeslice = timeslice;

	if (requeue) {
		bool need_schedule;
		if (sched_state_get_running(&thread->scheduler_state)) {
			sched_state_set_need_requeue(&thread->scheduler_state,
						     true);
			need_schedule = resched_running_thread(thread);
		} else {
			need_schedule = add_thread_to_scheduler(thread);
		}

		if (need_schedule) {
			scheduler_trigger();
		}
	}
}

error_t
scheduler_set_priority(thread_t *thread, priority_t priority)
{
	error_t err = OK;

	assert_preempt_disabled();

	if ((priority < SCHEDULER_MIN_PRIORITY) ||
	    (priority > SCHEDULER_MAX_PRIORITY)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (thread->scheduler_priority != priority) {
		update_sched_params(thread, priority,
				    thread->scheduler_base_timeslice);
	}

out:
	return err;
}

error_t
scheduler_set_timeslice(thread_t *thread, nanoseconds_t timeslice)
{
	error_t err = OK;

	assert_preempt_disabled();

	if ((timeslice > SCHEDULER_MAX_TIMESLICE) ||
	    (timeslice < SCHEDULER_MIN_TIMESLICE)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	ticks_t timeslice_ticks = timer_convert_ns_to_ticks(timeslice);
	if (thread->scheduler_base_timeslice != timeslice_ticks) {
		update_sched_params(thread, thread->scheduler_priority,
				    timeslice_ticks);
	}

out:
	return err;
}

bool
scheduler_current_can_idle(void)
{
	assert_preempt_disabled();

	bool	     can_idle  = false;
	thread_t *   current   = thread_get_self();
	scheduler_t *scheduler = &CPULOCAL(scheduler);

	if (compiler_unexpected(idle_is_current())) {
		can_idle = true;
		goto out;
	}

	spinlock_acquire(&scheduler->lock);
	// If current is not the active thread, we need to reschedule.
	// Otherwise, check if there are any threads in the CPU's queues.
	if (current == scheduler->active_thread) {
		can_idle = bitmap_empty(scheduler->prio_bitmap,
					SCHEDULER_NUM_PRIORITIES);
	}
	spinlock_release(&scheduler->lock);

out:
	return can_idle;
}

#if !defined(UNIT_TESTS)
idle_state_t
scheduler_fprr_handle_vcpu_idle_fastpath(void)
{
	idle_state_t state = IDLE_STATE_IDLE;

	if (!scheduler_current_can_idle()) {
		state = IDLE_STATE_RESCHEDULE;
	}

	return state;
}
#endif
