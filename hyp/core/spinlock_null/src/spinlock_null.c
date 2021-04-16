// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <spinlock.h>

// Dummy spinlock implementation used for uniprocessor builds.

void
spinlock_init(spinlock_t *lock)
{
	trigger_spinlock_init_event(lock);
}

void
spinlock_acquire(spinlock_t *lock)
{
	trigger_spinlock_acquire_event(lock);
	trigger_spinlock_acquired_event(lock);
}

bool
spinlock_trylock(spinlock_t *lock)
{
	// Always succeeds; this must not be used to prevent recursion!
	spinlock_acquire(lock);
	return true;
}

void
spinlock_release(spinlock_t *lock)
{
	trigger_spinlock_release_event(lock);
	trigger_spinlock_released_event(lock);
}
