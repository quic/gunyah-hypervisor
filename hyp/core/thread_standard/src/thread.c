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
#include <trace.h>
#include <util.h>

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
	error_t	  err	 = OK;
	thread_t *thread = thread_create.thread;

	assert(thread != NULL);

	thread->kind   = thread_create.kind;
	thread->params = thread_create.params;

	size_t stack_size = (thread_create.stack_size != 0U)
				    ? thread_create.stack_size
				    : thread_stack_size_default;
	if (stack_size > THREAD_STACK_MAX_SIZE) {
		err = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (!util_is_baligned(stack_size, thread_stack_alloc_align)) {
		err = ERROR_ARGUMENT_ALIGNMENT;
		goto out;
	}

	void_ptr_result_t stack = partition_alloc(
		thread->header.partition, stack_size, thread_stack_alloc_align);
	if (stack.e != OK) {
		err = stack.e;
		goto out;
	}

#if !defined(NDEBUG)
	// Fill the stack with a pattern so we can detect maximum stack
	// depth
	(void)memset_s(stack.r, stack_size, 0x57, stack_size);
#endif

	thread->stack_mem  = (uintptr_t)stack.r;
	thread->stack_size = stack_size;

	scheduler_block_init(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE);

out:
	return err;
}

void
thread_standard_unwind_object_create_thread(error_t	    result,
					    thread_create_t create)
{
	thread_t *thread = create.thread;
	assert(thread != NULL);
	assert(result != OK);
	assert(atomic_load_relaxed(&thread->state) == THREAD_STATE_INIT);

	if (thread->stack_mem != 0U) {
		(void)partition_free(thread->header.partition,
				     (void *)thread->stack_mem,
				     thread->stack_size);
		thread->stack_mem = 0U;
	}
}

error_t
thread_standard_handle_object_activate_thread(thread_t *thread)
{
	error_t err = OK;

	assert(thread != NULL);

	// Get an appropriate address for the stack and map it there.
	thread->stack_base =
		trigger_thread_get_stack_base_event(thread->kind, thread);
	if (thread->stack_base == 0U) {
		err = ERROR_NOMEM;
		goto out;
	}

	assert(util_is_baligned(thread->stack_base, THREAD_STACK_MAP_ALIGN));

	err = thread_arch_map_stack(thread);
	if (err != OK) {
		thread->stack_base = 0U;
		goto out;
	}

	thread_arch_init_context(thread);

	// Put the thread into ready state and give it a reference to itself.
	// This reference is released in thread_exit(). At this point the thread
	// can only be deleted by another thread by calling thread_kill().
	(void)object_get_thread_additional(thread);
	atomic_store_relaxed(&thread->state, THREAD_STATE_READY);

	// Remove the lifecycle block, which allows the thread to be scheduled
	// (assuming nothing else blocked it).
	scheduler_lock(thread);
	if (scheduler_unblock(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE)) {
		scheduler_trigger();
	}
	scheduler_unlock(thread);
out:
	return err;
}

void
thread_standard_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	thread_state_t state = atomic_load_relaxed(&thread->state);
	assert((state == THREAD_STATE_INIT) || (state == THREAD_STATE_EXITED));

	if (thread->stack_base != 0U) {
		thread_arch_unmap_stack(thread);
		thread->stack_base = 0U;
	}

	if (thread->stack_mem != 0U) {
		(void)partition_free(thread->header.partition,
				     (void *)thread->stack_mem,
				     thread->stack_size);
		thread->stack_mem = 0U;
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
thread_switch_to(thread_t *thread, ticks_t schedtime)
{
	assert_preempt_disabled();

	thread_t *current = thread_get_self();
	assert(thread != current);

	TRACE_LOCAL(INFO, INFO, "thread: ctx switch from: {:#x} to: {:#x}",
		    (uintptr_t)current, (uintptr_t)thread);

	trigger_thread_save_state_event();
	error_t err =
		trigger_thread_context_switch_pre_event(thread, schedtime);
	if (compiler_unexpected(err != OK)) {
		object_put_thread(thread);
		goto out;
	}

	ticks_t	  prevticks = schedtime;
	thread_t *prev	    = thread_arch_switch_thread(thread, &schedtime);
	assert(prev != NULL);

	trigger_thread_context_switch_post_event(prev, schedtime, prevticks);
	object_put_thread(prev);

	trigger_thread_load_state_event(false);

	asm_context_sync_fence();
out:
	return err;
}

error_t
thread_kill(thread_t *thread)
{
	assert(thread != NULL);

	error_t	       err;
	thread_state_t expected_state = THREAD_STATE_READY;
	if (atomic_compare_exchange_strong_explicit(
		    &thread->state, &expected_state, THREAD_STATE_KILLED,
		    memory_order_relaxed, memory_order_relaxed)) {
		trigger_thread_killed_event(thread);
		err = OK;
	} else if ((expected_state == THREAD_STATE_KILLED) ||
		   (expected_state == THREAD_STATE_EXITED)) {
		// Thread was already killed, or has exited
		err = OK;
	} else {
		// Thread had not started yet
		err = ERROR_OBJECT_STATE;
	}

	return err;
}

bool
thread_is_dying(const thread_t *thread)
{
	assert(thread != NULL);
	return atomic_load_relaxed(&thread->state) == THREAD_STATE_KILLED;
}

bool
thread_has_exited(const thread_t *thread)
{
	assert(thread != NULL);
	return atomic_load_relaxed(&thread->state) == THREAD_STATE_EXITED;
}

noreturn void
thread_exit(void)
{
	thread_t *thread = thread_get_self();
	assert(thread != NULL);
	preempt_disable();

	atomic_store_relaxed(&thread->state, THREAD_STATE_EXITED);

	scheduler_lock_nopreempt(thread);
	scheduler_block(thread, SCHEDULER_BLOCK_THREAD_LIFECYCLE);
	scheduler_unlock_nopreempt(thread);

	// TODO: wake up anyone waiting in thread_join()

	trigger_thread_exited_event();

	// Release the thread's reference to itself (note that the CPU still
	// holds a reference, so this won't delete it immediately). This matches
	// the get in thread_create_finished().
	object_put_thread(thread);

	scheduler_yield();

	// This thread should never run again, unless it is explicitly reset
	// (which will prevent a switch returning here).
	panic("Switched to an exited thread!");
}

void
thread_standard_handle_thread_exit_to_user(void)
{
	thread_t *thread = thread_get_self();
	assert(thread != NULL);

	thread_state_t state = atomic_load_relaxed(&thread->state);
	if (compiler_unexpected(state == THREAD_STATE_KILLED)) {
		thread_exit();
	} else {
		assert(state == THREAD_STATE_READY);
	}
}
