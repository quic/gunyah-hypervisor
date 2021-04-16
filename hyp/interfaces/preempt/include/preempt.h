// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Disable preemption, if it is not already disabled.
//
// This prevents the current thread being switched. It may also disable
// interrupts, but the caller should not rely on this.
//
// Calls to this function may be nested. Each call must be matched by a
// call to preempt_enable().
void
preempt_disable(void);

// Undo the effect of an earlier preempt_disable() call.
//
// If the matching preempt_disable() call disabled interrupts, then this call
// will re-enable them.
void
preempt_enable(void);

// Handle an interrupt in hypervisor mode.
//
// This function must be called by the architecture's interrupt handling routine
// when an interrupt preempts execution of the hypervisor. It will arrange for
// the handling of the interrupt, but note that such handling may not complete
// before this function returns.
//
// If this function returns true, the caller must arrange for interrupts to be
// disabled upon return from the current interrupt. This is intended to allow
// preempt module implementations to defer handling of an interrupt; e.g. to
// allow preempt_disable() to avoid disabling interrupts if the CPU makes that
// too slow to do frequently.
bool
preempt_interrupt_dispatch(void);

// Handle an asynchronous abort in hypervisor mode.
//
// This function must be called by the architecture's exception or interrupt
// handling routine when an asynchronous abort preempts execution of the
// hypervisor. It will arrange for handling of the abort.
//
// The meaning of "asynchronous abort" is architecture-specific and includes,
// for example, an AArch64 SError interrupt or an x86 NMI.
bool
preempt_abort_dispatch(void);

// Assert that preemption is currently disabled.
//
// This calls assert(), so it is effective only if !defined(NDEBUG).
void
assert_preempt_disabled(void);

// Assert that preemption is currently enabled.
//
// This calls assert(), so it is effective only if !defined(NDEBUG).
void
assert_preempt_enabled(void);
