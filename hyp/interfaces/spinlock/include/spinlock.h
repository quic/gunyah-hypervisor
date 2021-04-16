// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
spinlock_init(spinlock_t *lock);

void
spinlock_acquire(spinlock_t *lock);

bool
spinlock_trylock(spinlock_t *lock);

void
spinlock_release(spinlock_t *lock);
