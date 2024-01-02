// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Execute the architecture's basic wait-for-interrupt instruction.
//
// This function is called with interrupts disabled.
//
// If interrupts must be enabled for the wait instruction to function correctly,
// this function may enable them; in this case the architecture's vectors must
// be able to handle interrupts taken from the idle loop, even in non-
// preemptible configurations that otherwise do not take interrupts in the
// hypervisor. Interrupts must be disabled again before returning.
//
// If the wait instruction can work with interrupts disabled, this function
// must leave them disabled and call irq_interrupt_dispatch() directly after the
// wait. This call may be conditional on an explicit check for pending
// interrupts, if such a check is possible.
//
// This function returns true if a reschedule may be necessary. An
// implementation that enables interrupts must always return true.
bool
idle_arch_wait(void) REQUIRE_PREEMPT_DISABLED;

// Execute a wait-for-interrupt with a timeout.
//
// This is the same as idle_arch_wait(), except that a timeout can be specified
// (as an absolute ticks value) as the time at which the CPU will stop waiting.
// If possible, the implementation should execute a wait for interrupt
// instruction, and arrange to be woken at expiry of the timeout if no other
// event has occurred.
//
// The wokeup mechanism should not rely on interrupt delivery, and should not
// execute any non-trivial code; it is assumed that an architectural wakeup
// mechanism will be used (e.g. AArch64 FEAT_WFxT). If no such mechanism is
// available, the implementation should check for interrupts without waiting.
bool
idle_arch_wait_timeout(ticks_t timeout) REQUIRE_PREEMPT_DISABLED;
