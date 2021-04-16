// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <hypregisters.h>

#include <platform_timer.h>

#include <asm/barrier.h>
#include <asm/timestamp.h>

uint64_t
arch_get_timestamp(void)
{
	__asm__ volatile("isb" : "+m"(asm_ordering));
	return platform_timer_get_current_ticks();
}
