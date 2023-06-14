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
#include <enum.h>
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
#if defined(INTERFACE_VCPU)
#include <vcpu.h>
#endif

#include <events/scheduler.h>

#include <asm/event.h>

#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(scheduler_t, scheduler);
CPULOCAL_DECLARE_STATIC(thread_t *_Atomic, primary_thread);
CPULOCAL_DECLARE_STATIC(thread_t *, running_thread);
CPULOCAL_DECLARE_STATIC(thread_t *, yielded_from);

static BITMAP_DECLARE(SCHEDULER_NUM_BLOCK_BITS, non_killable_block_mask);

static_assert((SCHEDULER_DEFAULT_PRIORITY >= SCHEDULER_MIN_PRIORITY) &&
		      (SCHEDULER_DEFAULT_PRIORITY <= SCHEDULER_MAX_PRIORITY),
	      "Default priority is invalid.");
static_assert((SCHEDULER_DEFAULT_TIMESLICE <= SCHEDULER_MAX_TIMESLICE) &&
		      (SCHEDULER_DEFAULT_TIMESLICE >= SCHEDULER_MIN_TIMESLICE),
	      "Default timeslice is invalid.");
static_assert(SCHEDULER_BLOCK__MAX < BITMAP_WORD_BITS,
	      "Scheduler block flags must fit in a register");

static ticks_t
get_target_timeout(scheduler_t *scheduler, thread_t *target)
	REQUIRE_PREEMPT_DISABLED
{
	assert(scheduler != NULL);
	assert(target != NULL);

	return scheduler->schedtime + target->scheduler_active_timeslice;
}

static void
reset_sched_params(thread_t *target)
{
	assert(target != NULL);

	target->scheduler_active_timeslice = target->scheduler_base_timeslice;
}

static void
set_yield_to(thread_t *target, thread_t *yield_to)
{
	assert(target != NULL);
	assert(yield_to != NULL);
	assert(target != yield_to);
	assert(target->scheduler_yield_to == NULL);

	target->scheduler_yield_to = object_get_thread_additional(yield_to);
}

static void
discard_yield_to(thread_t *target)
{
	assert(target != NULL);
	assert(target->scheduler_yield_to != NULL);

	object_put_thread(target->scheduler_yield_to);
	target->scheduler_yield_to = NULL;
}

static void
end_directed_yield(thread_t *target)
{
	assert(target != NULL);

	atomic_store_relaxed(&target->scheduler_yielding, false);
}

static bool
update_timeslice(scheduler_t *scheduler, thread_t *target, ticks_t curticks)
	REQUIRE_PREEMPT_DISABLED
{
	assert(scheduler != NULL);
	assert(target != NULL);

	ticks_t timeout = get_target_timeout(scheduler, target);
	bool	expired = timeout <= curticks;

	if (expired) {
		reset_sched_params(target);
		end_directed_yield(target);
	} else {
		// Account for the time the target has used.
		target->scheduler_active_timeslice = timeout - curticks;
	}

	return expired;
}

static void
add_to_runqueue(scheduler_t *scheduler, thread_t *target, bool at_tail)
	REQUIRE_SPINLOCK(scheduler->lock)
{
	assert_preempt_disabled();
	assert_spinlock_held(&scheduler->lock);

	index_t i	  = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
	list_t *list	  = &scheduler->runqueue[i];
	bool	was_empty = list_is_empty(list);

	assert(was_empty || bitmap_isset(scheduler->prio_bitmap, i));

	if (at_tail) {
		list_insert_at_tail(list, &target->scheduler_list_node);
	} else {
		list_insert_at_head(list, &target->scheduler_list_node);
	}

	if (was_empty) {
		bitmap_set(scheduler->prio_bitmap, i);
	}
}

static void
remove_from_runqueue(scheduler_t *scheduler, thread_t *target)
	REQUIRE_SPINLOCK(scheduler->lock)
{
	assert_preempt_disabled();

	index_t	     i	  = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
	list_t	    *list = &scheduler->runqueue[i];
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
	REQUIRE_SPINLOCK(scheduler->lock)
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

static bool
can_be_scheduled(const thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);

	register_t block_bits = thread->scheduler_block_bits[0];

	if (compiler_unexpected(
		    sched_state_get_killed(&thread->scheduler_state))) {
		block_bits &= non_killable_block_mask[0];
	}

	return bitmap_empty(&block_bits, SCHEDULER_NUM_BLOCK_BITS);
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

	ENUM_FOREACH(SCHEDULER_BLOCK, block)
	{
		scheduler_block_properties_t props =
			trigger_scheduler_get_block_properties_event(
				(scheduler_block_t)block);
		if (scheduler_block_properties_get_non_killable(&props)) {
			bitmap_set(non_killable_block_mask, block);
		}
	}
}

error_t
scheduler_fprr_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;

	assert(thread != NULL);
	assert(atomic_load_relaxed(&thread->state) == THREAD_STATE_INIT);
	assert(!sched_state_get_init(&thread->scheduler_state));
	assert(!bitmap_empty(thread->scheduler_block_bits,
			     SCHEDULER_NUM_BLOCK_BITS));

	spinlock_init(&thread->scheduler_lock);
	atomic_init(&thread->scheduler_active_affinity, CPU_INDEX_INVALID);
	thread->scheduler_prev_affinity = CPU_INDEX_INVALID;

	cpu_index_t cpu		   = thread_create.scheduler_affinity_valid
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

#if defined(INTERFACE_VCPU)
bool
scheduler_fprr_handle_vcpu_activate_thread(thread_t	      *thread,
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

void
scheduler_fprr_handle_vcpu_wakeup(thread_t *thread)
	REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	assert(thread->kind == THREAD_KIND_VCPU);

	bool was_yielding = atomic_exchange_explicit(
		&thread->scheduler_yielding, false, memory_order_relaxed);
	if (compiler_unexpected(was_yielding)) {
		cpu_index_t affinity = thread->scheduler_affinity;

		// The thread must have a valid affinity in order to perform
		// a directed yield; see remove_thread_from_scheduler().
		assert(cpulocal_index_valid(affinity));

		scheduler_t *scheduler =
			&CPULOCAL_BY_INDEX(scheduler, affinity);
		bool is_active;

		spinlock_acquire_nopreempt(&scheduler->lock);
		is_active = scheduler->active_thread == thread;
		spinlock_release_nopreempt(&scheduler->lock);

		if (is_active) {
			// The thread is actively yielding; trigger a reschedule
			// so the cancellation of the yield is observed.
			if (affinity != cpulocal_get_index()) {
				ipi_one(IPI_REASON_RESCHEDULE, affinity);
			} else {
				scheduler_trigger();
			}
		}
	}
}

bool
scheduler_fprr_handle_vcpu_expects_wakeup(const thread_t *thread)
{
	assert(thread->kind == THREAD_KIND_VCPU);

	return atomic_load_relaxed(&thread->scheduler_yielding);
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
}

bool
scheduler_fprr_handle_ipi_reschedule(void)
{
	return true;
}

bool
scheduler_fprr_handle_timer_reschedule(void)
{
	assert_preempt_disabled();

	scheduler_trigger();

	return true;
}

scheduler_block_properties_t
scheduler_fprr_handle_scheduler_get_block_properties(scheduler_block_t block)
{
	assert(block == SCHEDULER_BLOCK_AFFINITY_CHANGED);

	scheduler_block_properties_t props =
		scheduler_block_properties_default();
	scheduler_block_properties_set_non_killable(&props, true);

	return props;
}

rcu_update_status_t
scheduler_fprr_handle_affinity_change_update(rcu_entry_t *entry)
{
	rcu_update_status_t ret = rcu_update_status_default();

	thread_t   *thread = thread_container_of_scheduler_rcu_entry(entry);
	cpu_index_t prev_cpu, next_cpu;

	scheduler_lock_nopreempt(thread);
	assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED));
	prev_cpu = thread->scheduler_prev_affinity;
	next_cpu = thread->scheduler_affinity;
	scheduler_unlock_nopreempt(thread);

	trigger_scheduler_affinity_changed_sync_event(thread, prev_cpu,
						      next_cpu);

	scheduler_lock_nopreempt(thread);
	if (scheduler_unblock(thread, SCHEDULER_BLOCK_AFFINITY_CHANGED)) {
		rcu_update_status_set_need_schedule(&ret, true);
	}
	scheduler_unlock_nopreempt(thread);

	object_put_thread(thread);

	return ret;
}

static void
set_next_timeout(scheduler_t *scheduler, thread_t *target)
	REQUIRE_SPINLOCK(scheduler->lock)
{
	assert_spinlock_held(&scheduler->lock);

	bool need_timeout = false;

	if (target != idle_thread()) {
		// A timeout needs to be set if the scheduler queue
		// for the current priority is not empty, or if we
		// may yield to another target.
		index_t i = SCHEDULER_MAX_PRIORITY - target->scheduler_priority;
		need_timeout = bitmap_isset(scheduler->prio_bitmap, i) ||
			       atomic_load_relaxed(&target->scheduler_yielding);
	}

	if (need_timeout) {
		ticks_t timeout = get_target_timeout(scheduler, target);
		timer_update(&scheduler->timer, timeout);
	} else {
		timer_dequeue(&scheduler->timer);
	}
}

static thread_t *
get_next_target(scheduler_t *scheduler, ticks_t curticks)
	REQUIRE_SPINLOCK(scheduler->lock)
{
	assert(scheduler != NULL);
	assert_spinlock_held(&scheduler->lock);

	thread_t *prev		    = scheduler->active_thread;
	thread_t *target	    = prev;
	bool	  timeslice_expired = false;
	index_t	  i;

	if (target != NULL) {
		timeslice_expired =
			update_timeslice(scheduler, target, curticks);
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
		scheduler->active_thread = target;
	} else {
		target			 = idle_thread();
		scheduler->active_thread = NULL;
	}

	scheduler->schedtime = curticks;

	if ((prev != NULL) && (target != prev)) {
		add_to_runqueue(scheduler, prev, timeslice_expired);
	}

	return target;
}

static bool
can_yield_to(thread_t *yield_to) REQUIRE_SCHEDULER_LOCK(yield_to)
{
	assert_preempt_disabled();

	thread_t   *current  = thread_get_self();
	cpu_index_t cpu	     = cpulocal_get_index();
	cpu_index_t affinity = yield_to->scheduler_affinity;
	bool	    yield    = true;

	// The target's affinity must be equal to this cpu or invalid.
	if (cpulocal_index_valid(affinity) && (affinity != cpu)) {
		yield = false;
		goto out;
	}

	// We can't yield to a thread if it is already running and
	// not the current thread.
	if (sched_state_get_running(&yield_to->scheduler_state) &&
	    (yield_to != current)) {
		yield = false;
		goto out;
	}

	if (!can_be_scheduled(yield_to)) {
		yield = false;
	}
out:
	return yield;
}

static thread_t *
select_yield_target(thread_t *target, bool *can_idle) REQUIRE_PREEMPT_DISABLED
{
	assert(target != NULL);
	assert(can_idle != NULL);
	assert_preempt_disabled();

	thread_t *next = target;

	CPULOCAL(yielded_from) = NULL;

	if (atomic_load_relaxed(&target->scheduler_yielding)) {
		thread_t *yield_to = target->scheduler_yield_to;
		assert(yield_to != NULL);

		scheduler_lock_nopreempt(yield_to);
		if (can_yield_to(yield_to)) {
			next		       = yield_to;
			CPULOCAL(yielded_from) = target;
			*can_idle	       = false;
		} else {
			end_directed_yield(target);
		}
		scheduler_unlock_nopreempt(yield_to);
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
		ticks_t	     curticks  = timer_get_current_timer_ticks();
		thread_t    *current   = thread_get_self();
		thread_t    *target;

		rcu_read_start();

		trigger_scheduler_schedule_event(current,
						 CPULOCAL(yielded_from),
						 scheduler->schedtime,
						 curticks);

		spinlock_acquire_nopreempt(&scheduler->lock);
		target	      = get_next_target(scheduler, curticks);
		bool can_idle = bitmap_empty(scheduler->prio_bitmap,
					     SCHEDULER_NUM_PRIORITIES);
		set_next_timeout(scheduler, target);
		spinlock_release_nopreempt(&scheduler->lock);

		target = select_yield_target(target, &can_idle);

		trigger_scheduler_selected_thread_event(target, &can_idle);

		if (target == current) {
			rcu_read_finish();
			trigger_scheduler_quiescent_event();
			must_schedule = false;
		} else if (object_get_thread_safe(target)) {
			// The reference obtained here will be released when the
			// thread stops running.
			rcu_read_finish();

			if (compiler_expected(
				    thread_switch_to(target, curticks) == OK)) {
				switched = true;
				must_schedule =
					ipi_clear(IPI_REASON_RESCHEDULE);
			} else {
				must_schedule = true;
			}
		} else {
			// Unable to obtain a reference to the target thread;
			// re-run the scheduler.
			rcu_read_finish();
			must_schedule = true;
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
	thread_t *yielded_from = CPULOCAL(yielded_from);
	if (yielded_from != NULL) {
		// End the directed yield to the current thread.
		end_directed_yield(yielded_from);
	} else {
		// Discard the rest of the current thread's timeslice.
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

	thread_t *yielded_from = CPULOCAL(yielded_from);
	if (yielded_from == target) {
		// We are trying to yield back to the thread that
		// yielded to us; end the original yield.
		end_directed_yield(yielded_from);
	} else if (yielded_from != NULL) {
		// Update the yielding thread's target.
		discard_yield_to(yielded_from);
		set_yield_to(yielded_from, target);
	} else {
#if defined(INTERFACE_VCPU)
		if ((current->kind == THREAD_KIND_VCPU) &&
		    vcpu_pending_wakeup()) {
			// The current thread has a pending wakeup;
			// skip the directed yield.
			goto out;
		}
#endif
		// Initiate a new directed yield. We must pin the current
		// thread, as allowing migration may result in the current
		// thread running simultaneously with its yield target.
		// Pinning the thread also makes accesses to the yield-to
		// pointer CPU-local for the duration of the yield, making
		// it safe to access without the thread lock.
		scheduler_lock_nopreempt(current);
		scheduler_pin(current);
		scheduler_unlock_nopreempt(current);
		set_yield_to(current, target);
		atomic_store_relaxed(&current->scheduler_yielding, true);
	}

	(void)scheduler_schedule();

	if (yielded_from == NULL) {
		discard_yield_to(current);
		scheduler_lock_nopreempt(current);
		scheduler_unpin(current);
		scheduler_unlock_nopreempt(current);
	}

#if defined(INTERFACE_VCPU)
out:
#endif
	preempt_enable();
}

void
scheduler_lock(thread_t *thread)
{
	spinlock_acquire(&thread->scheduler_lock);
}

void
scheduler_lock_nopreempt(thread_t *thread)
{
	spinlock_acquire_nopreempt(&thread->scheduler_lock);
}

void
scheduler_unlock(thread_t *thread)
{
	spinlock_release(&thread->scheduler_lock);
}

void
scheduler_unlock_nopreempt(thread_t *thread)
{
	spinlock_release_nopreempt(&thread->scheduler_lock);
}

static bool
add_thread_to_scheduler(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	assert(!sched_state_get_running(&thread->scheduler_state));
	assert(!sched_state_get_queued(&thread->scheduler_state));
	assert(!sched_state_get_exited(&thread->scheduler_state));
	assert(can_be_scheduled(thread));

	bool	    need_schedule;
	cpu_index_t affinity = thread->scheduler_affinity;

	if (cpulocal_index_valid(affinity)) {
		cpu_index_t  cpu = cpulocal_get_index();
		scheduler_t *scheduler =
			&CPULOCAL_BY_INDEX(scheduler, affinity);

		spinlock_acquire_nopreempt(&scheduler->lock);

		if (scheduler->active_thread == NULL) {
			// The newly unblocked thread is the only one runnable,
			// so a reschedule will always be needed.
			need_schedule = true;
		} else if (bitmap_empty(scheduler->prio_bitmap,
					SCHEDULER_NUM_PRIORITIES)) {
			// The scheduler's current thread was scheduled with
			// can_idle set, so it may have gone idle without
			// rescheduling. Force a reschedule regardless of
			// priority, to ensure that it doesn't needlessly block
			// a lower-priority threads.
			need_schedule = true;
		} else {
			// There is already an active thread; a reschedule is
			// needed if the newly unblocked thread has equal or
			// higher priority
			need_schedule =
				thread->scheduler_priority >=
				scheduler->active_thread->scheduler_priority;
		}

		reset_sched_params(thread);
		sched_state_set_queued(&thread->scheduler_state, true);

		// Each thread has a reference to itself which remains until it
		// exits. Since threads are not runnable after exiting, the
		// scheduler queues can safely use this reference instead of
		// getting an additional one.
		add_to_runqueue(scheduler, thread, true);

		spinlock_release_nopreempt(&scheduler->lock);

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
remove_thread_from_scheduler(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);

	cpu_index_t affinity	 = thread->scheduler_affinity;
	bool	    was_yielding = atomic_exchange_explicit(
		       &thread->scheduler_yielding, false, memory_order_relaxed);

	if (cpulocal_index_valid(affinity)) {
		assert(sched_state_get_queued(&thread->scheduler_state));

		scheduler_t *scheduler =
			&CPULOCAL_BY_INDEX(scheduler, affinity);
		bool was_active = false;

		spinlock_acquire_nopreempt(&scheduler->lock);
		if (scheduler->active_thread == thread) {
			scheduler->active_thread = NULL;
			was_active		 = true;
		} else {
			remove_from_runqueue(scheduler, thread);
		}
		spinlock_release_nopreempt(&scheduler->lock);

		sched_state_set_queued(&thread->scheduler_state, false);

		if (compiler_unexpected(was_active && was_yielding)) {
			// The thread was actively yielding; trigger a
			// reschedule to ensure the yield ends.
			if (affinity != cpulocal_get_index()) {
				ipi_one(IPI_REASON_RESCHEDULE, affinity);
			} else {
				scheduler_trigger();
			}
		}
	} else {
		// Threads with invalid affinities cannot perform directed
		// yields; as they only run via directed yields, any call to
		// scheduler_yield_to() will update the yielding thread instead.
		assert(!was_yielding);
	}
}

static bool
resched_running_thread(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	assert(sched_state_get_running(&thread->scheduler_state));
	assert(!sched_state_get_queued(&thread->scheduler_state) ||
	       sched_state_get_killed(&thread->scheduler_state));

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
start_affinity_changed_events(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
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

	scheduler_lock_nopreempt(next);
	cpu_index_t affinity	 = next->scheduler_affinity;
	thread_t   *yielded_from = CPULOCAL(yielded_from);

	// The next thread's affinity could have changed between target
	// selection and now; it may have been blocked by or is already running
	// on another CPU. Only set it running if it is still valid to do so.
	bool runnable = !sched_state_get_running(&next->scheduler_state) &&
			(can_be_scheduled(next) || (next == idle_thread()));
	bool affinity_valid =
		(affinity == cpu) ||
		(!cpulocal_index_valid(affinity) && (yielded_from != NULL));

	if (compiler_expected(runnable && affinity_valid)) {
		assert(!sched_state_get_need_requeue(&next->scheduler_state));
		assert(!sched_state_get_exited(&next->scheduler_state));
		sched_state_set_running(&next->scheduler_state, true);
		CPULOCAL(running_thread) = next;
		atomic_store_relaxed(&next->scheduler_active_affinity, cpu);
	} else {
		err = ERROR_DENIED;
		if (yielded_from != NULL) {
			end_directed_yield(yielded_from);
		}
	}
	scheduler_unlock_nopreempt(next);

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

	scheduler_lock_nopreempt(prev);
	sched_state_set_running(&prev->scheduler_state, false);

	if (sched_state_get_need_requeue(&prev->scheduler_state)) {
		// The thread may have blocked after being marked for a
		// requeue. Ensure it is still runnable prior to adding
		// it to a scheduler queue.
		if (can_be_scheduled(prev)) {
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
	scheduler_unlock_nopreempt(prev);

	if (need_schedule) {
		scheduler_trigger();
	}
}

void
scheduler_block(thread_t *thread, scheduler_block_t block)
	REQUIRE_SCHEDULER_LOCK(thread)
{
	TRACE(DEBUG, INFO,
	      "scheduler: block {:#x}, reason: {:d}, others: {:#x}",
	      (uintptr_t)thread, (register_t)block,
	      thread->scheduler_block_bits[0]);

	assert_spinlock_held(&thread->scheduler_lock);
	assert(block <= SCHEDULER_BLOCK__MAX);

	if (!bitmap_isset(thread->scheduler_block_bits, (index_t)block)) {
		trigger_scheduler_blocked_event(thread, block,
						can_be_scheduled(thread));
	}

	bitmap_set(thread->scheduler_block_bits, (index_t)block);
	if (sched_state_get_queued(&thread->scheduler_state) &&
	    !can_be_scheduled(thread)) {
		remove_thread_from_scheduler(thread);
	}
}

void
scheduler_block_init(thread_t *thread, scheduler_block_t block)
{
	assert(!sched_state_get_init(&thread->scheduler_state));
	assert(block <= SCHEDULER_BLOCK__MAX);
	bitmap_set(thread->scheduler_block_bits, (index_t)block);
}

bool
scheduler_unblock(thread_t *thread, scheduler_block_t block)
	REQUIRE_LOCK(thread->scheduler_lock)
{
	assert_spinlock_held(&thread->scheduler_lock);
	assert(block <= SCHEDULER_BLOCK__MAX);
	bool was_blocked = !can_be_scheduled(thread);
	bool block_was_set =
		bitmap_isset(thread->scheduler_block_bits, (index_t)block);
	bitmap_clear(thread->scheduler_block_bits, (index_t)block);
	bool now_runnable  = can_be_scheduled(thread);
	bool need_schedule = was_blocked && now_runnable;

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
	      (uintptr_t)thread, (register_t)block,
	      thread->scheduler_block_bits[0], (register_t)need_schedule);

	if (block_was_set) {
		trigger_scheduler_unblocked_event(thread, block, now_runnable);
	}

	return need_schedule;
}

bool
scheduler_is_blocked(const thread_t *thread, scheduler_block_t block)
{
	assert(block <= SCHEDULER_BLOCK__MAX);
	return bitmap_isset(thread->scheduler_block_bits, (index_t)block);
}

bool
scheduler_is_runnable(const thread_t *thread)
{
	return can_be_scheduled(thread);
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
	assert_spinlock_held(&thread->scheduler_lock);
	thread->scheduler_pin_count++;
}

void
scheduler_unpin(thread_t *thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	assert(thread->scheduler_pin_count > 0U);
	thread->scheduler_pin_count--;
}

cpu_index_t
scheduler_get_affinity(thread_t *thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	return thread->scheduler_affinity;
}

cpu_index_t
scheduler_get_active_affinity(thread_t *thread)
{
	assert_spinlock_held(&thread->scheduler_lock);

	cpu_index_t cpu =
		atomic_load_relaxed(&thread->scheduler_active_affinity);

	return cpulocal_index_valid(cpu) ? cpu : thread->scheduler_affinity;
}

error_t
scheduler_set_affinity(thread_t *thread, cpu_index_t target_cpu)
{
	assert_spinlock_held(&thread->scheduler_lock);

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

	err = trigger_scheduler_set_affinity_prepare_event(thread, prev_cpu,
							   target_cpu);
	if (err != OK) {
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
	REQUIRE_SCHEDULER_LOCK(thread)
{
	assert_spinlock_held(&thread->scheduler_lock);

	// If the thread is blocked, or is still running and has been marked for
	// a requeue, then it is safe to update the scheduler parameters without
	// any queue operations. If not, it first needs to be removed from its
	// queue before the update, then added back when it is safe to do so.
	bool requeue = can_be_scheduled(thread) &&
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

	assert_spinlock_held(&thread->scheduler_lock);

	// Verify the SCHEDULER_MIN_PRIORITY is configured other than 0 to add
	// another check for the 'priority' variable.
	static_assert(SCHEDULER_MIN_PRIORITY == 0U,
		      "zero minimum priority expected");

	if ((priority > SCHEDULER_MAX_PRIORITY)) {
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

	assert_spinlock_held(&thread->scheduler_lock);

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
scheduler_will_preempt_current(thread_t *thread)
{
	assert_spinlock_held(&thread->scheduler_lock);
	thread_t *current = thread_get_self();

	return (thread->scheduler_priority > current->scheduler_priority) ||
	       (current->kind == THREAD_KIND_IDLE);
}

void
scheduler_fprr_handle_thread_killed(thread_t *thread)
{
	assert(thread != NULL);

	bool need_schedule = false;

	scheduler_lock(thread);

	// Many of the block flags will be ignored once the killed
	// flag is set, so check if the thread becomes runnable.
	bool was_blocked = !can_be_scheduled(thread);
	sched_state_set_killed(&thread->scheduler_state, true);
	bool runnable = was_blocked && can_be_scheduled(thread);
	bool running  = sched_state_get_running(&thread->scheduler_state);

	if (runnable) {
		assert(!sched_state_get_queued(&thread->scheduler_state));

		if (running) {
			sched_state_set_need_requeue(&thread->scheduler_state,
						     true);
			need_schedule = resched_running_thread(thread);
		} else {
			need_schedule = add_thread_to_scheduler(thread);
		}
	} else if (running) {
		// If the thread is running remotely, we need to send
		// an IPI to ensure it exits in a timely manner.
		(void)resched_running_thread(thread);
	} else {
		// Thread is either still blocked or already
		// scheduled to run, so there is nothing to do.
	}

	scheduler_unlock_nopreempt(thread);

	if (need_schedule) {
		scheduler_trigger();
	}

	preempt_enable();
}

void
scheduler_fprr_handle_thread_exited(void)
{
	assert_preempt_disabled();

	thread_t *thread = thread_get_self();

	scheduler_lock_nopreempt(thread);

	assert(atomic_load_relaxed(&thread->state) == THREAD_STATE_EXITED);
	assert(scheduler_is_blocked(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE));
	assert(sched_state_get_running(&thread->scheduler_state));

	if (sched_state_get_killed(&thread->scheduler_state)) {
		if (sched_state_get_queued(&thread->scheduler_state)) {
			remove_thread_from_scheduler(thread);
		}
		sched_state_set_killed(&thread->scheduler_state, false);
	}

	assert(!can_be_scheduled(thread));
	assert(!sched_state_get_queued(&thread->scheduler_state));

	sched_state_set_exited(&thread->scheduler_state, true);

	scheduler_unlock_nopreempt(thread);
}
