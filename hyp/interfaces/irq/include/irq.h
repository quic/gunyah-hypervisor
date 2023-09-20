// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Routines for handling hardware-triggered IRQs.

#if !defined(IRQ_NULL)
// Return the maximum valid hardware IRQ number.
irq_t
irq_max(void);

// Enable a hardware IRQ, which must not be per-CPU.
//
// This function always immediately enables the IRQ, regardless of how many
// times irq_disable_*() has been called; there is no nesting count. The caller
// is responsible for counting disables if necessary.
//
// Note that newly registered IRQs are always disabled, and IRQs are
// automatically disabled when they are deregistered.
void
irq_enable_shared(hwirq_t *hwirq);

// Enable a hardware per-CPU IRQ on the local CPU.
//
// The behaviour is the same as for irq_enable_shared(), but affects only the
// calling CPU.
void
irq_enable_local(hwirq_t *hwirq) REQUIRE_PREEMPT_DISABLED;

// Disable a hardware IRQ, which must not be per-CPU, and wait until any running
// handlers on remote CPUs have completed.
//
// This function might block the calling thread, so cannot be called with
// preemption disabled.
void
irq_disable_shared_sync(hwirq_t *hwirq);

// Disable a hardware IRQ, which must not be per-CPU, without waiting for
// handlers on remote CPUs to complete.
//
// This may be called with preemption disabled.
void
irq_disable_shared_nosync(hwirq_t *hwirq);

// Disable a hardware per-CPU IRQ on the local CPU.
//
// This may be called with preemption disabled.
void
irq_disable_local(hwirq_t *hwirq) REQUIRE_PREEMPT_DISABLED;

// Disable a hardware per-CPU IRQ on the local CPU, without waiting for the
// physical interrupt controller to acknowledge the disable (which may allow
// spurious interrupts to occur the call).
//
// This may be called with preemption disabled.
void
irq_disable_local_nowait(hwirq_t *hwirq) REQUIRE_PREEMPT_DISABLED;

// Deactivate an interrupt that has been handled after returning false from
// the irq_received handler. This is called automatically if the handler returns
// true.
void
irq_deactivate(hwirq_t *hwirq);

// Obtain the HW IRQ structure for a specific IRQ number.
//
// Must be called from an RCU critical section. No reference is taken to the
// result.
hwirq_t *
irq_lookup_hwirq(irq_t irq) REQUIRE_RCU_READ;

#if IRQ_HAS_MSI

// Allocate an IRQ number from the platform's message-signalled IRQ number
// range, and register a HW IRQ structure for it with the specified handler
// type.
//
// If there are no free MSI numbers, returns ERROR_BUSY.
//
// On success, returns a hwirq object with one reference held. No capability is
// created.
//
// The allocated MSI number will be automatically freed when the returned object
// is destroyed.
hwirq_ptr_result_t
irq_allocate_msi(partition_t *partition, hwirq_action_t action);

#endif // IRQ_HAS_MSI

#endif // !defined(IRQ_NULL)

// Handle interrupt assertion on the current CPU.
//
// Returns true if rescheduling is needed.
bool
irq_interrupt_dispatch(void) REQUIRE_PREEMPT_DISABLED;
