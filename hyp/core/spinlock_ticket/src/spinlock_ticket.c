// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <atomic.h>
#include <preempt.h>
#include <spinlock.h>

#include <events/spinlock.h>

#include <asm/event.h>

// Ticket spinlock implementation, used for multiprocessor builds on
// architectures that have event-wait instructions (i.e. ARMv7 and ARMv8). If
// there is no event-wait then a more cache-efficient (but more complex) lock
// may be preferable.

void
spinlock_init(spinlock_t *lock)
{
	atomic_init(&lock->now_serving, 0);
	atomic_init(&lock->next_ticket, 0);
	trigger_spinlock_init_event(lock);
}

void
spinlock_acquire(spinlock_t *lock)
{
	preempt_disable();
	spinlock_acquire_nopreempt(lock);
}

void
spinlock_acquire_nopreempt(spinlock_t *lock) LOCK_IMPL
{
	trigger_spinlock_acquire_event(lock);

	// Take a ticket
	uint16_t my_ticket = atomic_fetch_add_explicit(&lock->next_ticket, 1,
						       memory_order_relaxed);

	// Wait until our ticket is being served
	while (asm_event_load_before_wait(&lock->now_serving) != my_ticket) {
		asm_event_wait(&lock->now_serving);
	}

	trigger_spinlock_acquired_event(lock);
}

bool
spinlock_trylock(spinlock_t *lock)
{
	bool success;

	preempt_disable();
	success = spinlock_trylock_nopreempt(lock);
	if (!success) {
		preempt_enable();
	}

	return success;
}

bool
spinlock_trylock_nopreempt(spinlock_t *lock) LOCK_IMPL
{
	trigger_spinlock_acquire_event(lock);

	// See which ticket is being served
	uint16_t now_serving = atomic_load_relaxed(&lock->now_serving);

	// Take a ticket, but only if it's being served already
	bool success = atomic_compare_exchange_strong_explicit(
		&lock->next_ticket, &now_serving, now_serving + 1U,
		memory_order_acquire, memory_order_relaxed);

	if (success) {
		trigger_spinlock_acquired_event(lock);
	} else {
		trigger_spinlock_failed_event(lock);
	}
	return success;
}

void
spinlock_release(spinlock_t *lock)
{
	spinlock_release_nopreempt(lock);
	preempt_enable();
}

void
spinlock_release_nopreempt(spinlock_t *lock) LOCK_IMPL
{
	uint16_t now_serving = atomic_load_relaxed(&lock->now_serving);

	trigger_spinlock_release_event(lock);

	// Start serving the next ticket
	asm_event_store_and_wake(&lock->now_serving, now_serving + 1U);

	trigger_spinlock_released_event(lock);
}

void
assert_spinlock_held(spinlock_t *lock)
{
	assert_preempt_disabled();
	trigger_spinlock_assert_held_event(lock);
}
