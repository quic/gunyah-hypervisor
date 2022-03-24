// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <cpulocal.h>
#include <ipi.h>
#include <preempt.h>
#include <rcu.h>
#include <spinlock.h>
#include <task_queue.h>

#include <events/task_queue.h>

#include "event_handlers.h"

CPULOCAL_DECLARE_STATIC(task_queue_entry_t, task_queue_head);
CPULOCAL_DECLARE_STATIC(spinlock_t, task_queue_lock);

void
task_queue_handle_boot_cpu_cold_init(cpu_index_t cpu)
{
	spinlock_init(&CPULOCAL_BY_INDEX(task_queue_lock, cpu));
	task_queue_entry_t *head = &CPULOCAL_BY_INDEX(task_queue_head, cpu);
	task_queue_entry_bf_set_prev(&head->bf, head);
	task_queue_entry_bf_set_next(&head->bf, head);
	task_queue_entry_bf_set_class(&head->bf, TASK_QUEUE_CLASS_HEAD);
	task_queue_entry_bf_set_cpu(&head->bf, cpu);
}

void
task_queue_init(task_queue_entry_t *entry, task_queue_class_t task_class)
{
	entry->bf = task_queue_entry_bf_default();
	task_queue_entry_bf_set_class(&entry->bf, task_class);
	task_queue_entry_bf_set_cpu(&entry->bf, PLATFORM_MAX_CORES);
}

error_t
task_queue_schedule(task_queue_entry_t *entry)
{
	error_t err;

	// The entry must not be queued already.
	if (task_queue_entry_bf_get_cpu(&entry->bf) < PLATFORM_MAX_CORES) {
		err = ERROR_BUSY;
		goto out;
	}

	cpulocal_begin();
	cpu_index_t cpu = cpulocal_get_index();
	spinlock_acquire_nopreempt(&CPULOCAL_BY_INDEX(task_queue_lock, cpu));

	task_queue_entry_t *head = &CPULOCAL_BY_INDEX(task_queue_head, cpu);
	task_queue_entry_t *tail = task_queue_entry_bf_get_prev(&head->bf);

	task_queue_entry_bf_set_cpu(&entry->bf, cpu);
	task_queue_entry_bf_set_next(&entry->bf, head);
	task_queue_entry_bf_set_prev(&entry->bf, tail);
	task_queue_entry_bf_set_prev(&head->bf, entry);
	task_queue_entry_bf_set_next(&tail->bf, entry);

	spinlock_release_nopreempt(&CPULOCAL_BY_INDEX(task_queue_lock, cpu));
	cpulocal_end();

	ipi_one_relaxed(IPI_REASON_TASK_QUEUE, cpu);

	err = OK;
out:
	return err;
}

// Cancel future execution of a given task queue entry.
//
// Note that this does not cancel execution if it has already started. Any
// execution that has already started is not guaranteed to be complete until an
// RCU grace period has elapsed. Also, the entry may not be safely freed until
// an RCU grace period has elapsed.
error_t
task_queue_cancel(task_queue_entry_t *entry)
{
	error_t err;

	cpu_index_t cpu = task_queue_entry_bf_get_cpu(&entry->bf);

	if (cpu >= PLATFORM_MAX_CORES) {
		err = ERROR_IDLE;
		goto out;
	}

	spinlock_acquire(&CPULOCAL_BY_INDEX(task_queue_lock, cpu));

	task_queue_entry_t *next = task_queue_entry_bf_get_next(&entry->bf);
	task_queue_entry_t *prev = task_queue_entry_bf_get_prev(&entry->bf);

	task_queue_entry_bf_set_next(&prev->bf, next);
	task_queue_entry_bf_set_prev(&next->bf, prev);

	spinlock_release(&CPULOCAL_BY_INDEX(task_queue_lock, cpu));

	task_queue_entry_bf_set_prev(&entry->bf, NULL);
	task_queue_entry_bf_set_next(&entry->bf, NULL);
	task_queue_entry_bf_set_cpu(&entry->bf, PLATFORM_MAX_CORES);

	err = OK;
out:
	return err;
}

bool
task_queue_handle_ipi_received(void)
{
	assert_preempt_disabled();

	// Ensure that no deleted objects are freed while this handler is
	// running. Note that the pointers don't need the usual RCU barriers
	// because they are protected by the queue spinlock.
	rcu_read_start();

	cpu_index_t	    cpu	 = cpulocal_get_index();
	task_queue_entry_t *head = &CPULOCAL_BY_INDEX(task_queue_head, cpu);
	spinlock_t	   *lock = &CPULOCAL_BY_INDEX(task_queue_lock, cpu);

	spinlock_acquire_nopreempt(lock);
	task_queue_entry_t *entry = task_queue_entry_bf_get_next(&head->bf);
	while (entry != head) {
		// Remove the entry from the list
		task_queue_class_t task_class =
			task_queue_entry_bf_get_class(&entry->bf);
		task_queue_entry_t *next =
			task_queue_entry_bf_get_next(&entry->bf);
		task_queue_entry_bf_set_next(&head->bf, next);
		task_queue_entry_bf_set_prev(&next->bf, head);

		// Release the lock so deletions on other cores don't block,
		// and so we can safely queue tasks in the execute handler
		spinlock_release_nopreempt(lock);

		// Clear out the entry so it can be reused
		task_queue_init(entry, task_class);

		// Execute the task
		error_t err =
			trigger_task_queue_execute_event(task_class, entry);
		assert(err == OK);

		// Re-acquire the lock and find the next entry.
		spinlock_acquire_nopreempt(lock);
		entry = task_queue_entry_bf_get_next(&head->bf);
	}
	spinlock_release_nopreempt(lock);

	rcu_read_finish();

	return true;
}
