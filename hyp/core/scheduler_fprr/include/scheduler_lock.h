// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifdef __EVENTS_DSL__
#define require_scheduler_lock(t) require_spinlock(t->scheduler_lock)
#else
#define ACQUIRE_SCHEDULER_LOCK(thread) ACQUIRE_SPINLOCK(thread->scheduler_lock)
#define ACQUIRE_SCHEDULER_LOCK_NP(thread)                                      \
	ACQUIRE_SPINLOCK_NP(thread->scheduler_lock)
#define RELEASE_SCHEDULER_LOCK(thread) RELEASE_SPINLOCK(thread->scheduler_lock)
#define RELEASE_SCHEDULER_LOCK_NP(thread)                                      \
	RELEASE_SPINLOCK_NP(thread->scheduler_lock)
#define REQUIRE_SCHEDULER_LOCK(thread) REQUIRE_SPINLOCK(thread->scheduler_lock)
#define EXCLUDE_SCHEDULER_LOCK(thread) EXCLUDE_SPINLOCK(thread->scheduler_lock)
#endif
