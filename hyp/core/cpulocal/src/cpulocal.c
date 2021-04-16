// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <trace.h>

#include "event_handlers.h"

bool
cpulocal_index_valid(cpu_index_t index)
{
	return index < PLATFORM_MAX_CORES;
}

cpu_index_t
cpulocal_check_index(cpu_index_t index)
{
	assert(cpulocal_index_valid(index));
	return index;
}

cpu_index_t
cpulocal_get_index_for_thread(const thread_t *thread)
{
	assert(thread != NULL);
	return thread->cpulocal_current_cpu;
}

cpu_index_t
cpulocal_get_index(void)
{
	const thread_t *self = thread_get_self();
	return cpulocal_check_index(cpulocal_get_index_for_thread(self));
}

void
cpulocal_handle_boot_cpu_cold_init(cpu_index_t cpu_index)
{
	thread_t *self = thread_get_self();
	assert(self != NULL);

	// Ensure that the index is set early on the primary idle thread
	self->cpulocal_current_cpu = cpulocal_check_index(cpu_index);

	// This is the earliest point at which we can call TRACE(), so let's
	// do that now to let debuggers know that the CPU is coming online.
	TRACE_LOCAL(DEBUG, INFO, "CPU {:d} coming online", cpu_index);
}

error_t
cpulocal_handle_object_create_thread(thread_create_t thread_create)
{
	// The primary idle thread calls this on itself, having already set
	// its CPU index in the boot_cpu_cold_init handler above; so check that
	// we're not about to clobber the current thread's CPU index.
	if (thread_get_self() != thread_create.thread) {
		thread_create.thread->cpulocal_current_cpu = CPU_INDEX_INVALID;
	}

	return OK;
}

void
cpulocal_handle_thread_context_switch_post(thread_t *prev)
{
	thread_t *self = thread_get_self();
	assert(self != NULL);
	cpu_index_t this_cpu = CPU_INDEX_INVALID;

#if SCHEDULER_CAN_MIGRATE
	if (compiler_unexpected(prev == self)) {
		assert(idle_thread() == prev);
		this_cpu = self->scheduler_affinity;
	} else {
		assert(prev != NULL);
		this_cpu = cpulocal_check_index(prev->cpulocal_current_cpu);
		assert((self->kind != THREAD_KIND_IDLE) ||
		       (this_cpu == self->scheduler_affinity));
	}
#else
	this_cpu = self->scheduler_affinity;
#endif

	assert(this_cpu != CPU_INDEX_INVALID);
	prev->cpulocal_current_cpu = CPU_INDEX_INVALID;
	self->cpulocal_current_cpu = this_cpu;
}
