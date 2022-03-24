// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// A simple wait_queue for blocking threads while waiting on events.
// TODO: add support for interruption - e.g. destroying threads

// Ordering in the wait_queue API:
//
// An acquire operation is implied by any wait_queue_wait() call that sleeps;
// and a release operation on the wait queue is implied by any
// wait_queue_wakeup() call that wakes up at least one thread.

// Initialise the wait_queue.
void
wait_queue_init(wait_queue_t *wait_queue);

// Enqueue calling thread on the wait_queue.
//
// Must be called before calling wait_queue_get(), and finally the caller
// should call wait_queue_finish() when done.
void
wait_queue_prepare(wait_queue_t *wait_queue)
	ACQUIRE_PREEMPT_DISABLED ACQUIRE_LOCK(wait_queue);

// The calling thread enters a critical section where it is safe to perform the
// condition check without races. If the condition passes, then call
// wait_queue_put(), else call wait_queue_wait().
//
// wait_queue_prepare() must have been called prior to this.
void
wait_queue_get(void)
	REQUIRE_PREEMPT_DISABLED ACQUIRE_LOCK(wait_queue_condition);

// Exit the wait_queue critical section.
//
// Must be called after wait_queue_get() and the subsequent condition check
// succeeded. Must not be called after wait_queue_wait().
void
wait_queue_put(void)
	REQUIRE_PREEMPT_DISABLED RELEASE_LOCK(wait_queue_condition);

// Atomically exit the wait_queue critical section and block until a wakeup
// event.
//
// May be called after wait_queue_get() and the subsequent condition check
// fails and we want to yield. Must not be called after wait_queue_put().
void
wait_queue_wait(void)
	REQUIRE_PREEMPT_DISABLED RELEASE_LOCK(wait_queue_condition);

// Dequeue the thread from the wait_queue. Call this when woken up and the wait
// condition now passes, after wait_queue_put() or wait_queue_wait().
void
wait_queue_finish(wait_queue_t *wait_queue)
	RELEASE_PREEMPT_DISABLED RELEASE_LOCK(wait_queue)
		EXCLUDE_LOCK(wait_queue_condition);

// Perform a wakeup event on the wait_queue
void
wait_queue_wakeup(wait_queue_t *wait_queue);
