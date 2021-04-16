// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Platform routines for handling hardware IRQs.
//
// These routines presume that the interrupt controller implements automatic
// masking of all interrupts until the end of the handler, and selectively
// extending that masking after the end of the interrupt handler for interrupts
// forwarded to a VM, similar to an ARM GICv2 or later with EOImode=1. If the
// interrupt controller does not implement these semantics, the platform driver
// must emulate them.
//
// The calling code should not assign any particular meaning to IRQ numbers,
// beyond assuming that they are not greater than the value returned by
// platform_irq_max().
//
// Note that the platform driver may handle some IRQs internally; for example,
// to signal IPIs, or to support a debug console. Such IRQs are not exposed to
// this interface.

// Obtain the maximum valid IRQ number.
//
// This returns an irq_t value which is the upper bound for valid irq_t values
// on this platform. Note that it is not necessarily the case that all irq_t
// values less than this value are valid; see platform_irq_check() below.
//
// This function may be called by a default-priority boot_cold_init handler. If
// the driver needs initialisation to determine this value, it should subscribe
// to boot_cold_init with a positive priority.
irq_t
platform_irq_max(void);

// Check whether a specific IRQ number is valid for handler registration.
//
// This function returns ERROR_DENIED for any IRQ number that is reserved by the
// platform driver or by a higher privileged level, ERROR_ARGUMENT_INVALID for
// any IRQ number that is greater than the value returned by platform_irq_max()
// or is otherwise known not to be implemented by the hardware, and OK for any
// other IRQ number.
error_t
platform_irq_check(irq_t irq);

// Check whether a specific IRQ number is per-CPU.
//
// This function must only be called on IRQ numbers that have previously
// returned an OK result from platform_irq_check().
bool
platform_irq_is_percpu(irq_t irq);

// Enable delivery of a specified IRQ, which must not be per-CPU.
void
platform_irq_enable(irq_t irq);

// Enable delivery of a specified per-CPU IRQ, on the calling CPU.
void
platform_irq_enable_local(irq_t irq);

// Enable delivery of a specified per-CPU IRQ, on the specified CPU.
//
// Note that some platforms are unable to implement this function.
void
platform_irq_enable_percpu(irq_t irq, cpu_index_t cpu);

// Disable delivery of a specified IRQ, which must not be per-CPU.
//
// On some platforms, this function must busy-wait until the IRQ controller has
// acknowledged that the interrupt is disabled.
void
platform_irq_disable(irq_t irq);

// Disable delivery of a specified per-CPU IRQ, on the calling CPU.
//
// On some platforms, this function must busy-wait until the IRQ controller has
// acknowledged that the interrupt is disabled.
void
platform_irq_disable_local(irq_t irq);

// Disable delivery of a specified per-CPU IRQ, on the calling CPU, without
// waiting for the physical interrupt controller to acknowledge that delivery is
// disabled.
//
// This may be called if the in-hypervisor driver for the IRQ needs to disable
// it in performance-critical code (e.g. a context switch) and is prepared to
// handle any spurious interrupts that may occur as a result of not waiting.
void
platform_irq_disable_local_nowait(irq_t irq);

// Disable delivery of a specified per-CPU IRQ, on the specified CPU.
//
// On some platforms, this function must busy-wait until the IRQ controller has
// acknowledged that the interrupt is disabled.
//
// Note that some platforms are unable to implement this function.
void
platform_irq_disable_percpu(irq_t irq, cpu_index_t cpu);

// Acknowledge and activate an IRQ.
//
// This function acknowledges the highest-priority pending IRQ and returns its
// IRQ number. If there are multiple IRQs with equal highest priority, it
// selects one to acknowledge arbitrarily. Delivery of the acknowledged IRQ is
// disabled until the next platform_irq_deactivate() call for that IRQ, unless a
// platform-specific mechanism deactivates the IRQ.
//
// If there is an interrupt to acknowledge, but it is directed to the platform
// driver itself (e.g. to signal an IPI), the result will be set to ERROR_RETRY.
//
// If there are no pending interrupts left to acknowledge, the result will be
// set to ERROR_IDLE.
//
// This function must only be called from an interrupt handler. If it returns an
// IRQ number, that number must be passed to platform_irq_priority_drop() before
// the interrupt handler returns.
irq_result_t
platform_irq_acknowledge(void);

// De-prioritise an acknowledged IRQ.
//
// This function lowers the effective priority of an acknowledged IRQ, allowing
// its handling to be deferred until after the interrupt handler returns without
// blocking delivery of further interrupts.
//
// This must only be called for IRQs returned by platform_irq_acknowledge().
// Additionally, the specified IRQ must be the one most recently returned by
// that function on the current physical CPU, after excluding any IRQs that have
// already been passed to this function.
//
// This call must be followed (eventually) by a platform_irq_deactivate() call,
// unless the IRQ is forwarded to a VCPU and the platform provides a mechanism
// for forwarded IRQs to be deactivated directly by the VCPU.
void
platform_irq_priority_drop(irq_t irq);

// Deactivate an acknowledged IRQ.
//
// This function re-enables delivery of the specified interrupt after it was
// previously de-prioritised by platform_irq_priority_drop(). For a per-CPU IRQ
// it must be called on the same physical CPU that acknowledged the IRQ;
// otherwise it may be called on any CPU.
//
// Note that the platform may provide an alternative mechanism for performing
// this operation for IRQs that are forwarded to a VCPU via a hardware virtual
// IRQ delivery mechanism. In that case, the hypervisor need not call this
// function for any forwarded IRQ that uses that mechanism.
void
platform_irq_deactivate(irq_t irq);

// Deactivate an acknowledged per-CPU IRQ on a specified physical CPU.
//
// This works the same way as platform_irq_deactivate(), but allows per-CPU
// IRQs acknowledged on one physical CPU to be deactivated from a different
// physical CPU.
//
// Note that this may be slower than platform_irq_deactivate() for such IRQs,
// and may not be possible to implement without IPIs.
void
platform_irq_deactivate_percpu(irq_t irq, cpu_index_t cpu);

// Set trigger of per-CPU IRQ on a specified physical CPU.
// FIXME: add more details descriptions.
irq_trigger_result_t
platform_irq_set_mode_percpu(irq_t irq, irq_trigger_t trigger, cpu_index_t cpu);
