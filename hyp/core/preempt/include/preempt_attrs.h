// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// RCU may be implemented using preempt-disable. Therefore, excluding preempt
// disable also implies excluding RCU read.

#ifdef __EVENTS_DSL__
#define acquire_preempt_disabled acquire_lock preempt_disabled
#define release_preempt_disabled release_lock preempt_disabled
#define require_preempt_disabled require_lock preempt_disabled
#define exclude_preempt_disabled                                               \
	exclude_lock preempt_disabled;                                         \
	exclude_read rcu_read
#else
#define ACQUIRE_PREEMPT_DISABLED ACQUIRE_LOCK(preempt_disabled)
#define TRY_ACQUIRE_PREEMPT_DISABLED(success)                                  \
	TRY_ACQUIRE_LOCK(success, preempt_disabled)
#define RELEASE_PREEMPT_DISABLED RELEASE_LOCK(preempt_disabled)
#define REQUIRE_PREEMPT_DISABLED REQUIRE_LOCK(preempt_disabled)
#define EXCLUDE_PREEMPT_DISABLED                                               \
	EXCLUDE_LOCK(preempt_disabled) EXCLUDE_READ(rcu_read)
#define ACQUIRE_PREEMPT_NMI ACQUIRE_LOCK(preempt_nmi_lock)
#define RELEASE_PREEMPT_NMI RELEASE_LOCK(preempt_nmi_lock)
#endif
