// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#ifdef __EVENTS_DSL__
#define acquire_preempt_disabled acquire_lock preempt_disabled
#define release_preempt_disabled release_lock preempt_disabled
#define require_preempt_disabled require_lock preempt_disabled
#define exclude_preempt_disabled exclude_lock preempt_disabled
#else
#define ACQUIRE_PREEMPT_DISABLED ACQUIRE_LOCK(preempt_disabled)
#define TRY_ACQUIRE_PREEMPT_DISABLED(success)                                  \
	TRY_ACQUIRE_LOCK(success, preempt_disabled)
#define RELEASE_PREEMPT_DISABLED RELEASE_LOCK(preempt_disabled)
#define REQUIRE_PREEMPT_DISABLED REQUIRE_LOCK(preempt_disabled)
#define EXCLUDE_PREEMPT_DISABLED EXCLUDE_LOCK(preempt_disabled)
#endif
