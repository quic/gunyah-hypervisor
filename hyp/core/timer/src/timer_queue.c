// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <compiler.h>
#include <cpulocal.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_timer.h>
#include <preempt.h>
#include <spinlock.h>
#include <timer_queue.h>
#include <util.h>

#include <events/timer.h>

#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(timer_queue_t, timer_queue);

void
timer_handle_boot_cpu_cold_init(cpu_index_t cpu_index)
{
	timer_queue_t *tq = &CPULOCAL_BY_INDEX(timer_queue, cpu_index);
	tq->timeout	  = TIMER_INVALID_TIMEOUT;
	list_init(&tq->list);
	spinlock_init(&tq->lock);
}

#if !defined(UNITTESTS) || !UNITTESTS
void
timer_handle_rootvm_init(boot_env_data_t *env_data)
{
	env_data->timer_freq = timer_get_timer_frequency();
}
#endif

uint32_t
timer_get_timer_frequency()
{
	return platform_timer_get_frequency();
}

ticks_t
timer_get_current_timer_ticks()
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
	bool smaller = false;

	ticks_t timeout_a =
		timer_container_of_timer_queue_list_node(node_a)->timeout;
	ticks_t timeout_b =
		timer_container_of_timer_queue_list_node(node_b)->timeout;

	if (timeout_a < timeout_b) {
		smaller = true;
	}

	return smaller;
}

void
timer_init_object(timer_t *timer, timer_action_t action)
{
	assert(timer != NULL);

	timer->timer_queue = NULL;
	timer->timeout	   = TIMER_INVALID_TIMEOUT;
	timer->action	   = action;
	timer->queued	   = false;
	spinlock_init(&timer->lock);
}

bool
timer_is_queued(timer_t *timer)
{
	assert(timer != NULL);

	bool queued = false;
	spinlock_acquire(&timer->lock);
	timer_queue_t *tq = timer->timer_queue;
	if (tq == NULL) {
		goto out;
	}

	spinlock_acquire_nopreempt(&tq->lock);
	if (timer->queued) {
		queued = true;
	} else {
		timer->timer_queue = NULL;
	}
	spinlock_release_nopreempt(&tq->lock);
out:
	spinlock_release(&timer->lock);
	return queued;
}

ticks_t
timer_queue_get_next_timeout(void)
{
	timer_queue_t *tq = &CPULOCAL(timer_queue);
	ticks_t	       timeout;

	spinlock_acquire(&tq->lock);
	timeout = tq->timeout;
	spinlock_release(&tq->lock);

	return timeout;
}

static void
timer_update_timeout(timer_queue_t *tq) REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	if (tq == &CPULOCAL(timer_queue)) {
		if (tq->timeout != TIMER_INVALID_TIMEOUT) {
			platform_timer_set_timeout(tq->timeout);
		} else {
			platform_timer_cancel_timeout();
		}
	}
}

static void
timer_enqueue_internal(timer_t *timer, ticks_t timeout) REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	if (compiler_unexpected(timer->timer_queue != NULL)) {
		// This timer object already belongs to a queue
		panic("Request to enqueue a timer that is already queued");
	}

	timer_queue_t *tq = &CPULOCAL(timer_queue);

	// There is no need to check if the timeout is already in the past, as
	// the timer module generates a level-triggered interrupt if the timer
	// condition is already met.

	timer->timeout	   = timeout;
	timer->timer_queue = tq;
	timer->queued	   = true;

	bool new_head = list_insert_in_order(&tq->list,
					     &timer->timer_queue_list_node,
					     is_timeout_a_smaller_than_b);

	if (new_head) {
		tq->timeout = timeout;
	}
}

static void
timer_dequeue_internal(timer_t *timer, bool timer_locked)
	REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	timer_queue_t *tq = timer->timer_queue;

	if (compiler_unexpected(tq == NULL)) {
		// The timer object is not in a queue
		panic("Request to dequeue a timer that is not in a queue");
	}

	bool new_head =
		list_delete_node(&tq->list, &timer->timer_queue_list_node);

	if (new_head) {
		list_node_t *head = list_get_head(&tq->list);
		ticks_t	     timeout =
			timer_container_of_timer_queue_list_node(head)->timeout;

		tq->timeout = timeout;

	} else if (list_is_empty(&tq->list)) {
		tq->timeout = TIMER_INVALID_TIMEOUT;
	}

	if (timer_locked) {
		timer->timer_queue = NULL;
	}

	timer->queued = false;
}

static void
timer_update_internal(timer_t *timer, ticks_t timeout) REQUIRE_PREEMPT_DISABLED
{
	assert_preempt_disabled();

	timer_queue_t *tq = &CPULOCAL(timer_queue);

	if (compiler_unexpected(tq != timer->timer_queue)) {
		// The timer object is not queued on this CPU
		panic("Request to update a timer that is not queued on this CPU");
	}

	if (compiler_expected(timer->timeout != timeout)) {
		// There is no need to check if the timeout is already in the
		// past, as the timer module generates a level-triggered
		// interrupt if the timer condition is already met.

		// Delete timer from queue, update it, and add it again to queue

		bool new_head_delete = list_delete_node(
			&tq->list, &timer->timer_queue_list_node);

		timer->timeout = timeout;

		bool new_head_insert = list_insert_in_order(
			&tq->list, &timer->timer_queue_list_node,
			is_timeout_a_smaller_than_b);

		if (new_head_delete || new_head_insert) {
			list_node_t *head = list_get_head(&tq->list);
			ticks_t	     head_timeout =
				timer_container_of_timer_queue_list_node(head)
					->timeout;

			tq->timeout = head_timeout;
		}
	}
}

void
timer_enqueue(timer_t *timer, ticks_t timeout)
{
	assert(timer != NULL);

	preempt_disable();
	timer_queue_t *tq = &CPULOCAL(timer_queue);

	spinlock_acquire_nopreempt(&timer->lock);
	if (timer->timer_queue != NULL) {
		spinlock_acquire_nopreempt(&timer->timer_queue->lock);
		if (compiler_unexpected(timer->queued)) {
			panic("Request to enqueue a queued timer");
		}
		spinlock_release_nopreempt(&timer->timer_queue->lock);
		timer->timer_queue = NULL;
	}

	spinlock_acquire_nopreempt(&tq->lock);
	timer_enqueue_internal(timer, timeout);
	timer_update_timeout(tq);

	spinlock_release_nopreempt(&tq->lock);
	spinlock_release_nopreempt(&timer->lock);
	preempt_enable();
}

void
timer_dequeue(timer_t *timer)
{
	assert(timer != NULL);

	spinlock_acquire(&timer->lock);

	timer_queue_t *tq = timer->timer_queue;
	if (tq == NULL) {
		goto out;
	}

	spinlock_acquire_nopreempt(&tq->lock);
	if (timer->queued) {
		timer_dequeue_internal(timer, true);
		timer_update_timeout(tq);
	} else {
		timer->timer_queue = NULL;
	}
	spinlock_release_nopreempt(&tq->lock);
out:
	spinlock_release(&timer->lock);
}

void
timer_update(timer_t *timer, ticks_t timeout)
{
	assert(timer != NULL);

	spinlock_acquire(&timer->lock);

	timer_queue_t *tq = timer->timer_queue;

	// If timer is queued on another CPU, it needs to be dequeued.
	if ((tq != NULL) && (tq != &CPULOCAL(timer_queue))) {
		spinlock_acquire_nopreempt(&tq->lock);
		if (timer->queued) {
			timer_dequeue_internal(timer, true);
		} else {
			timer->timer_queue = NULL;
		}
		spinlock_release_nopreempt(&tq->lock);
	}

	tq = &CPULOCAL(timer_queue);

	spinlock_acquire_nopreempt(&tq->lock);
	if (timer->timer_queue != NULL) {
		if (timer->queued) {
			timer_update_internal(timer, timeout);
		} else {
			timer->timer_queue = NULL;
			timer_enqueue_internal(timer, timeout);
		}
	} else {
		timer_enqueue_internal(timer, timeout);
	}

	timer_update_timeout(tq);

	spinlock_release_nopreempt(&tq->lock);
	spinlock_release(&timer->lock);
}

static void
timer_dequeue_expired(void) REQUIRE_PREEMPT_DISABLED
{
	ticks_t	       current_ticks = timer_get_current_timer_ticks();
	timer_queue_t *tq	     = &CPULOCAL(timer_queue);

	assert_preempt_disabled();

	spinlock_acquire_nopreempt(&tq->lock);

	while (tq->timeout <= current_ticks) {
		list_node_t *head = list_get_head(&tq->list);
		timer_t *timer = timer_container_of_timer_queue_list_node(head);
		timer_dequeue_internal(timer, false);
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
}
