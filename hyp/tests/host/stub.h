// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifndef STUB_H
#define STUB_H

#include <stdnoreturn.h>

#if !defined(NDEBUG)
noreturn void
assert_failed(const char *file, int line, const char *func, const char *err);
#endif

void
panic(const char *msg);

// =========================================================================
// RCU handling stubs
// =========================================================================

void
rcu_read_start(void) ACQUIRE_RCU_READ;

void
rcu_read_finish(void) RELEASE_RCU_READ;

// =========================================================================
// Spinlock stubs
// =========================================================================

void
spinlock_init(spinlock_t *lock);

void
spinlock_acquire(spinlock_t *lock) ACQUIRE_SPINLOCK(lock);

void
spinlock_acquire_nopreempt(spinlock_t *lock) ACQUIRE_SPINLOCK_NP(lock);

void
spinlock_release(spinlock_t *lock) RELEASE_SPINLOCK(lock);

void
spinlock_release_nopreempt(spinlock_t *lock) RELEASE_SPINLOCK_NP(lock);

#endif // STUB_H
