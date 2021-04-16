// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#if !defined(NDEBUG)
#include <string.h>
#endif

#include <atomic.h>
#include <compiler.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <preempt.h>
#include <scheduler.h>
#include <thread.h>

#include <events/thread.h>

#include <asm/barrier.h>

#include "event_handlers.h"
#include "thread_arch.h"

// Externally visible for assembly
extern thread_t _Thread_local current_thread;

// The current thread
thread_t _Thread_local current_thread
	__attribute__((section(".tbss.current_thread")));

error_t
thread_standard_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	thread->kind   = thread_create.kind;
	thread->params = thread_create.params;

	size_t stack_size = (thread_create.stack_size != 0U)
				    ? thread_create.stack_size
				    : thread_stack_size_default;

	void_ptr_result_t stack = partition_alloc(
		thread->header.partition, stack_size, thread_stack_align);
	if (stack.e == OK) {
#if !defined(NDEBUG)
		// Fill the stack with a pattern so we can detect maximum stack
		// depth
		memset(stack.r, 0x57, stack_size);
#endif

		thread->stack_base = (uintptr_t)stack.r;
		thread->stack_size = stack_size;
		thread_arch_init_context(thread);
	} else {
		thread->stack_base = (uintptr_t)0U;
		thread->stack_size = (size_t)0U;
	}

	scheduler_block_init(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE);

	return stack.e;
}

void
thread_standard_unwind_object_create_thread(error_t	    result,
					    thread_create_t create)
{
	thread_t *thread = create.thread;
	assert(thread != NULL);
	assert(result != OK);
	assert(thread->state == THREAD_STATE_INIT);

	if (thread->stack_base != (uintptr_t)0U) {
		partition_free(thread->header.partition,
			       (void *)thread->stack_base, thread->stack_size);
	}
}

error_t
thread_standard_handle_object_activate_thread(thread_t *thread)
{
	assert(thread != NULL);

	// Put the thread into ready state and give it a reference to itself.
	// This reference is released in thread_exit(). At this point the thread
	// can only be deleted by another thread by calling thread_kill().
	(void)object_get_thread_additional(thread);
	thread->state = THREAD_STATE_READY;

	// Remove the lifecycle block, which allows the thread to be scheduled
	// (assuming nothing else blocked it).
	scheduler_lock(thread);
	if (scheduler_unblock(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE)) {
		scheduler_trigger();
	}
	scheduler_unlock(thread);

	return OK;
}

void
thread_standard_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);
	assert((thread->state == THREAD_STATE_INIT) ||
	       (thread->state == THREAD_STATE_EXITED));

	if (thread->stack_base != (uintptr_t)0U) {
		partition_free(thread->header.partition,
			       (void *)thread->stack_base, thread->stack_size);
	}
}

error_t
thread_standard_handle_thread_context_switch_pre(void)
{
	return OK;
}

thread_t *
thread_get_self(void)
{
	return &current_thread;
}

error_t
thread_switch_to(thread_t *thread)
{
	assert_preempt_disabled();
	assert(thread != thread_get_self());

	trigger_thread_save_state_event();
	error_t err = trigger_thread_context_switch_pre_event(thread);
	if (compiler_unexpected(err != OK)) {
		object_put_thread(thread);
		goto out;
	}

	thread_t *prev = thread_arch_switch_thread(thread);
	assert(prev != NULL);

	trigger_thread_context_switch_post_event(prev);
	object_put_thread(prev);

	trigger_thread_load_state_event(false);

	asm_context_sync_fence();
out:
	return err;
}

bool
thread_is_dying(const thread_t *thread)
{
	assert(thread != NULL);
	return thread->state == THREAD_STATE_KILLED;
}

noreturn void
thread_exit(void)
{
	thread_t *thread = thread_get_self();
	assert(thread != NULL);
	preempt_disable();

	thread->state = THREAD_STATE_EXITED;

	scheduler_lock(thread);
	scheduler_block(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE);
	scheduler_unlock(thread);

	// TODO: wake up anyone waiting in thread_join()

	// Release the thread's reference to itself (note that the CPU still
	// holds a reference, so this won't delete it immediately). This matches
	// the get in thread_create_finished().
	object_put_thread(thread);

	scheduler_yield();

	// This thread should never run again, unless it is explicitly reset
	// (which will prevent a switch returning here).
	panic("Switched to an exited thread!");
}
