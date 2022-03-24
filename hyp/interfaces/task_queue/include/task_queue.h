// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Configure a task queue entry with a specific class. This class should
// identify the container type and element of the entry.
void
task_queue_init(task_queue_entry_t *entry, task_queue_class_t task_class);

// Schedule future execution of a given task queue entry.
//
// All calls to this function and to task_queue_cancel() for the same entry must
// be serialised by the caller.
//
// The caller also must ensure that the entry is not freed until the task has
// executed. This can be done in either of the following ways:
//
// 1. Take a reference to the object containing the entry before calling this
//    function, and release it in the task_queue_execute handler or if this
//    function fails.
//
// 2. Call task_queue_cancel() in the containing object's deactivation handler,
//    and ensure that the task execution handler can tolerate the object being
//    concurrently deactivated.
//
// In implementations that share task queues between CPUs or allow cross-CPU
// execution of tasks, this call implies a release memory barrier that matches
// an acquire memory barrier before the task_queue_execute handler starts.
//
// If the task was already queued, this function returns ERROR_BUSY. Otherwise,
// the task_queue_execute handler will be executed once per successful call.
error_t
task_queue_schedule(task_queue_entry_t *entry);

// Cancel future execution of a given task queue entry.
//
// All calls to this function and to task_queue_schedule() for the same entry
// must be serialised by the caller.
//
// This function does not cancel execution if it has already started, and does
// not wait for execution to complete. Any execution that has already started is
// guaranteed to be complete after an RCU grace period has elapsed. Also, the
// entry may not be safely freed until an RCU grace period has elapsed.
//
// If the task was not queued, or had already started, this function returns
// ERROR_IDLE.
error_t
task_queue_cancel(task_queue_entry_t *entry);
