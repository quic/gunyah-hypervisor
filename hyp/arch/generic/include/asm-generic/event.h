// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Wait for or raise events.
//
// These macros may apply architecture-specific optimisations to improve the
// efficiency of inter-CPU signalling by polling shared variables.
//
// These are the default definitions, which provide adequate memory barriers
// but otherwise just busy-wait. This header should only be included by the
// architecture-specific asm/event.h, which can optionally define the
// asm_event_wait() macro to an operation that may sleep, and also define the
// other operations if necessary.

// Load a polled variable before a possible asm_event_wait().
//
// This load must be an acquire operation on the specified variable.
//
// To be safe on platforms that sleep in asm_event_wait(), the return value of
// this macro _must_ be used in an expression that determines whether to
// call asm_event_wait().
#if !defined(asm_event_load_before_wait)
#define asm_event_load_before_wait(p)                                          \
	atomic_load_explicit(p, memory_order_acquire)
#endif

// As above, but for a named bitfield type.
//
// This is needed to hide pointer casts that would otherwise be unsafe on
// platforms where asm_event_load_before_wait() needs type-specific inline asm,
// such as ARMv8.
#if !defined(asm_event_load_bf_before_wait)
#define asm_event_load_bf_before_wait(name, p) asm_event_load_before_wait(p)
#endif

// Poll after checking the result of asm_event_load_before_wait().
//
// Polling may place the calling CPU in a low-power halt state until the
// value read by asm_event_load_before_wait() is updated by either a remote CPU,
// or a local interrupt handler that interrupts the poll. The polled variable
// must be updated by either calling asm_event_store_and_wake(), or with some
// other store operation followed by a call to asm_event_wake_updated().
//
// Updates performed by remote CPUs in any other way, or performed by the local
// CPU other than in an interrupt handler that preempts the poll, are not
// guaranteed to wake a sleeping poll.
//
// Polling must be safe from races; that is, asm_event_wait() must return if
// an update inter-thread happens after asm_event_load_before_wait(), regardless
// of whether the update inter-thread happens after asm_event_wait() is called.
//
// Polling is not required to sleep until the polled value is updated;
// it may wake early or not sleep at all. If the CPU does not support this
// operation and will never sleep, ASM_EVENT_WAIT_IS_NOOP is defined to be
// nonzero.
#if !defined(asm_event_wait)
#define ASM_EVENT_WAIT_IS_NOOP 1
#define asm_event_wait(p)      ((void)0)
#else
#define ASM_EVENT_WAIT_IS_NOOP 0
#endif

// Store an event variable and wake CPUs waiting on it.
//
// This store must be a release operation on the specified variable.
#if !defined(asm_event_store_and_wake)
#define asm_event_store_and_wake(p, v)                                         \
	atomic_store_explicit((p), (v), memory_order_release)
#endif

// Wake CPUs waiting on one or more variables that have been updated with direct
// atomic_*() calls rather than by calling asm_event_store_and_wake().
//
// Any direct updates must either be release operations, or else followed by
// a release fence, prior to executing this operation.
//
// This may be more expensive than asm_event_store_and_wake(), especially for a
// single store.
#if !defined(asm_event_wake_updated)
#define asm_event_wake_updated() (void)0
#endif
