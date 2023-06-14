// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <ipi.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_cpu.h>
#include <platform_timer.h>
#include <preempt.h>
#include <spinlock.h>
#include <timer_queue.h>
#include <util.h>

#include <events/timer.h>

#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(timer_queue_t, timer_queue);

void
timer_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	// Initialise all timer queues here as online CPUs may try to move
	// timers to CPUs that have not booted yet
	// Secondary CPUs will be set online by `power_cpu_online()` handler
	for (cpu_index_t cpu_index = 0U; cpu_index < PLATFORM_MAX_CORES;
	     cpu_index++) {
		timer_queue_t *tq = &CPULOCAL_BY_INDEX(timer_queue, cpu_index);
		spinlock_init(&tq->lock);
		list_init(&tq->list);
		tq->timeout = TIMER_INVALID_TIMEOUT;
		tq->online  = (cpu_index == boot_cpu_index);
	}
}

#if !defined(UNITTESTS) || !UNITTESTS
void
timer_handle_rootvm_init(hyp_env_data_t *hyp_env)
{
	hyp_env->timer_freq = timer_get_timer_frequency();
}
#endif

uint32_t
timer_get_timer_frequency(void)
{
	return platform_timer_get_frequency();
}

ticks_t
timer_get_current_timer_ticks(void)
{
	return platform_timer_get_current_ticks();
}

ticks_t
timer_convert_ns_to_ticks(nanoseconds_t ns)
{
	return platform_convert_ns_to_ticks(ns);
}

nanoseconds_t
timer_convert_ticks_to_ns(ticks_t ticks)
{
	return platform_convert_ticks_to_ns(ticks);
}

static bool
is_timeout_a_smaller_than_b(list_node_t *node_a, list_node_t *node_b)
{
	ticks_t timeout_a = timer_container_of_list_node(node_a)->timeout;
	ticks_t timeout_b = timer_container_of_list_node(node_b)->timeout;

	return timeout_a < timeout_b;
}

void
timer_init_object(timer_t *timer, timer_action_t action)
{
	assert(timer != NULL);

	timer->timeout = TIMER_INVALID_TIMEOUT;
	timer->action  = action;
	atomic_init(&timer->queue, NULL);
}

bool
timer_is_queued(timer_t *timer)
{
	assert(timer != NULL);

	return atomic_load_relaxed(&timer->queue) != NULL;
}

ticks_t
timer_queue_get_next_timeout(void)
{
	timer_queue_t *tq = &CPULOCAL(timer_queue);
	ticks_t	       timeout;

	spinlock_acquire_nopreempt(&tq->lock);
	timeout = tq->timeout;
	spinlock_release_nopreempt(&tq->lock);

	return timeout;
}

static void
timer_update_timeout(timer_queue_t *tq) REQUIRE_SPINLOCK(tq->lock)
{
	assert_preempt_disabled();
	assert(tq == &CPULOCAL(timer_queue));

	if (tq->timeout != TIMER_INVALID_TIMEOUT) {
		platform_timer_set_timeout(tq->timeout);
	} else {
		platform_timer_cancel_timeout();
	}
}

static void
timer_enqueue_internal(timer_queue_t *tq, timer_t *timer, ticks_t timeout)
	REQUIRE_SPINLOCK(tq->lock)
{
	assert_preempt_disabled();
	assert(tq == &CPULOCAL(timer_queue));
	assert(tq->online);

	// Set the timer's queue pointer. We need acquire ordering to ensure we
	// observe any previous dequeues on other CPUs.
	timer_queue_t *old_tq = NULL;
	if (!atomic_compare_exchange_strong_explicit(&timer->queue, &old_tq, tq,
						     memory_order_acquire,
						     memory_order_relaxed)) {
		// This timer is already queued; it is the caller's
		// responsibility to avoid this.
		panic("Request to enqueue a timer that is already queued");
	}

	// There is no need to check if the timeout is already in the past, as
	// the timer module generates a level-triggered interrupt if the timer
	// condition is already met.
	timer->timeout = timeout;

	bool new_head = list_insert_in_order(&tq->list, &timer->list_node,
					     is_timeout_a_smaller_than_b);
	if (new_head) {
		tq->timeout = timeout;
		timer_update_timeout(tq);
	}
}

static bool
timer_dequeue_internal(timer_queue_t *tq, timer_t *timer)
	REQUIRE_SPINLOCK(tq->lock)
{
	assert_preempt_disabled();

	bool new_timeout = false;

	// The timer may have expired between loading the timer's queue and
	// acquiring its lock. Ensure the timer's queue has not changed before
	// dequeuing.
	if (compiler_expected(atomic_load_relaxed(&timer->queue) == tq)) {
		bool new_head = list_delete_node(&tq->list, &timer->list_node);
		if (new_head) {
			list_node_t *head = list_get_head(&tq->list);
			tq->timeout =
				timer_container_of_list_node(head)->timeout;
			new_timeout = true;
		} else if (list_is_empty(&tq->list)) {
			tq->timeout = TIMER_INVALID_TIMEOUT;
			new_timeout = true;
		} else {
			// The queue's timeout has not changed.
		}

		// Clear the timer's queue pointer. We need release ordering to
		// ensure this dequeue is observed by the next enqueue.
		atomic_store_release(&timer->queue, NULL);
	}

	return new_timeout;
}

static void
timer_update_internal(timer_queue_t *tq, timer_t *timer, ticks_t timeout)
	REQUIRE_SPINLOCK(tq->lock)
{
	assert_preempt_disabled();
	assert(tq == &CPULOCAL(timer_queue));
	assert(tq->online);

	if (compiler_unexpected(tq != atomic_load_relaxed(&timer->queue))) {
		// There is a race with timer updates; it is the caller's
		// responsibility to prevent this.
		panic("Request to update a timer that is not queued on this CPU");
	}

	if (compiler_expected(timer->timeout != timeout)) {
		// There is no need to check if the timeout is already in the
		// past, as the timer module generates a level-triggered
		// interrupt if the timer condition is already met.

		// Delete timer from queue, update it, and add it again to queue

		bool new_head_delete =
			list_delete_node(&tq->list, &timer->list_node);

		timer->timeout = timeout;

		bool new_head_insert =
			list_insert_in_order(&tq->list, &timer->list_node,
					     is_timeout_a_smaller_than_b);

		if (new_head_delete || new_head_insert) {
			list_node_t *head = list_get_head(&tq->list);
			tq->timeout =
				timer_container_of_list_node(head)->timeout;
			timer_update_timeout(tq);
		}
	}
}

void
timer_enqueue(timer_t *timer, ticks_t timeout)
{
	assert(timer != NULL);

	preempt_disable();

	timer_queue_t *tq = &CPULOCAL(timer_queue);

	spinlock_acquire_nopreempt(&tq->lock);
	timer_enqueue_internal(tq, timer, timeout);
	spinlock_release_nopreempt(&tq->lock);

	preempt_enable();
}

void
timer_dequeue(timer_t *timer)
{
	assert(timer != NULL);

	timer_queue_t *tq = atomic_load_relaxed(&timer->queue);

	if (tq != NULL) {
		spinlock_acquire(&tq->lock);
		if (timer_dequeue_internal(tq, timer) &&
		    (tq == &CPULOCAL(timer_queue))) {
			timer_update_timeout(tq);
		}
		spinlock_release(&tq->lock);
	}
}

void
timer_update(timer_t *timer, ticks_t timeout)
{
	assert(timer != NULL);

	preempt_disable();

	timer_queue_t *old_tq = atomic_load_relaxed(&timer->queue);
	timer_queue_t *new_tq = &CPULOCAL(timer_queue);

	// If timer is queued on another CPU, it needs to be dequeued.
	if ((old_tq != NULL) && (old_tq != new_tq)) {
		spinlock_acquire_nopreempt(&old_tq->lock);
		(void)timer_dequeue_internal(old_tq, timer);
		spinlock_release_nopreempt(&old_tq->lock);
	}

	spinlock_acquire_nopreempt(&new_tq->lock);
	if (old_tq == new_tq) {
		timer_update_internal(new_tq, timer, timeout);
	} else {
		timer_enqueue_internal(new_tq, timer, timeout);
	}
	spinlock_release_nopreempt(&new_tq->lock);

	preempt_enable();
}

static void
timer_dequeue_expired(void) REQUIRE_PREEMPT_DISABLED
{
	ticks_t	       current_ticks = timer_get_current_timer_ticks();
	timer_queue_t *tq	     = &CPULOCAL(timer_queue);

	assert_preempt_disabled();

	spinlock_acquire_nopreempt(&tq->lock);

	while (tq->timeout <= current_ticks) {
		list_node_t *head  = list_get_head(&tq->list);
		timer_t	    *timer = timer_container_of_list_node(head);
		(void)timer_dequeue_internal(tq, timer);
		spinlock_release_nopreempt(&tq->lock);
		(void)trigger_timer_action_event(timer->action, timer);
		spinlock_acquire_nopreempt(&tq->lock);
	}

	timer_update_timeout(tq);
	spinlock_release_nopreempt(&tq->lock);
}

void
timer_handle_platform_timer_expiry(void)
{
	timer_dequeue_expired();
}

error_t
timer_handle_power_cpu_suspend(void)
{
	// TODO: Delay or reject attempted suspend if timeout is due to expire
	// sooner than the CPU can reach the requested power state.

#if defined(MODULE_CORE_TIMER_LP) && MODULE_CORE_TIMER_LP
	// The timer_lp module will enqueue the timeout on the global low power
	// timer, so we can cancel the core-local timer to avoid redundant
	// interrupts if the suspend finishes without entering a state that
	// stops the timer.
	platform_timer_cancel_timeout();
#endif

	return OK;
}

// Also handles power_cpu_resume
void
timer_handle_power_cpu_online(void)
{
	timer_dequeue_expired();

	// Mark this CPU timer queue as online
	timer_queue_t *tq = &CPULOCAL(timer_queue);
	assert_preempt_disabled();
	spinlock_acquire_nopreempt(&tq->lock);
	tq->online = true;
	spinlock_release_nopreempt(&tq->lock);
}

// A timer_queue operation has occurred that requires synchronisation, process
// our timer queue. Handle any expired timers as the timer might have expired
// since it was queued on this CPU and reprogram the platform timer if required.
bool NOINLINE
timer_handle_ipi_received(void)
{
	timer_dequeue_expired();

	return true;
}

static bool
timer_try_move_to_cpu(timer_t *timer, cpu_index_t target)
	REQUIRE_PREEMPT_DISABLED
{
	bool	       moved = false;
	timer_queue_t *ttq   = &CPULOCAL_BY_INDEX(timer_queue, target);

	assert_preempt_disabled();

	spinlock_acquire_nopreempt(&ttq->lock);

	// We can only use active CPU timer queues
	if (ttq->online) {
		// Update the timer queue to be on the new CPU
		timer_queue_t *old_ttq = NULL;
		if (!atomic_compare_exchange_strong_explicit(
			    &timer->queue, &old_ttq, ttq, memory_order_acquire,
			    memory_order_relaxed)) {
			panic("Request to move timer that is already queued");
		}

		// Call IPI if the queue HEAD changed so the target CPU can
		// update its local timer
		bool new_head =
			list_insert_in_order(&ttq->list, &timer->list_node,
					     is_timeout_a_smaller_than_b);
		if (new_head) {
			ttq->timeout = timer->timeout;
			spinlock_release_nopreempt(&ttq->lock);
			ipi_one(IPI_REASON_TIMER_QUEUE_SYNC, target);
		} else {
			spinlock_release_nopreempt(&ttq->lock);
		}
		moved = true;
	} else {
		spinlock_release_nopreempt(&ttq->lock);
	}

	return moved;
}

void
timer_handle_power_cpu_offline(void)
{
	// Try to move any timers to the next one up from this one.
	// If this is the last core, wrap around
	cpu_index_t our_index = cpulocal_get_index();
	cpu_index_t start =
		(cpu_index_t)((our_index + 1U) % PLATFORM_MAX_CORES);
	timer_queue_t *tq = &CPULOCAL(timer_queue);

	assert_preempt_disabled();
	spinlock_acquire_nopreempt(&tq->lock);

	// Mark this CPU timer queue as going down and cancel any pending timers
	tq->online = false;
	platform_timer_cancel_timeout();

	// Move all active timers in this CPU timer queue to an active CPU
	while (tq->timeout != TIMER_INVALID_TIMEOUT) {
		list_node_t *head  = list_get_head(&tq->list);
		timer_t	    *timer = timer_container_of_list_node(head);

		// Remove timer from this core.
		(void)timer_dequeue_internal(tq, timer);
		spinlock_release_nopreempt(&tq->lock);

		// The target core might go down while we are searching, so
		// always check if the target is active. Hopefully the target
		// Queue stays online so we check the last-used CPU first. If
		// we cannot find any active timer queues we panic. In reality
		// at least one CPU timer queue should always be online.
		bool	    found_target = false;
		cpu_index_t target	 = start;
		while (!found_target) {
			if (platform_cpu_exists(target)) {
				if (timer_try_move_to_cpu(timer, target)) {
					found_target = true;
					start	     = target;
					break;
				}
			}

			// Skip our CPU as we know we are going down
			// This could happen if the previously saved core is
			// down now and the initial search wrapped.
			target = (cpu_index_t)((target + 1U) %
					       PLATFORM_MAX_CORES);
			if (target == our_index) {
				target = (cpu_index_t)((target + 1U) %
						       PLATFORM_MAX_CORES);
			}
			if (target == start) {
				// we looped around without finding a target,
				// this should never happen.
				break;
			}
		}

		if (!found_target) {
			panic("Could not find target CPU for timer migration");
		}

		// Get the lock back to check the next timer
		spinlock_acquire_nopreempt(&tq->lock);
	}

	spinlock_release_nopreempt(&tq->lock);
}
