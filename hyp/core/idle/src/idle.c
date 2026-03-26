// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <bitmap.h>
#include <bootmem.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <idle.h>
#include <ipi.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>
#include <timer_queue.h>
#include <trace.h>
#include <util.h>

#include <events/idle.h>
#include <events/object.h>

#include "event_handlers.h"
#include "idle_arch.h"

CPULOCAL_DECLARE_STATIC(thread_t *, idle_thread);

static uintptr_t idle_stack_base;

// Idle thread creation is special, since it has to happen very early for the
// boot CPU and we perform part of the boot with an incomplete idle thread
// structure. We need to manually trigger the object manager's
// object_init_thread event early, and later trigger the object_create_thread
// event.

const static thread_create_t idle_default_params = {
	.scheduler_affinity	  = 0U, // must be updated before use
	.scheduler_affinity_valid = true,
	.scheduler_priority	  = SCHEDULER_MIN_PRIORITY,
	.scheduler_priority_valid = true,
	.kind			  = THREAD_KIND_IDLE,
	.thread			  = NULL, // must be updated before use
};

extern void
thread_switch_boot_thread(thread_t *new_thread);

// Thread size is the size of the whole thread TLS area to be allocated,
// which is larger than 'struct thread'
extern const size_t thread_size;
extern const size_t thread_align;

void
idle_handle_boot_runtime_first_init(cpu_index_t boot_cpu_index)
{
	void_ptr_result_t ret;
	thread_t	 *boot_thread;

	// Allocate boot CPU idle thread and TLS out of bootmem.
	ret = bootmem_allocate(thread_size, thread_align);
	if (ret.e != OK) {
		panic("Unable to allocate boot idle thread");
	}

	// For now, we just zero-initialise the thread and TLS data and call
	// thread init event. The real setup will be done in the idle module
	// after partitions and allocators are working.
	boot_thread = (thread_t *)ret.r;

	assert(thread_size >= sizeof(*boot_thread));
	errno_t err_mem = memset_s(boot_thread, thread_size, 0, thread_size);
	if (err_mem != 0) {
		panic("Error in memset_s operation!");
	}

	thread_create_t idle_params = idle_default_params;

	idle_params.scheduler_affinity = boot_cpu_index;
	idle_params.thread	       = boot_thread;

	partition_t *hyp_partition = partition_get_private();
	trigger_object_init_thread_event(idle_params, hyp_partition);

	boot_thread->cpulocal_current_cpu = boot_cpu_index;

	// This must be the last operation in boot_runtime_first_init.
	thread_switch_boot_thread(boot_thread);
}

error_t
idle_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	if (thread_is_kind(thread, THREAD_KIND_IDLE)) {
		scheduler_block_init(thread, SCHEDULER_BLOCK_IDLE);
	}

	return OK;
}

static thread_t *
idle_thread_create(cpu_index_t i)
{
	thread_create_t idle_params    = idle_default_params;
	idle_params.scheduler_affinity = i;

	thread_ptr_result_t ret =
		partition_allocate_thread(partition_get_private(), idle_params);

	if (ret.e != OK) {
		panic("Unable to create idle thread");
	}

	return ret.r;
}

static void
idle_thread_init_boot(thread_t *thread, cpu_index_t i)
{
	thread_create_t idle_params    = idle_default_params;
	idle_params.scheduler_affinity = i;
	idle_params.thread	       = thread;

	if (trigger_object_create_thread_event(idle_params) != OK) {
		panic("Unable to create idle thread");
	}
}

void
idle_thread_init(void)
{
	// Allocate some address space for the idle stacks.
	size_t aspace_size =
		THREAD_STACK_MAP_ALIGN * ((size_t)PLATFORM_MAX_CORES + 1U);

	virt_range_result_t stack_range = hyp_aspace_allocate(aspace_size);
	if (stack_range.e != OK) {
		panic("Unable to allocate address space for idle stacks");
	}

	// Start the idle stack range at the next alignment boundary to
	// ensure we have guard pages before the first mapped stack.
	idle_stack_base =
		util_balign_up(stack_range.r.base + 1U, THREAD_STACK_MAP_ALIGN);

	const cpu_index_t cpu = cpulocal_get_index();

	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		if (!platform_cpu_exists(i)) {
			continue;
		}
		thread_t *thread_idle;

		if (cpu == i) {
			thread_t *self = thread_get_self();
			idle_thread_init_boot(self, i);
			thread_idle = self;
		} else {
			thread_idle = idle_thread_create(i);
		}

		// Each idle thread needs a single extra reference to prevent it
		// being deleted when it first starts. This is because it will
		// be switching from itself in thread_boot_set_idle(), so when
		// it releases the reference to the previous thread in
		// thread_arch_main(), it will in fact be releasing itself.
		(void)object_get_thread_additional(thread_idle);

		CPULOCAL_BY_INDEX(idle_thread, i) = thread_idle;
		if (object_activate_thread(thread_idle) != OK) {
			panic("Error activating idle thread");
		}

		assert(scheduler_is_blocked(CPULOCAL_BY_INDEX(idle_thread, i),
					    SCHEDULER_BLOCK_IDLE));
	}
}

static cpu_index_t idle_boot_cpu = CPU_INDEX_INVALID;

void
idle_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	idle_boot_cpu = boot_cpu_index;
}

static noreturn void
idle_loop(uintptr_t unused_params)
{
	// We generally run the idle thread with preemption disabled. Handlers
	// for the idle_yield event may re-enable preemption, as long as they
	// are guaranteed to stop waiting and return true if preemption occurs.
	preempt_disable();

	const cpu_index_t this_cpu = cpulocal_get_index();

	if (compiler_unexpected(this_cpu == idle_boot_cpu)) {
		// We need to do this only once
		idle_boot_cpu = CPU_INDEX_INVALID;
		trigger_idle_start_event();
	}

	assert(idle_is_current());

	(void)unused_params;

	assert(scheduler_is_blocked(thread_get_self(), SCHEDULER_BLOCK_IDLE));

	do {
		scheduler_yield();

		// If yield returned, nothing is runnable
		TRACE(INFO, INFO, "no runnable VCPUs, entering idle");

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

uintptr_t
idle_handle_thread_get_stack_base(thread_kind_t kind, thread_t *thread)
{
	assert(kind == THREAD_KIND_IDLE);
	assert(thread != NULL);

	cpu_index_t cpu = thread->scheduler_affinity;

	return idle_stack_base + ((uintptr_t)cpu * THREAD_STACK_MAP_ALIGN);
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

// Prepare to block execution on the calling CPU.
void
idle_block_start(void) LOCK_IMPL
{
	trigger_idle_block_start_event();
}

// Clean up after resuming execution after blocking on the calling CPU.
void
idle_block_finish(void) LOCK_IMPL
{
	trigger_idle_block_finish_event();
}

idle_state_t
idle_wakeup(void)
{
	idle_state_t ret;

	// Check for an immediately pending wakeup interrupt that triggers a
	// local reschedule. Note that misrouted or spurious IRQs that don't
	// cause local reschedules won't be counted here; we'll keep waiting.
	if (irq_interrupt_dispatch()) {
		ret = IDLE_STATE_RESCHEDULE;
		goto out;
	}

	// Check for a pending reschedule not directly caused by an interrupt.
	if (ipi_handle_relaxed()) {
		ret = IDLE_STATE_RESCHEDULE;
		goto out;
	}

#if (!defined(PLATFORM_IDLE_WAKEUP_NOWAIT) || !PLATFORM_IDLE_WAKEUP_NOWAIT) && \
	(PLATFORM_IDLE_WAKEUP_TIMEOUT_NS > 0)
	// Wait a while for a reschedule event to be triggered. As above,
	// misrouted or spurious IRQs don't count.
	ticks_t start_ticks = timer_get_current_timer_ticks();
	ticks_t wait_ticks  = timer_convert_ns_to_ticks(
		(nanoseconds_t)PLATFORM_IDLE_WAKEUP_TIMEOUT_NS);
	ticks_t end_ticks = start_ticks + wait_ticks;
	do {
		if (idle_arch_wait_timeout(end_ticks)) {
			ret = IDLE_STATE_RESCHEDULE;
			goto out;
		}
	} while (timer_get_current_timer_ticks() <= end_ticks);
#endif // !PLATFORM_IDLE_WAKEUP_NOWAIT

	// Still no reschedule. Give up and restart the idle handlers.
	TRACE(INFO, INFO, "spurious wakeup, entering idle");
	ret = IDLE_STATE_WAKEUP;

out:
	return ret;
}
