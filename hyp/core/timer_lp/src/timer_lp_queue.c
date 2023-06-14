// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <cpulocal.h>
#include <ipi.h>
#include <list.h>
#include <platform_timer_lp.h>
#include <preempt.h>
#include <spinlock.h>
#include <timer_queue.h>

#include "event_handlers.h"

static spinlock_t	timer_lp_queue_lock;
static timer_lp_queue_t timer_lp_queue PROTECTED_BY(timer_lp_queue_lock);

CPULOCAL_DECLARE_STATIC(timer_lp_t, timer_lp);

void
timer_lp_queue_handle_boot_cold_init(void)
{
	spinlock_init(&timer_lp_queue_lock);
	spinlock_acquire(&timer_lp_queue_lock);
	timer_lp_queue.timeout = TIMER_INVALID_TIMEOUT;
	list_init(&timer_lp_queue.list);
	spinlock_release(&timer_lp_queue_lock);
}

void
timer_lp_queue_handle_boot_cpu_cold_init(cpu_index_t cpu_index)
{
	timer_lp_t *timer = &CPULOCAL_BY_INDEX(timer_lp, cpu_index);
	timer->timeout	  = TIMER_INVALID_TIMEOUT;
	timer->cpu_index  = cpu_index;
}

static bool
is_timeout_a_smaller_than_b(list_node_t *node_a, list_node_t *node_b)
{
	bool smaller = false;

	ticks_t timeout_a = timer_lp_container_of_node(node_a)->timeout;
	ticks_t timeout_b = timer_lp_container_of_node(node_b)->timeout;

	if (timeout_a < timeout_b) {
		smaller = true;
	}

	return smaller;
}

static void
timer_lp_enqueue(timer_lp_t *timer, ticks_t timeout)
	REQUIRE_SPINLOCK(timer_lp_queue_lock)
{
	timer->timeout = timeout;

	bool new_head = list_insert_in_order(&timer_lp_queue.list, &timer->node,
					     is_timeout_a_smaller_than_b);

	if (new_head) {
		timer_lp_queue.timeout = timer->timeout;
		platform_timer_lp_set_timeout_and_route(timer->timeout,
							timer->cpu_index);
	}
}

static bool
timer_lp_dequeue(timer_lp_t *timer) REQUIRE_SPINLOCK(timer_lp_queue_lock)
{
	bool new_head = list_delete_node(&timer_lp_queue.list, &timer->node);
	bool need_update;

	if (new_head) {
		list_node_t *head	= list_get_head(&timer_lp_queue.list);
		timer_lp_t  *head_timer = timer_lp_container_of_node(head);

		timer_lp_queue.timeout = head_timer->timeout;
		need_update	       = true;
	} else if (list_is_empty(&timer_lp_queue.list)) {
		timer_lp_queue.timeout = TIMER_INVALID_TIMEOUT;
		need_update	       = true;
	} else {
		need_update = false;
	}

	timer->timeout = TIMER_INVALID_TIMEOUT;

	return need_update;
}

static void
timer_lp_queue_save_arch_timer(void) REQUIRE_SPINLOCK(timer_lp_queue_lock)
{
	// Get the next timeout of the local arch timer queue

	ticks_t timeout = timer_queue_get_next_timeout();
	if (timeout == TIMER_INVALID_TIMEOUT) {
		goto out;
	}

	timer_lp_t *timer = &CPULOCAL(timer_lp);
	assert(timer->timeout == TIMER_INVALID_TIMEOUT);

	timer_lp_enqueue(timer, timeout);

out:
	return;
}

error_t
timer_lp_handle_power_cpu_suspend(void)
{
	assert_preempt_disabled();

	// TODO: Delay or reject attempted suspend if timeout is due to expire
	// sooner than the CPU can reach the requested power state.

	spinlock_acquire_nopreempt(&timer_lp_queue_lock);

	timer_lp_queue_save_arch_timer();

	spinlock_release_nopreempt(&timer_lp_queue_lock);

	return OK;
}

static void
timer_lp_sync(bool force_update) REQUIRE_SPINLOCK(timer_lp_queue_lock)
{
	cpu_index_t cpu_index	  = cpulocal_get_index();
	ticks_t	    current_ticks = platform_timer_lp_get_current_ticks();
	bool	    do_update	  = force_update;

	assert_preempt_disabled();

	while (timer_lp_queue.timeout <= current_ticks) {
		list_node_t *head  = list_get_head(&timer_lp_queue.list);
		timer_lp_t  *timer = timer_lp_container_of_node(head);

		(void)timer_lp_dequeue(timer);
		// We just dequeued the head, so always update the timer
		do_update = true;

		if (timer->cpu_index != cpu_index) {
			ipi_one(IPI_REASON_RESCHEDULE, timer->cpu_index);
		}
	}

	if (!do_update) {
		// Queue head didn't change, nothing to do
	} else if (timer_lp_queue.timeout == TIMER_INVALID_TIMEOUT) {
		// Queue is now empty
		platform_timer_lp_cancel_timeout();
	} else {
		// Schedule the next timeout
		list_node_t *head	= list_get_head(&timer_lp_queue.list);
		timer_lp_t  *head_timer = timer_lp_container_of_node(head);
		platform_timer_lp_set_timeout_and_route(head_timer->timeout,
							head_timer->cpu_index);
	}
}

static void
timer_lp_queue_restore_arch_timer(void) REQUIRE_SPINLOCK(timer_lp_queue_lock)
{
	timer_lp_t *timer = &CPULOCAL(timer_lp);
	if (timer->timeout == TIMER_INVALID_TIMEOUT) {
		goto out;
	}

	if (timer_lp_dequeue(timer)) {
		timer_lp_sync(true);
	}

out:
	return;
}

void
timer_lp_handle_power_cpu_resume(void)
{
	assert_preempt_disabled();

	spinlock_acquire_nopreempt(&timer_lp_queue_lock);

	timer_lp_queue_restore_arch_timer();

	spinlock_release_nopreempt(&timer_lp_queue_lock);
}

void
timer_lp_handle_platform_timer_lp_expiry(void)
{
	spinlock_acquire_nopreempt(&timer_lp_queue_lock);

	timer_lp_sync(false);

	spinlock_release_nopreempt(&timer_lp_queue_lock);
}
