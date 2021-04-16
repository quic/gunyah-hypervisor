// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Interfaces for asserting virtual IRQs.
//
// Hardware-sourced interrupts may be handled internally by the VIC if some
// degree of hardware acceleration is available, e.g. for ARM GICv2 and later.
// Otherwise, they will be handled by a module that uses these APIs.
//
// Note that concurrent calls to the virq_assert() and virq_clear() functions
// for the same source may leave the VIRQ in an unknown state. Callers that
// expect level-triggered behaviour must serialise calls to these functions.
//
// The caller must either hold references to all specified object(s), or
// else be in an RCU read-side critical section.

// Assert a VIRQ source.
//
// If the edge_only parameter is true, then the VIRQ will only be asserted if
// it is configured for edge triggering, as if a hardware line had experienced
// an arbitrarily short transient signal (or was triggered by a message). This
// avoids the need to either call virq_clear() or register a virq_check_pending
// handler, and is more efficient than either of those options. Note that the
// VIC implementation may not support edge triggering for any specific VIRQ.
// Also note that the effect of such a call on a VIRQ source that was previously
// asserted with the edge_only parameter set to false is unpredictable.
//
// If the source has not claimed a VIRQ, or the target VIC or VCPU has been
// destroyed, this function returns ERROR_VIRQ_NOT_BOUND.
//
// On success, this function returns a boolean value which is true if the IRQ
// was delivered with edge triggering enabled.
bool_result_t
virq_assert(virq_source_t *source, bool edge_only);

// Deassert a level-triggered VIRQ source.
//
// This function has no effect for an edge-triggered VIRQ. It only affects VIRQs
// that either do not support edge triggering, or else have been configured for
// level triggering by the VM. Note that this configuration might change at
// runtime without notifying the VIRQ source, and there is no mechanism to query
// the current configuration.
//
// Returning false from a handler for the virq_check_pending event has the same
// effect as calling this function. The event handler is typically more
// efficient, but requires lock-free synchronisation that is not practical in
// some cases.
//
// Note that this function does not wait for cancellation of the specified VIRQ
// on every registered VCPU. If the VIRQ is currently asserted and routed to a
// VCPU that is active on a remote physical CPU, the interrupt may be spuriously
// delivered to the VM shortly after this function returns.
//
// If the source has not claimed a VIRQ, or the target VIC or VCPU has been
// destroyed, this function returns ERROR_VIRQ_NOT_BOUND. Otherwise, it returns
// OK, regardless of the prior state of the interrupt.
error_t
virq_clear(virq_source_t *source);

// Query whether a level-triggered VIRQ source is currently asserted.
//
// Returns true if the VIRQ has been asserted by a virq_assert() call (with
// edge_only set to false) and has not subsequently been cleared by either a
// call to virq_clear() or a false result from the virq_check_pending event.
//
// If the source has not claimed a VIRQ, or the target VIC or VCPU has been
// destroyed, this function returns ERROR_VIRQ_NOT_BOUND.
bool_result_t
virq_query(virq_source_t *source);
