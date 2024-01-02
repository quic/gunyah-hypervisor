// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <idle.h>
#include <ipi.h>
#include <log.h>
#include <object.h>
#include <preempt.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>

#include <events/scheduler.h>

#include <asm/event.h>

#include "event_handlers.h"

// Writes protected by per-CPU scheduler lock; reads protected by RCU
CPULOCAL_DECLARE_STATIC(spinlock_t, active_thread_lock);
CPULOCAL_DECLARE_STATIC(thread_t *_Atomic, active_thread);

error_t
scheduler_trivial_handle_object_create_thread(thread_create_t thread_create)
{
	thread_t *thread = thread_create.thread;
	assert(thread != NULL);

	spinlock_init(&thread->scheduler_lock);

	cpu_index_t cpu		   = thread_create.scheduler_affinity_valid
					     ? thread_create.scheduler_affinity
					     : cpulocal_get_index();
	thread->scheduler_affinity = cpu;

	return OK;
}

error_t
scheduler_trivial_handle_object_activate_thread(thread_t *thread)
{
	error_t err;

	cpu_index_t cpu = thread->scheduler_affinity;

	spinlock_acquire(&CPULOCAL_BY_INDEX(active_thread_lock, cpu));
	thread_t *_Atomic *active_thread_p =
		&CPULOCAL_BY_INDEX(active_thread, cpu);
	if (bitmap_isset(thread->scheduler_block_bits, SCHEDULER_BLOCK_IDLE)) {
		// This is the idle thread; don't make it the active thread
		err = OK;
	} else if (atomic_load_relaxed(active_thread_p) == NULL) {
		// This is the active thread; remember it
		atomic_store_relaxed(active_thread_p, thread);
		err = OK;
	} else {
		err = ERROR_BUSY;
	}
	spinlock_release(&CPULOCAL_BY_INDEX(active_thread_lock, cpu));

	return err;
}

void
scheduler_trivial_handle_object_deactivate_thread(thread_t *thread)
{
	assert(thread != NULL);

	cpu_index_t cpu = thread->scheduler_affinity;
	assert(cpulocal_index_valid(cpu));

	spinlock_acquire(&CPULOCAL_BY_INDEX(active_thread_lock, cpu));
	thread_t *_Atomic *active_thread_p =
		&CPULOCAL_BY_INDEX(active_thread, cpu);
	if (atomic_load_relaxed(active_thread_p) == thread) {
		atomic_store_relaxed(active_thread_p, NULL);
	}
	spinlock_release(&CPULOCAL_BY_INDEX(active_thread_lock, cpu));
}

void
scheduler_trivial_handle_boot_cold_init(void)
{
	for (cpu_index_t i = 0; cpulocal_index_valid(i); i++) {
		spinlock_init(&CPULOCAL_BY_INDEX(active_thread_lock, i));
	}
}

bool
scheduler_schedule(void)
{
	preempt_disable();
	bool must_schedule = true;
	bool switched	   = false;

	for (count_t i = 0; must_schedule; i++) {
#if defined(NDEBUG)
		(void)i;
#else
		const count_t reschedule_warn_limit = 16;
		if (i == reschedule_warn_limit) {
			TRACE_AND_LOG(ERROR, WARN,
				      "Possible reschedule loop on CPU {:d}",
				      cpulocal_get_index());
		}
#endif

		rcu_read_start();

		thread_t *target =
			atomic_load_consume(&CPULOCAL(active_thread));

		if ((target != NULL) && !scheduler_is_runnable(target)) {
			target = NULL;
		}

		if ((target != NULL) && !object_get_thread_safe(target)) {
			target = NULL;
		}

		if (target == NULL) {
			target = object_get_thread_additional(idle_thread());
		}

		rcu_read_finish();

		bool can_idle = true;
		trigger_scheduler_selected_thread_event(target, &can_idle);

		if (target != thread_get_self()) {
			// The reference we took above will be released when the
			// thread stops running.
			error_t err = thread_switch_to(target);
			assert(err == OK);
			switched = true;
			must_schedule =
				ipi_clear_relaxed(IPI_REASON_RESCHEDULE);
		} else {
			trigger_scheduler_quiescent_event();
			object_put_thread(target);
			must_schedule = false;
		}
	}

	preempt_enable();

	return switched;
}

void
scheduler_trigger(void)
{
	// Note that we don't need to disable preemption here; if we are
	// preempted and switch CPU, that implies that the reschedule we
	// were being called to trigger has already happened.
	//
	// This function is typically called when preemption is off anyway (as
	// scheduler_schedule() would be called otherwise).
	cpu_index_t cpu = cpulocal_get_index();
	ipi_one_relaxed(IPI_REASON_RESCHEDULE, cpu);
}

void
scheduler_yield(void)
{
	(void)scheduler_schedule();
}

void
scheduler_yield_to(thread_t *target)
{
	(void)target;
	(void)scheduler_schedule();
}

void
scheduler_lock(thread_t *thread)
{
	spinlock_acquire(&thread->scheduler_lock);
}

void
scheduler_unlock(thread_t *thread)
{
	spinlock_release(&thread->scheduler_lock);
}

void
scheduler_block(thread_t *thread, scheduler_block_t block)
{
	TRACE(DEBUG, INFO,
	      "scheduler: block {:#x}, reason: {:d}, others: {:#x}",
	      (uintptr_t)thread, block, thread->scheduler_block_bits[0]);

	assert_preempt_disabled();
	assert(block <= SCHEDULER_BLOCK__MAX);
	bool block_was_set = bitmap_isset(thread->scheduler_block_bits, block);

	if (!bitmap_isset(thread->scheduler_block_bits, block)) {
		trigger_scheduler_blocked_event(thread, block,
						scheduler_is_runnable(thread));
	}

	bitmap_set(thread->scheduler_block_bits, block);
}

void
scheduler_block_init(thread_t *thread, scheduler_block_t block)
{
	assert(block <= SCHEDULER_BLOCK__MAX);
	bitmap_set(thread->scheduler_block_bits, block);
}

bool
scheduler_trivial_handle_ipi_reschedule(void)
{
	return true;
}

bool
scheduler_unblock(thread_t *thread, scheduler_block_t block)
{
	assert_preempt_disabled();
	assert(block <= SCHEDULER_BLOCK__MAX);
	bool block_was_set = bitmap_isset(thread->scheduler_block_bits, block);
	bitmap_clear(thread->scheduler_block_bits, block);
	bool now_runnable  = scheduler_is_runnable(thread);
	bool need_schedule = block_was_set && now_runnable;

	if (need_schedule) {
		cpu_index_t cpu = cpulocal_get_index();
		if (cpu != thread->scheduler_affinity) {
			ipi_one(IPI_REASON_RESCHEDULE,
				thread->scheduler_affinity);
			need_schedule = false;
		}
	}

	TRACE(DEBUG, INFO,
	      "scheduler: unblock {:#x}, reason: {:d}, others: {:#x}, local run: {:d}",
	      (uintptr_t)thread, block, thread->scheduler_block_bits[0],
	      need_schedule);

	if (block_was_set) {
		trigger_scheduler_unblocked_event(thread, block, now_runnable);
	}

	return need_schedule;
}

bool
scheduler_is_blocked(const thread_t *thread, scheduler_block_t block)
{
	assert(block <= SCHEDULER_BLOCK__MAX);
	return bitmap_isset(thread->scheduler_block_bits, block);
}

bool
scheduler_is_runnable(const thread_t *thread)
{
	return bitmap_empty(thread->scheduler_block_bits,
			    SCHEDULER_NUM_BLOCK_BITS);
}

bool
scheduler_is_running(const thread_t *thread)
{
	bool	    ret;
	cpu_index_t cpu = thread->scheduler_affinity;

	if (!cpulocal_index_valid(cpu)) {
		ret = false;
		goto out;
	}

	thread_t *active_thread =
		atomic_load_consume(&CPULOCAL_BY_INDEX(active_thread, cpu));
	bool active_runnable = scheduler_is_runnable(active_thread);

	// Its either the active_thread or idle thread.
	if (thread == active_thread) {
		ret = active_runnable;
	} else {
		ret = !active_runnable;
		assert(thread == idle_thread_for(cpu));
	}
out:
	return ret;
}

thread_t *
scheduler_get_primary_vcpu(cpu_index_t cpu)
{
	return atomic_load_consume(&CPULOCAL_BY_INDEX(active_thread, cpu));
}

void
scheduler_pin(thread_t *thread)
{
	assert_preempt_disabled();
	(void)thread;
}

void
scheduler_unpin(thread_t *thread)
{
	assert_preempt_disabled();
	(void)thread;
}

cpu_index_t
scheduler_get_affinity(thread_t *thread)
{
	assert_preempt_disabled();

	return thread->scheduler_affinity;
}

error_t
scheduler_set_affinity(thread_t *thread, cpu_index_t target_cpu)
{
	assert_preempt_disabled();

	error_t	    err	      = OK;
	bool	    need_sync = false;
	cpu_index_t prev_cpu  = thread->scheduler_affinity;

	if (prev_cpu == target_cpu) {
		goto out;
	}

	if (!cpulocal_index_valid(target_cpu)) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	thread->scheduler_affinity = target_cpu;
	err = trigger_scheduler_set_affinity_prepare_event(thread, prev_cpu,
							   target_cpu);
	if (err != OK) {
		goto out;
	}
	trigger_scheduler_affinity_changed_event(thread, prev_cpu, target_cpu,
						 &need_sync);

out:
	return err;
}

bool
scheduler_will_preempt_current(thread_t *thread)
{
	assert_preempt_disabled();
	return false;
}
