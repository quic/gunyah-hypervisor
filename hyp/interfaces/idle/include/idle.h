// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Idle thread and related APIs.

// Get the current CPU's idle thread. Must only be called in a cpulocal
// critical section.
thread_t *
idle_thread(void) REQUIRE_PREEMPT_DISABLED;

// Get the specified CPU's idle thread.
thread_t *
idle_thread_for(cpu_index_t cpu_index);

// True when running in the current CPU's idle thread.
bool
idle_is_current(void) REQUIRE_PREEMPT_DISABLED;

bool
idle_yield(void) REQUIRE_PREEMPT_DISABLED;
