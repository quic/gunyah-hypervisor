// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <bitmap.h>
#include <cpulocal.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <spinlock.h>

#include <asm/event.h>

#include "event_handlers.h"

#define TEST_ITERATIONS 100

extern test_info_t test_info;
test_info_t	   test_info;
extern test_info_t test_spinlock_multi_info;
test_info_t	   test_spinlock_multi_info;
extern test_info_t test_spinlock_multi_lock[PLATFORM_MAX_CORES];
test_info_t	   test_spinlock_multi_lock[PLATFORM_MAX_CORES];

#if defined(UNIT_TESTS)
void
tests_spinlock_single_lock_init(void)
{
	// Initialize spinlock with boot cpu
	spinlock_init(&test_info.lock);
	test_info.count = 0;
}
#endif

// Only one core can increment the count at the time.
bool
tests_spinlock_single_lock(void)
{
	bool ret		  = false;
	bool wait_all_cores_start = true;
	bool wait_all_cores_end	  = true;

	spinlock_acquire_nopreempt(&test_info.lock);
	test_info.count++;
	spinlock_release_nopreempt(&test_info.lock);

	// Wait until all cores have reached this point to start.
	while (wait_all_cores_start) {
		spinlock_acquire_nopreempt(&test_info.lock);

		if (!cpulocal_index_valid((cpu_index_t)test_info.count)) {
			wait_all_cores_start = false;
		}

		spinlock_release_nopreempt(&test_info.lock);
	}

	for (count_t i = 0; i < TEST_ITERATIONS; i++) {
		spinlock_acquire_nopreempt(&test_info.lock);
		test_info.count++;
		spinlock_release_nopreempt(&test_info.lock);
	}

	// If test succeeds, count should be (TEST_ITERATIONS *
	// PLATFORM_MAX_CORES) + PLATFORM_MAX_CORES
	while (wait_all_cores_end) {
		spinlock_acquire_nopreempt(&test_info.lock);

		if (test_info.count == ((TEST_ITERATIONS * PLATFORM_MAX_CORES) +
					PLATFORM_MAX_CORES)) {
			wait_all_cores_end = false;
		}

		spinlock_release_nopreempt(&test_info.lock);
	}

	return ret;
}

#if defined(UNIT_TESTS)
void
tests_spinlock_multiple_locks_init(void)
{
	// Initialize spinlocks with boot cpu
	spinlock_init(&test_spinlock_multi_info.lock);
	test_spinlock_multi_info.count = 0;

	for (int i = 0; cpulocal_index_valid((cpu_index_t)i); i++) {
		spinlock_init(&test_spinlock_multi_lock[i].lock);
		test_spinlock_multi_lock[i].count = 0;
	}
}
#endif

// Dinning philosophers example
// Only philosophers that hold two forks can be eating at the same time.
// To avoid deadlock the odd and even numbers start picking differently.
bool
tests_spinlock_multiple_locks(void)
{
	bool ret		  = false;
	bool wait_all_cores_start = true;
	bool wait_all_cores_end	  = true;

	const cpu_index_t cpu = cpulocal_get_index();

	spinlock_acquire_nopreempt(&test_spinlock_multi_info.lock);
	test_spinlock_multi_info.count++;
	spinlock_release_nopreempt(&test_spinlock_multi_info.lock);

	index_t left  = cpu;
	index_t right = cpu + 1;

	if (cpu == (PLATFORM_MAX_CORES - 1)) {
		right = 0;
	}

	// Wait until all cores have reached this point to start.
	while (wait_all_cores_start) {
		spinlock_acquire_nopreempt(&test_spinlock_multi_info.lock);

		if (test_spinlock_multi_info.count == PLATFORM_MAX_CORES) {
			wait_all_cores_start = false;
		}

		spinlock_release_nopreempt(&test_spinlock_multi_info.lock);
	}

	for (count_t i = 0; i < TEST_ITERATIONS; i++) {
		if ((cpu % 2) == 0U) {
			spinlock_acquire_nopreempt(
				&test_spinlock_multi_lock[left].lock);
			spinlock_acquire_nopreempt(
				&test_spinlock_multi_lock[right].lock);
		} else {
			spinlock_acquire_nopreempt(
				&test_spinlock_multi_lock[right].lock);
			spinlock_acquire_nopreempt(
				&test_spinlock_multi_lock[left].lock);
		}

		test_spinlock_multi_lock[left].count++;
		test_spinlock_multi_lock[right].count++;

		spinlock_release_nopreempt(
			&test_spinlock_multi_lock[left].lock);
		spinlock_release_nopreempt(
			&test_spinlock_multi_lock[right].lock);
	}

	// If test succeeds, each fork count should be (2 * TEST_ITERATIONS)
	while (wait_all_cores_end) {
		spinlock_acquire_nopreempt(
			&test_spinlock_multi_lock[left].lock);

		if (test_spinlock_multi_lock[left].count ==
		    (2 * TEST_ITERATIONS)) {
			wait_all_cores_end = false;
		}

		spinlock_release_nopreempt(
			&test_spinlock_multi_lock[left].lock);
	}

	return ret;
}
