// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

void
platform_timer_lp_set_timeout(ticks_t timeout) REQUIRE_PREEMPT_DISABLED;

ticks_t
platform_timer_lp_get_timeout(void);

void
platform_timer_lp_cancel_timeout(void) REQUIRE_PREEMPT_DISABLED;

uint32_t
platform_timer_lp_get_frequency(void);

ticks_t
platform_timer_lp_get_current_ticks(void);

void
platform_timer_lp_visibility(bool visible);

void
platform_timer_lp_set_timeout_and_route(ticks_t timeout, cpu_index_t cpu_index)
	REQUIRE_PREEMPT_DISABLED;
