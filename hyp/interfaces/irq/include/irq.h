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
irq_enable(hwirq_t *hwirq);

// Enable a hardware per-CPU IRQ on the local CPU.
//
// The behaviour is the same as for irq_enable(), but affects only the calling
// CPU.
void
irq_enable_local(hwirq_t *hwirq);

// Disable a hardware IRQ, which must not be per-CPU, and wait until any running
// handlers on remote CPUs have completed.
//
// This function might block the calling thread, so cannot be called with
// preemption disabled.
void
irq_disable_sync(hwirq_t *hwirq);

// Disable a hardware IRQ, which must not be per-CPU, without waiting for
// handlers on remote CPUs to complete.
//
// This may be called with preemption disabled.
void
irq_disable_nosync(hwirq_t *hwirq);

// Disable a hardware per-CPU IRQ on the local CPU.
//
// This may be called with preemption disabled.
void
irq_disable_local(hwirq_t *hwirq);

// Disable a hardware per-CPU IRQ on the local CPU, without waiting for the
// physical interrupt controller to acknowledge the disable (which may allow
// spurious interrupts to occur the call).
//
// This may be called with preemption disabled.
void
irq_disable_local_nowait(hwirq_t *hwirq);

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
irq_lookup_hwirq(irq_t irq);
#endif

// Handle interrupt assertion on the current CPU.
//
// Returns true if rescheduling is needed.
bool
irq_interrupt_dispatch(void);
