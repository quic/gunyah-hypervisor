// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <cpulocal.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_timer.h>
#include <preempt.h>
#include <timer_queue.h>
#include <util.h>

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
	return platform_timer_convert_ns_to_ticks(ns);
}

nanoseconds_t
timer_convert_ticks_to_ns(ticks_t ticks)
{
	return platform_timer_convert_ticks_to_ns(ticks);
}

void
timer_init_object(timer_t *timer)
{
	assert(timer != NULL);

	timer->prev    = NULL;
	timer->next    = NULL;
	timer->timeout = TIMER_INVALID_TIMEOUT;
}

void
timer_enqueue(timer_t *timer, ticks_t timeout, timer_action_t action)
{
	(void)timer;
	(void)timeout;
	(void)action;
}

void
timer_dequeue(timer_t *timer)
{
	(void)timer;
}

void
timer_update(timer_t *timer, ticks_t timeout)
{
	(void)timer;
	(void)timeout;
}
