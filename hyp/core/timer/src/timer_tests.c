// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <panic.h>
#include <timer_queue.h>
#include <trace.h>

#include "event_handlers.h"

#define MAX_TICKS_DIFFERENCE 0x100

CPULOCAL_DECLARE_STATIC(timer_t, timer1);
CPULOCAL_DECLARE_STATIC(timer_t, timer2);
CPULOCAL_DECLARE_STATIC(uint8_t, test_num);
CPULOCAL_DECLARE_STATIC(_Atomic bool, in_progress);
CPULOCAL_DECLARE_STATIC(ticks_t, expected_timeout);

bool
tests_timer()
{
	ticks_t	 current_ticks;
	timer_t *timer1		  = &CPULOCAL(timer1);
	timer_t *timer2		  = &CPULOCAL(timer2);
	ticks_t *expected_timeout = &CPULOCAL(expected_timeout);

	// Test 1
	// Enqueue a timer and make sure its expiry is received
	CPULOCAL(test_num) = 1;
	timer_init_object(timer1, TIMER_ACTION_TEST);
	timer_init_object(timer2, TIMER_ACTION_TEST);
	current_ticks = timer_get_current_timer_ticks();
	*expected_timeout =
		current_ticks + (0x100000 * (cpulocal_get_index() + 1));
	atomic_store_relaxed(&CPULOCAL(in_progress), true);
	timer_enqueue(timer1, *expected_timeout);

	while (atomic_load_relaxed(&CPULOCAL(in_progress)))
		;

	// Test 2
	// Enqueue two timers, dequeue the first one and make sure only the
	// expiry for the second one is received
	CPULOCAL(test_num)++;
	timer_init_object(timer1, TIMER_ACTION_TEST);
	timer_init_object(timer2, TIMER_ACTION_TEST);
	current_ticks = timer_get_current_timer_ticks();
	atomic_store_relaxed(&CPULOCAL(in_progress), true);
	timer_enqueue(timer1,
		      current_ticks + (0x100000 * (cpulocal_get_index() + 1)));

	*expected_timeout =
		current_ticks + (0x200000 * (cpulocal_get_index() + 1));
	timer_enqueue(timer2, *expected_timeout);

	timer_dequeue(timer1);

	while (atomic_load_relaxed(&CPULOCAL(in_progress)))
		;

	// TODO: Add more tests

	LOG(DEBUG, INFO, "Timer tests successfully finished on core {:d}",
	    cpulocal_get_index());
	return false;
}

bool
tests_timer_action(timer_t *timer)
{
	ticks_t *expected_timeout = &CPULOCAL(expected_timeout);
	ticks_t	 current_ticks	  = timer_get_current_timer_ticks();

	assert(timer != NULL);

	if (!atomic_load_relaxed(&CPULOCAL(in_progress))) {
		LOG(ERROR, PANIC,
		    "Unexpected timer expiry trigger on core {:d}",
		    cpulocal_get_index());
		panic("Unexpected timer expiry trigger");
	} else if (timer->timeout != *expected_timeout) {
		LOG(ERROR, PANIC,
		    "Timer expiry trigger (test {:d}) on core {:d}"
		    " arrived for the wrong timer; expected {:#x}, got {:#x}",
		    CPULOCAL(test_num), cpulocal_get_index(), *expected_timeout,
		    timer->timeout);
		panic("Timer expiry trigger arrived with wrong timeout");
	} else if (*expected_timeout > current_ticks) {
		LOG(ERROR, PANIC,
		    "Timer expiry trigger (test {:d}) on core {:d}"
		    " arrived too early; expected at {:#x}, arrived at {:#x}",
		    CPULOCAL(test_num), cpulocal_get_index(), *expected_timeout,
		    current_ticks);
		panic("Timer expiry trigger arrived too early");
	} else if (current_ticks - *expected_timeout > MAX_TICKS_DIFFERENCE) {
		LOG(ERROR, PANIC,
		    "Timer expiry trigger (test {:d}) on core {:d}"
		    " took too long to arrive; expected at"
		    "{:#x}, arrived at {:#x}, diff {:#x}",
		    CPULOCAL(test_num), cpulocal_get_index(), *expected_timeout,
		    current_ticks, current_ticks - *expected_timeout);
		panic("Timer expiry trigger arrived too late");
	} else {
		LOG(DEBUG, INFO,
		    "Timer interrupt (test {:d}): core {:d}, expected at {:#x}"
		    ", arrived at {:#x}, diff {:#x}",
		    CPULOCAL(test_num), cpulocal_get_index(), *expected_timeout,
		    current_ticks, current_ticks - *expected_timeout);
		atomic_store_relaxed(&CPULOCAL(in_progress), false);
	}

	return true;
}
#else

extern char unused;

#endif
