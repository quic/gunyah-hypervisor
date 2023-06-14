// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Initialise a spinlock structure.
//
// This must be called exactly once for each lock, before any of the functions
// below are called.
void
spinlock_init(spinlock_t *lock);

// Acquire exclusive ownership of a lock, spinning indefinitely until it is
// acquired. Preemption will be disabled.
void
spinlock_acquire(spinlock_t *lock) ACQUIRE_SPINLOCK(lock);

// Attempt to immediately acquire exclusive ownership of a lock, and return true
// if it succeeds. If the lock is already exclusively held by another thread,
// return false with no side-effects.
//
// Preemption will be disabled if the lock is acquired.
bool
spinlock_trylock(spinlock_t *lock) TRY_ACQUIRE_SPINLOCK(true, lock);

// Release exclusive ownership of a lock. Preemption will be enabled.
void
spinlock_release(spinlock_t *lock) RELEASE_SPINLOCK(lock);

// As for spinlock_acquire(), but preemption must already be disabled and will
// not be disabled again.
void
spinlock_acquire_nopreempt(spinlock_t *lock) ACQUIRE_SPINLOCK_NP(lock);

// As for spinlock_trylock(), but preemption must already be disabled and will
// not be disabled again.
bool
spinlock_trylock_nopreempt(spinlock_t *lock)
	TRY_ACQUIRE_SPINLOCK_NP(true, lock);

// As for spinlock_release(), but preemption will not be enabled.
void
spinlock_release_nopreempt(spinlock_t *lock) RELEASE_SPINLOCK_NP(lock);

// Assert that a specific spinlock is exclusively held by the caller.
//
// This might only be a static check, especially in non-debug builds.
void
assert_spinlock_held(const spinlock_t *lock) REQUIRE_LOCK(lock)
	REQUIRE_PREEMPT_DISABLED;
