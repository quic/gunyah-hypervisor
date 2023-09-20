// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

ticks_t
platform_timer_get_timeout(void);

void
platform_timer_cancel_timeout(void) REQUIRE_PREEMPT_DISABLED;

// Must be called with preempt_disabled
void
platform_timer_set_timeout(ticks_t timeout) REQUIRE_PREEMPT_DISABLED;

uint32_t
platform_timer_get_frequency(void);

ticks_t
platform_timer_get_current_ticks(void);

ticks_t
platform_timer_convert_ns_to_ticks(nanoseconds_t ns);

nanoseconds_t
platform_timer_convert_ticks_to_ns(ticks_t ticks);

ticks_t
platform_timer_convert_ms_to_ticks(milliseconds_t ms);

milliseconds_t
platform_timer_convert_ticks_to_ms(ticks_t ticks);

void
platform_timer_ndelay(nanoseconds_t duration);
