// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Trivial implementation of mutexes for configurations that have no scheduler
// in the hypervisor, either because the primary VM controls scheduling or
// because context switching is not supported at all. In this case, mutexes
// degenerate to spinlocks.

#include <hyptypes.h>

#include <mutex.h>
#include <spinlock.h>

#include <events/mutex.h>

void
mutex_init(mutex_t *lock)
{
	spinlock_init(&lock->lock);
	trigger_mutex_init_event(lock);
}

void
mutex_acquire(mutex_t *lock)
{
	trigger_mutex_acquire_event(lock);
	spinlock_acquire(&lock->lock);
	trigger_mutex_acquired_event(lock);
}

bool
mutex_trylock(mutex_t *lock)
{
	trigger_mutex_acquire_event(lock);
	if (!spinlock_trylock(&lock->lock)) {
		trigger_mutex_failed_event(lock);
		return false;
	}
	trigger_mutex_acquired_event(lock);
	return true;
}

void
mutex_release(mutex_t *lock)
{
	trigger_mutex_release_event(lock);
	spinlock_release(&lock->lock);
	trigger_mutex_released_event(lock);
}
