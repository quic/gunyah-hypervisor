// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <events/idle.h>
#include <events/object.h>

#include "event_handlers.h"
#include "idle_arch.h"

CPULOCAL_DECLARE_STATIC(thread_t *, idle_thread);

static thread_t *
idle_thread_create(cpu_index_t i)
{
	thread_create_t params = {
		.scheduler_affinity	  = i,
		.scheduler_affinity_valid = true,
		.kind			  = THREAD_KIND_IDLE,
	};

	thread_ptr_result_t ret =
		partition_allocate_thread(partition_get_private(), params);

	if (ret.e != OK) {
		panic("Unable to create idle thread");
	}

	return ret.r;
}

error_t
idle_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	if (thread->kind == THREAD_KIND_IDLE) {
		scheduler_block_init(thread, SCHEDULER_BLOCK_IDLE);
	}

	return OK;
}

static void
idle_thread_init_boot(thread_t *thread, cpu_index_t i)
{
	thread_create_t params = {
		.scheduler_affinity	  = i,
		.scheduler_affinity_valid = true,
		.kind			  = THREAD_KIND_IDLE,
	};

	// Open-coded partition_allocate_thread minus the actual allocation,
	// which is done out of early bootmem in thread_early_init(), and the
	// refcount init which is done at the same time.
	partition_t *hyp_partition = partition_get_private();
	thread->header.partition =
		object_get_partition_additional(hyp_partition);
	thread->header.type = OBJECT_TYPE_THREAD;
	atomic_init(&thread->header.state, OBJECT_STATE_INIT);
	params.thread = thread;

	if (trigger_object_create_thread_event(params) != OK) {
		panic("Unable to create idle thread");
	}
}

void
idle_thread_init(void)
{
	const cpu_index_t cpu = cpulocal_get_index();

	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		thread_t *idle_thread;

		if (cpu == i) {
			thread_t *self = thread_get_self();
			idle_thread_init_boot(self, i);
			idle_thread = self;
		} else {
			idle_thread = idle_thread_create(i);
		}

		CPULOCAL_BY_INDEX(idle_thread, i) = idle_thread;
		if (object_activate_thread(idle_thread) != OK) {
			panic("Error activating idle thread");
		}

		assert(scheduler_is_blocked(CPULOCAL_BY_INDEX(idle_thread, i),
					    SCHEDULER_BLOCK_IDLE));
	}
}

extern void *aarch64_boot_stack;

static cpu_index_t boot_cpu = CPU_INDEX_INVALID;

void
idle_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	boot_cpu = boot_cpu_index;
}

void
idle_handle_idle_start(void)
{
	partition_t *private = partition_get_private();

	size_t stack_size = BOOT_STACK_SIZE;

	// Free the boot stack
	// Find a better place to free the boot stack
	error_t err = partition_add_heap(
		private,
		partition_virt_to_phys(private, (uintptr_t)&aarch64_boot_stack),
		stack_size);
	if (err != OK) {
		panic("Error freeing stack to hypervisor partition");
	}
}

static noreturn void
idle_loop(uintptr_t unused_params)
{
	const cpu_index_t this_cpu = cpulocal_get_index();

	if (compiler_unexpected(this_cpu == boot_cpu)) {
		// We need to do this only once
		boot_cpu = CPU_INDEX_INVALID;
		trigger_idle_start_event();
	}

	assert(idle_is_current());

	(void)unused_params;

	// We generally run the idle thread with preemption disabled. Handlers
	// for the idle_yield event may re-enable preemption, as long as they
	// are guaranteed to stop waiting and return true if preemption occurs.
	preempt_disable();

	assert(scheduler_is_blocked(thread_get_self(), SCHEDULER_BLOCK_IDLE));

	do {
		scheduler_yield();

		// If yield returned, nothing is runnable
		TRACE(DEBUG, INFO, "no runnable VCPUs, entering idle");

		while (!idle_yield()) {
			// Retry until an IRQ or other wakeup event occurs
		}
	} while (1);
}

thread_func_t
idle_handle_thread_get_entry_fn(thread_kind_t kind)
{
	assert(kind == THREAD_KIND_IDLE);
	return idle_loop;
}

thread_t *
idle_thread(void)
{
	return CPULOCAL(idle_thread);
}

thread_t *
idle_thread_for(cpu_index_t cpu_index)
{
	return CPULOCAL_BY_INDEX(idle_thread, cpu_index);
}

bool
idle_is_current(void)
{
	return thread_get_self() == CPULOCAL(idle_thread);
}

bool
idle_yield(void)
{
	assert_preempt_disabled();

	bool	     must_schedule;
	idle_state_t state = trigger_idle_yield_event(idle_is_current());

	switch (state) {
	case IDLE_STATE_IDLE:
		must_schedule = idle_arch_wait();
		break;
	case IDLE_STATE_WAKEUP:
		must_schedule = false;
		break;
	case IDLE_STATE_RESCHEDULE:
		must_schedule = true;
		break;
	default:
		panic("Invalid idle state");
	}

	return must_schedule;
}

#if !defined(UNIT_TESTS)
idle_state_t
idle_handle_vcpu_idle_fastpath(void)
{
	thread_t *   current	= thread_get_self();
	idle_state_t idle_state = IDLE_STATE_WAKEUP;

	while (!current->vcpu_interrupted) {
		// Idle until an IRQ or other wakeup event occurs
		if (idle_yield()) {
			idle_state = IDLE_STATE_RESCHEDULE;
			break;
		}
	}

	return idle_state;
}
#endif
