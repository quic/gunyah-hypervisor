// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Run the scheduler and possibly switch to a different thread, with a hint
// that the current thread should continue running if it is permitted to do so
// (i.e. nothing is higher priority).
//
// The caller must not be holding any spinlocks (including the scheduler lock
// for any thread) and must not be in an RCU read-side critical section.
//
// Returns true if the scheduler switched threads (and has switched back).
bool
scheduler_schedule(void);

// Trigger a scheduler run to occur once it is safe.
//
// This is intended to be used instead of scheduler_schedule() when a thread is
// unblocked in a context that cannot easily run the scheduler, such as during a
// context switch.
//
// The scheduler run is not guaranteed to until the next return to userspace or
// the idle thread.
void
scheduler_trigger(void) REQUIRE_PREEMPT_DISABLED;

// Run the scheduler and possibly switch to a different thread, with a hint
// that the current thread wants to yield the CPU even if it is still
// permitted to run.
//
// The caller must not be holding any spinlocks (including the scheduler lock
// for any thread) and must not be in an RCU read-side critical section.
void
scheduler_yield(void);

// Run the scheduler and possibly switch to a different thread, with a hint
// that the current thread wants to donate its current time allocation and
// priority to the specified thread.
//
// The caller must not be holding any spinlocks (including the scheduler lock
// for any thread) and must not be in an RCU read-side critical section. The
// caller must hold a reference to the specified thread, and must not be the
// specified thread.
//
// Note that this is not guaranteed to switch to the specified thread, which
// may be blocked, only runnable on a remote CPU, or of lower priority than
// another runnable thread even after priority donation:
void
scheduler_yield_to(thread_t *target);

// Lock a thread's scheduler state.
//
// Calling this function acquires a spinlock that protects the specified thread
// from concurrent changes to its scheduling state. Calls to this function must
// be balanced by calls to scheduler_unlock().
//
// A caller must not attempt to acquire scheduling locks for multiple threads
// concurrently.
void
scheduler_lock(thread_t *thread) ACQUIRE_SCHEDULER_LOCK(thread);

// Lock a thread's scheduler state, when preemption is known to be disabled.
void
scheduler_lock_nopreempt(thread_t *thread) ACQUIRE_SCHEDULER_LOCK_NP(thread);

// Unlock a thread's scheduler state.
//
// Calling this function releases the spinlock that was acquired by calling
// scheduler_lock(). Calls to this function must exactly balance calls to
// scheduler_lock().
void
scheduler_unlock(thread_t *thread) RELEASE_SCHEDULER_LOCK(thread);

// Unlock a thread's scheduler state, without enabling preemption.
void
scheduler_unlock_nopreempt(thread_t *thread) RELEASE_SCHEDULER_LOCK_NP(thread);

// Block a thread for a specified reason.
//
// Calling this function prevents the specified thread being chosen by the
// scheduler, until scheduler_unblock() is called on the thread for the same
// reason. Multiple blocks with the same reason do not nest, and can be
// cleared by a single unblock.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// Calling this function on the current thread does not immediately switch
// contexts; the caller must subsequently call scheduler_schedule() or
// scheduler_yield*() in that case (not scheduler_trigger()!). If this is done
// with preemption enabled, any subsequent preemption will not return until the
// thread has been unblocked by another thread, and any call to
// scheduler_yield*() may not occur until after that unblock, so it is
// preferable to call scheduler_schedule() or disable preemption first.
//
// Calling this function on a thread that is currently running on a remote CPU
// will not immediately interrupt that thread. Call scheduler_sync(thread) if
// it is necessary to wait until the target thread is not running.
void
scheduler_block(thread_t *thread, scheduler_block_t block)
	REQUIRE_SCHEDULER_LOCK(thread);

// Block a thread for a specified reason during creation.
//
// This function has the same functionality as scheduler_block(), but the caller
// is not required to hold the scheduling lock for the thread. However, this
// function can only be used by object_create_thread handlers on a newly created
// thread.
void
scheduler_block_init(thread_t *thread, scheduler_block_t block);

// Remove a block reason from a thread, possibly allowing it to run.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock());
//
// Calling this function on a thread that is immediately runnable on some CPU
// does not necessarily cause it to actually run. If this function returns true,
// the caller should call scheduler_schedule(), scheduler_trigger() or
// scheduler_yield*() afterwards to avoid delaying execution of the unblocked
// thread.
//
// Returns true if a scheduler run is needed as a consequence of this call.
bool
scheduler_unblock(thread_t *thread, scheduler_block_t block)
	REQUIRE_SCHEDULER_LOCK(thread);

// Return true if a thread is blocked for a specified reason.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section.
//
// Note that this function is inherently racy: if the specified thread might
// be blocked or unblocked with the specified reason by a third party, then it
// may return an incorrect value. It is the caller's responsibility to
// guarantee that such races do not occur, typically by calling
// scheduler_lock().
bool
scheduler_is_blocked(const thread_t *thread, scheduler_block_t block);

// Return true if a thread is available for scheduling.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// This function may ignore some block flags if the thread has been killed.
bool
scheduler_is_runnable(const thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Return true if a thread is currently scheduled and running.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
bool
scheduler_is_running(const thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Wait until a specified thread is not running.
//
// The caller must not be holding any spinlocks and must not be an RCU
// read-side critical section. The caller must hold a reference to the
// specified thread.
//
// If the specified thread is not blocked, or may be unblocked by some other
// thread, this function may block indefinitely. There is no guarantee that
// the thread will not be running when this function returns; there is only a
// guarantee that the thread was not running at some time after the function
// was called.
//
// This function implies an acquire barrier that synchronises with a release
// barrier performed by the specified thread when it stopped running.
void
scheduler_sync(thread_t *thread);

// Pin a thread to its current physical CPU, preventing it from migrating to
// other physicals CPUs.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// Multiple calls to this function nest; the same number of calls to
// scheduler_unpin() are required before a thread becomes migratable again.
//
// This function is a no-op for schedulers that do not support migration.
void
scheduler_pin(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Unpin a thread from its current physical CPU.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// This function is a no-op for schedulers that do not support migration.
void
scheduler_unpin(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Get the primary VCPU for a specific physical CPU.
//
// This functions returns a pointer to a VCPU on the specified physical CPU that
// belongs to the primary HLOS VM, which is responsible for interrupt handling
// and hosts the primary scheduler. This is only defined in configurations that
// defer most decisions and all interrupt handling to the primary HLOS.
//
// This function does not take a reference to the returned thread, so it must be
// called from an RCU read-side critical section.
thread_t *
scheduler_get_primary_vcpu(cpu_index_t cpu) REQUIRE_RCU_READ;

// Returns the configured affinity of a thread.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// In most cases it is not correct for a thread to call this function on itself,
// because the result is the CPU that the scheduler _wants_ to schedule the
// thread on, not the CPU it _has_ scheduled the thread on. If the current
// thread wants to know which CPU it is running on, it can use
// cpulocal_get_index(); for threads that may be running remotely
// scheduler_get_active_affinity() should be used instead.
cpu_index_t
scheduler_get_affinity(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Returns the active affinity of a thread.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// This function can be used to determine which CPU a thread is actively running
// on, which may not reflect the configured affinity if it has been recently
// changed. If the thread is not currently running, this function will return
// the same result as scheduler_get_affinity().
cpu_index_t
scheduler_get_active_affinity(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);

// Set the affinity of a thread.
//
// The caller must either be the specified thread, or hold a reference to the
// specified thread, or be in an RCU read-side critical section. The caller must
// also hold the scheduling lock for the thread (see scheduler_lock()).
//
// For schedulers that do not support migration, this function must only be
// called for threads that have not yet been activated. Threads that are pinned
// to a CPU cannot have their affinity changed.
error_t
scheduler_set_affinity(thread_t *thread, cpu_index_t target_cpu)
	REQUIRE_SCHEDULER_LOCK(thread);

error_t
scheduler_set_priority(thread_t *thread, priority_t priority)
	REQUIRE_SCHEDULER_LOCK(thread);

error_t
scheduler_set_timeslice(thread_t *thread, nanoseconds_t timeslice)
	REQUIRE_SCHEDULER_LOCK(thread);

// Returns true if the specified thread has sufficient priority to immediately
// preempt the currently running thread.
//
// This function assumes that the specified thread is able to run on the calling
// CPU, regardless of the current block flags, affinity, timeslice, etc.
//
// The scheduler lock for the specified thread must be held, and it is assumed
// not to be the current thread.
bool
scheduler_will_preempt_current(thread_t *thread) REQUIRE_SCHEDULER_LOCK(thread);
