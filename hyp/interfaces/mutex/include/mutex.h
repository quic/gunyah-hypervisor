// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
mutex_init(mutex_t *lock);

void
mutex_acquire(mutex_t *lock) ACQUIRE_LOCK(lock);

bool
mutex_trylock(mutex_t *lock) TRY_ACQUIRE_LOCK(true, lock);

void
mutex_release(mutex_t *lock) RELEASE_LOCK(lock);
