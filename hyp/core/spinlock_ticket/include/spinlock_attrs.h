// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifdef __EVENTS_DSL__
#define require_spinlock(lock)                                                 \
	require_preempt_disabled;                                              \
	require_lock(lock)
#else
#define ACQUIRE_SPINLOCK(lock)	  ACQUIRE_LOCK(lock) ACQUIRE_PREEMPT_DISABLED
#define ACQUIRE_SPINLOCK_NP(lock) ACQUIRE_LOCK(lock) REQUIRE_PREEMPT_DISABLED
#define TRY_ACQUIRE_SPINLOCK(success, lock)                                    \
	TRY_ACQUIRE_LOCK(success, lock) TRY_ACQUIRE_PREEMPT_DISABLED(success)
#define TRY_ACQUIRE_SPINLOCK_NP(success, lock)                                 \
	TRY_ACQUIRE_LOCK(success, lock) REQUIRE_PREEMPT_DISABLED
#define RELEASE_SPINLOCK(lock)	  RELEASE_LOCK(lock) RELEASE_PREEMPT_DISABLED
#define RELEASE_SPINLOCK_NP(lock) RELEASE_LOCK(lock) REQUIRE_PREEMPT_DISABLED
#define REQUIRE_SPINLOCK(lock)	  REQUIRE_LOCK(lock) REQUIRE_PREEMPT_DISABLED
#define EXCLUDE_SPINLOCK(lock)	  EXCLUDE_LOCK(lock)
#endif
