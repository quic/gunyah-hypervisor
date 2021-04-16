// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Dummy preemption control functions for non-preemptible configurations.

#include <hyptypes.h>

#include <compiler.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <trace.h>

void
preempt_disable(void)
{
}

void
preempt_enable(void)
{
}

bool
preempt_interrupt_dispatch(void)
{
#if !defined(NDEBUG)
	panic("Hypervisor interrupts should be disabled!");
#else
	LOG(ERROR, WARN, "Hypervisor interrupts should be disabled!");
#endif
	return true;
}

void
assert_preempt_disabled(void)
{
}

void
assert_preempt_enabled(void)
{
}
