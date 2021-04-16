// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <bootmem.h>
#include <idle.h>
#include <object.h>
#include <panic.h>
#include <refcount.h>
#include <thread.h>
#include <thread_init.h>

#include "event_handlers.h"
#include "thread_arch.h"

extern void
thread_switch_boot_thread(thread_t *new_thread);

// Thread size is the size of the whole thread TLS area to be allocated,
// which is larger than 'struct thread'
extern const size_t thread_size;
extern const size_t thread_align;

void
thread_standard_handle_boot_runtime_first_init(void)
{
	void_ptr_result_t ret;
	thread_t *	  idle_thread;

	// Allocate boot CPU idle thread and TLS out of bootmem.
	ret = bootmem_allocate(thread_size, thread_align);
	if (ret.e != OK) {
		panic("unable to allocate boot idle thread");
	}

	// For now, we just zero-initialise the thread and TLS data and init the
	// reference count. The real setup will be done in the idle module after
	// partitions and allocators are working.
	idle_thread = (thread_t *)ret.r;
	memset(idle_thread, 0, thread_size);
	refcount_init(&idle_thread->header.refcount);

	// This must be the last operation in boot_runtime_first_init.
	thread_switch_boot_thread(idle_thread);
}

void
thread_standard_handle_boot_runtime_warm_init(thread_t *idle_thread)
{
	// This must be the last operation in boot_runtime_warm_init.
	thread_switch_boot_thread(idle_thread);
}

noreturn void
thread_boot_set_idle(void)
{
	thread_t *thread = thread_get_self();
	assert(thread == idle_thread());

	// We must always take a reference to the target thread when switching,
	// even if it is the same thread that is already (partly) current. This
	// is because the generic thread start and thread switch functions will
	// always release a reference to the old thread.
	object_get_thread_additional(thread);
	thread_arch_set_thread(thread);
}
