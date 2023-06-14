// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <timer_queue.h>
#include <trace.h>
#include <util.h>

#include <events/object.h>

#include <asm/event.h>

#include "event_handlers.h"

#define NUM_AFFINITY_SWITCH 20U

#define SCHED_TEST_STACK_AREA (4U << 20)

static uintptr_t	 sched_test_stack_base;
static uintptr_t	 sched_test_stack_end;
static _Atomic uintptr_t sched_test_stack_alloc;

static _Atomic count_t sync_flag;

CPULOCAL_DECLARE_STATIC(_Atomic uint8_t, wait_flag);
CPULOCAL_DECLARE_STATIC(thread_t *, test_thread);
CPULOCAL_DECLARE_STATIC(count_t, test_passed_count);
CPULOCAL_DECLARE_STATIC(_Atomic count_t, affinity_count);

static thread_ptr_result_t
create_thread(priority_t prio, cpu_index_t cpu, sched_test_op_t op)
	REQUIRE_PREEMPT_DISABLED
{
	thread_ptr_result_t ret;

	sched_test_param_t param = sched_test_param_default();
	sched_test_param_set_parent(&param, cpulocal_get_index());
	sched_test_param_set_op(&param, op);

	thread_create_t params = {
		.scheduler_affinity	  = cpu,
		.scheduler_affinity_valid = true,
		.scheduler_priority	  = prio,
		.scheduler_priority_valid = true,
		.kind			  = THREAD_KIND_SCHED_TEST,
		.params			  = sched_test_param_raw(param),
	};

	ret = partition_allocate_thread(partition_get_private(), params);
	if (ret.e != OK) {
		goto out;
	}

	error_t err = object_activate_thread(ret.r);
	if (err != OK) {
		object_put_thread(ret.r);
		ret = thread_ptr_result_error(err);
	}

out:
	return ret;
}

static void
destroy_thread(thread_t *thread)
{
	// Wait for the thread to exit so subsequent tests do not race with it.
	while (atomic_load_relaxed(&thread->state) != THREAD_STATE_EXITED) {
		scheduler_yield_to(thread);
	}

	object_put_thread(thread);
}

static void
schedule_check_switched(thread_t *thread, bool switch_expected)
{
	thread_t *current = thread_get_self();

	preempt_disable();
	if (scheduler_schedule()) {
		// We must have expected a switch.
		assert(switch_expected);
	} else if (switch_expected) {
		// If we didn't switch, then current must have already been
		// preempted. For current to run again, the other thread must
		// have exited or is yielding to us.
		assert((thread->scheduler_yield_to == current) ||
		       (atomic_load_relaxed(&thread->state) ==
			THREAD_STATE_EXITED));
	} else {
		// Nothing to check.
	}
	preempt_enable();
}

void
tests_scheduler_init(void)
{
	virt_range_result_t range = hyp_aspace_allocate(SCHED_TEST_STACK_AREA);
	assert(range.e == OK);

	sched_test_stack_base =
		util_balign_up(range.r.base + 1U, THREAD_STACK_MAP_ALIGN);
	sched_test_stack_end = range.r.base + (range.r.size - 1U);

	atomic_init(&sched_test_stack_alloc, sched_test_stack_base);
}

bool
tests_scheduler_start(void)
{
	thread_ptr_result_t ret;
	uint8_t		    old;

	// Test 1: priorities
	// priority > default: switch on schedule
	ret = create_thread(SCHEDULER_MAX_PRIORITY, cpulocal_get_index(),
			    SCHED_TEST_OP_INCREMENT);
	assert(ret.e == OK);

	schedule_check_switched(ret.r, true);

	old = atomic_load_relaxed(&CPULOCAL(wait_flag));
	assert(old == 1U);
	atomic_store_relaxed(&CPULOCAL(wait_flag), 0U);
	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;

	// priority == default: switch on yield
	ret = create_thread(SCHEDULER_DEFAULT_PRIORITY, cpulocal_get_index(),
			    SCHED_TEST_OP_INCREMENT);
	assert(ret.e == OK);

	while (atomic_load_relaxed(&CPULOCAL(wait_flag)) == 0U) {
		scheduler_yield();
	}
	atomic_store_relaxed(&CPULOCAL(wait_flag), 0U);
	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;

	// priority < default: switch on directed yield
	ret = create_thread(SCHEDULER_MIN_PRIORITY, cpulocal_get_index(),
			    SCHED_TEST_OP_INCREMENT);
	assert(ret.e == OK);

	schedule_check_switched(ret.r, false);

	while (atomic_load_relaxed(&CPULOCAL(wait_flag)) == 0U) {
		scheduler_yield_to(ret.r);
	}
	atomic_store_relaxed(&CPULOCAL(wait_flag), 0U);
	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;

	// Test 2: wait for timeslice expiry
	ret = create_thread(SCHEDULER_DEFAULT_PRIORITY, cpulocal_get_index(),
			    SCHED_TEST_OP_WAKE);
	assert(ret.e == OK);

	// Yield to reset the current thread's timeslice, then wait for the
	// other thread to run and update the wait flag.
	scheduler_yield();
	_Atomic uint8_t *wait_flag = &CPULOCAL(wait_flag);
	atomic_store_relaxed(wait_flag, 1U);
	preempt_enable();
	while (asm_event_load_before_wait(wait_flag) == 1U) {
		asm_event_wait(wait_flag);
	}
	preempt_disable();

	assert(atomic_load_relaxed(&CPULOCAL(wait_flag)) == 0U);
	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;

	// Test 3: double directed yield
	ret = create_thread(SCHEDULER_MIN_PRIORITY, CPU_INDEX_INVALID,
			    SCHED_TEST_OP_INCREMENT);
	assert(ret.e == OK);
	CPULOCAL(test_thread) = ret.r;

	ret = create_thread(SCHEDULER_MIN_PRIORITY + 1U, cpulocal_get_index(),
			    SCHED_TEST_OP_YIELDTO);
	assert(ret.e == OK);

	schedule_check_switched(ret.r, false);

	atomic_store_relaxed(&CPULOCAL(wait_flag), 1U);
	while (atomic_load_relaxed(&CPULOCAL(wait_flag)) == 1U) {
		scheduler_yield_to(ret.r);
	}
	atomic_store_relaxed(&CPULOCAL(wait_flag), 0U);

	destroy_thread(ret.r);
	destroy_thread(CPULOCAL(test_thread));
	CPULOCAL(test_passed_count)++;

#if SCHEDULER_CAN_MIGRATE
	error_t err;

	// Test 4: set affinity & yield to
	ret = create_thread(SCHEDULER_MAX_PRIORITY, CPU_INDEX_INVALID,
			    SCHED_TEST_OP_YIELDTO);
	assert(ret.e == OK);

	schedule_check_switched(ret.r, false);

	CPULOCAL(test_thread) = thread_get_self();
	scheduler_lock_nopreempt(ret.r);
	err = scheduler_set_affinity(ret.r, cpulocal_get_index());
	scheduler_unlock_nopreempt(ret.r);
	assert(err == OK);

	schedule_check_switched(ret.r, true);

	scheduler_yield_to(ret.r);
	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;

	(void)atomic_fetch_add_explicit(&sync_flag, 1U, memory_order_relaxed);
	while (asm_event_load_before_wait(&sync_flag) < PLATFORM_MAX_CORES) {
		asm_event_wait(&sync_flag);
	}

	// Test 5: migrate running thread
	ret = create_thread(SCHEDULER_DEFAULT_PRIORITY, cpulocal_get_index(),
			    SCHED_TEST_OP_AFFINITY);
	assert(ret.e == OK);

	while (atomic_load_relaxed(&CPULOCAL(affinity_count)) <
	       NUM_AFFINITY_SWITCH) {
		scheduler_yield();
		scheduler_lock_nopreempt(ret.r);
		cpu_index_t affinity = (scheduler_get_affinity(ret.r) + 1U) %
				       PLATFORM_MAX_CORES;
		err = scheduler_set_affinity(ret.r, affinity);
		scheduler_unlock_nopreempt(ret.r);
		assert((err == OK) || (err == ERROR_RETRY));
	}

	// Ensure the thread is running on the current CPU so we can yield to it
	// and ensure it exits.
	do {
		scheduler_lock_nopreempt(ret.r);
		err = scheduler_set_affinity(ret.r, cpulocal_get_index());
		scheduler_unlock_nopreempt(ret.r);
		assert((err == OK) || (err == ERROR_RETRY));
	} while (err == ERROR_RETRY);

	destroy_thread(ret.r);
	CPULOCAL(test_passed_count)++;
#endif

	return false;
}

static void
sched_test_thread_entry(uintptr_t param)
{
	cpulocal_begin();

	sched_test_param_t test_param = sched_test_param_cast((uint32_t)param);
	sched_test_op_t	   op	      = sched_test_param_get_op(&test_param);

	switch (op) {
	case SCHED_TEST_OP_INCREMENT:
		(void)atomic_fetch_add_explicit(&CPULOCAL(wait_flag), 1U,
						memory_order_relaxed);
		break;
	case SCHED_TEST_OP_WAKE: {
		_Atomic uint8_t *wait_flag = &CPULOCAL(wait_flag);
		cpulocal_end();
		while (asm_event_load_before_wait(wait_flag) == 0U) {
			asm_event_wait(wait_flag);
		}
		asm_event_store_and_wake(wait_flag, 0U);
		cpulocal_begin();
		break;
	}
	case SCHED_TEST_OP_YIELDTO:
		while (atomic_load_relaxed(&CPULOCAL(wait_flag)) == 1U) {
			scheduler_yield_to(CPULOCAL(test_thread));
		}
		break;
	case SCHED_TEST_OP_AFFINITY: {
		cpu_index_t parent = sched_test_param_get_parent(&test_param);
		_Atomic count_t *aff_count =
			&CPULOCAL_BY_INDEX(affinity_count, parent);
		while (atomic_load_relaxed(aff_count) < NUM_AFFINITY_SWITCH) {
			(void)atomic_fetch_add_explicit(aff_count, 1U,
							memory_order_relaxed);
			scheduler_yield();
		}
		break;
	}
	default:
		panic("Invalid param for sched test thread!");
	}

	cpulocal_end();
}

thread_func_t
sched_test_get_entry_fn(thread_kind_t kind)
{
	assert(kind == THREAD_KIND_SCHED_TEST);

	return sched_test_thread_entry;
}

uintptr_t
sched_test_get_stack_base(thread_kind_t kind, thread_t *thread)
{
	assert(kind == THREAD_KIND_SCHED_TEST);
	assert(thread != NULL);

	size_t	  stack_area = THREAD_STACK_MAP_ALIGN;
	uintptr_t stack_base = atomic_fetch_add_explicit(
		&sched_test_stack_alloc, stack_area, memory_order_relaxed);

	assert(stack_base >= sched_test_stack_base);
	assert((stack_base + (stack_area - 1U)) <= sched_test_stack_end);

	return stack_base;
}
#else

extern char unused;

#endif
