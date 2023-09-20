// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <allocator.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <log.h>
#include <memdb.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <partition_init.h>
#include <preempt.h>
#include <scheduler.h>
#include <trace.h>
#include <util.h>

#include <events/tests.h>

#include "event_handlers.h"

static uintptr_t test_thread_stack_base;

#if defined(UNIT_TESTS)
static thread_t *
tests_thread_create(cpu_index_t i)
{
	thread_create_t params = {
		.scheduler_affinity	  = i,
		.scheduler_affinity_valid = true,
		.kind			  = THREAD_KIND_TEST,
		.params			  = i,
		.stack_size		  = THREAD_STACK_MAX_SIZE,
	};

	thread_ptr_result_t ret =
		partition_allocate_thread(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Unable to create test thread");
	}

	if (object_activate_thread(ret.r) != OK) {
		panic("Error activating test thread");
	}

	return ret.r;
}
#endif

error_t
tests_handle_object_create_thread(thread_create_t create)
{
	thread_t *thread = create.thread;
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_TEST) {
		scheduler_block_init(thread, SCHEDULER_BLOCK_TEST);
	}

	return OK;
}

#if defined(UNIT_TESTS)
static void
tests_add_root_partition_heap(void)
{
	// Grab some kernel heap from the hypervisor_partition and give it to
	// the root partition allocator.
	void_ptr_result_t ret;
	size_t		  root_alloc_size = 0x20000;

	partition_t *hyp_partition  = partition_get_private();
	partition_t *root_partition = partition_get_root();

	ret = partition_alloc(hyp_partition, root_alloc_size, 4096U);
	if (ret.e != OK) {
		panic("Error allocating root partition heap");
	}

	paddr_t root_alloc_base =
		partition_virt_to_phys(hyp_partition, (uintptr_t)ret.r);

	error_t err = memdb_update(hyp_partition, root_alloc_base,
				   root_alloc_base + (root_alloc_size - 1U),
				   (uintptr_t)root_partition,
				   MEMDB_TYPE_PARTITION,
				   (uintptr_t)&hyp_partition->allocator,
				   MEMDB_TYPE_ALLOCATOR);
	if (err != OK) {
		panic("Error adding root partition heap memory");
	}

	err = partition_map_and_add_heap(root_partition, root_alloc_base,
					 root_alloc_size);
	if (err != OK) {
		panic("Error mapping root partition heap memory");
	}
}
#endif

static void
tests_alloc_stack_space(void)
{
	size_t aspace_size = THREAD_STACK_MAP_ALIGN * (PLATFORM_MAX_CORES + 1);

	virt_range_result_t stack_range = hyp_aspace_allocate(aspace_size);
	if (stack_range.e != OK) {
		panic("Unable to allocate address space for test thread stacks");
	}

	// Start the idle stack range at the next alignment boundary.
	test_thread_stack_base =
		util_balign_up(stack_range.r.base + 1U, THREAD_STACK_MAP_ALIGN);
}

void
tests_thread_init(void)
{
#if defined(UNIT_TESTS)
	tests_add_root_partition_heap();
#endif
	tests_alloc_stack_space();

	trigger_tests_init_event();

#if defined(UNIT_TESTS)
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		thread_t *thread = tests_thread_create(i);
		scheduler_lock(thread);
		scheduler_unblock(thread, SCHEDULER_BLOCK_TEST);
		scheduler_unlock(thread);

		// The thread has a reference to itself now (until it exits); we
		// don't need to hold onto it.
		object_put_thread(thread);
	}
#endif
}

static void
tests_main(uintptr_t cpu_index)
{
	preempt_disable();
	if (trigger_tests_start_event()) {
		panic("Tests are failing.");
	} else {
		LOG(DEBUG, INFO, "Tests completed successfully on CPU {:d}",
		    cpu_index);
	}
	preempt_enable();
}

thread_func_t
tests_handle_thread_get_entry_fn(thread_kind_t kind)
{
	assert(kind == THREAD_KIND_TEST);

	return tests_main;
}

uintptr_t
tests_handle_thread_get_stack_base(thread_kind_t kind, thread_t *thread)
{
	assert(kind == THREAD_KIND_TEST);
	assert(thread != NULL);

	cpu_index_t cpu = thread->scheduler_affinity;

	return test_thread_stack_base + (cpu * THREAD_STACK_MAP_ALIGN);
}
