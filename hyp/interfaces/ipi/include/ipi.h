// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Routines for handling inter-processor interrupts.
//
// Modules may register an IPI reason code, and an associated handler for the
// ipi_received event. When an IPI is targeted at an online CPU, and has not
// been masked by that CPU, the handler for the IPI's reason code will be run on
// the CPU. Handlers run with preemption disabled, and may run from the idle
// thread or an interrupt handler; they return a boolean value which indicates
// whether a reschedule is needed.
//
// Each handler call is preceded by an acquire barrier which synchronises with
// the release barrier that was performed when the IPI was sent. Any subsequent
// IPI send operation that does not synchronise with that acquire barrier is
// guaranteed to trigger another call to the handler. A duplicate IPI send that
// does synchronise with the handler's acquire barrier may or may not trigger
// another handler call.
//
// If a _relaxed function is used to send an IPI, the targeted CPU(s) will
// handle the IPI next time they switch contexts. There is no particular promise
// of timely handling, even on idle CPUs.
//
// If an _idle function is used to send an IPI, it is guaranteed to be handled
// quickly whenever the targeted CPU(s) are idle. It will avoid interrupting a
// busy CPU if possible. This may be an alias of the corresponding _relaxed
// function if relaxed IPIs are guaranteed to wake idle CPUs; otherwise it is an
// alias of the corresponding normal IPI function. The _idle functions in the
// IPI module interface cannot wake the target CPU from suspend, therefore,
// using these functions incorrectly may lead to an indefinite CPU suspend,
// causing the system to hang.
//
// All of the receive-side functions below must be called with preemption
// disabled, except where noted.

// Send the specified IPI to all online CPUs other than the caller.
//
// This implies a release barrier.
void
ipi_others(ipi_reason_t ipi);

// Send the specified IPI to all online CPUs other than the caller, with low
// priority.
//
// This implies a release barrier.
void
ipi_others_relaxed(ipi_reason_t ipi);

// Send the specified IPI to all online CPUs other than the caller, with low
// priority, guaranteeing that idle CPUs will wake.
//
// This implies a release barrier.
//
// Do not use with the intention of waking a suspended CPU.
void
ipi_others_idle(ipi_reason_t ipi);

// Send the specified IPI to a single CPU.
//
// This implies a release barrier.
void
ipi_one(ipi_reason_t ipi, cpu_index_t cpu);

// Send the specified IPI to a single CPU, with low priority.
//
// This implies a release barrier.
void
ipi_one_relaxed(ipi_reason_t ipi, cpu_index_t cpu);

// Send the specified IPI to a single CPU, with low priority, guaranteeing that
// it will wake if idle.
//
// This implies a release barrier.
//
// Do not use with the intention of waking a suspended CPU.
void
ipi_one_idle(ipi_reason_t ipi, cpu_index_t cpu);

// Atomically check and clear the specified IPI reason.
//
// This can be used to prevent redundant invocations of an IPI handler. Call
// it immediately prior to taking the same action that the handler would take.
// However, note that it may incorrectly return false when called in an IPI
// handler, including a handler for a different IPI than the one being cleared.
//
// This function executes an acquire operation before returning true, equivalent
// to the acquire operation that is executed before calling a handler.
//
// If possible, this function will cancel any pending physical IPIs that other
// CPUs have asserted to signal the IPI, but note that not all interrupt
// controllers can reliably cancel an IPI without handling it.
//
// This function may be safely called with preemption enabled, from any context.
// However, its result must be ignored if it is called in an IPI handler.
bool
ipi_clear(ipi_reason_t ipi);

// Atomically check and clear the specified IPI, assuming it was a relaxed IPI.
//
// This can be used to prevent redundant invocations of an IPI handler. Call
// it immediately prior to taking the same action that the handler would take.
//
// This function executes an acquire operation before returning true, equivalent
// to the acquire operation that is executed before calling a handler.
//
// This function does not attempt to cancel pending physical IPIs, and therefore
// can avoid the cost of interacting with the interrupt controller for IPIs that
// are always, or mostly, raised using the _relaxed functions.
bool
ipi_clear_relaxed(ipi_reason_t ipi);

// Immediately handle any relaxed IPIs.
//
// Returns true if a reschedule is needed.
bool
ipi_handle_relaxed(void) REQUIRE_PREEMPT_DISABLED;
