// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Configure vcpu options
//
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
vcpu_configure(thread_t *thread, vcpu_option_flags_t vcpu_options);

// Set a VCPU's initial execution state, including its entry point and context.
//
// The target thread must be a VCPU, its scheduling lock must be held by the
// caller, and it must be currently blocked by the SCHEDULER_BLOCK_VCPU_OFF
// flag. This function clears that block flag, and returns true if the vcpu has
// become runnable (which implies that a call to scheduler_schedule() is
// needed).
//
// If the target VCPU has ever run, it must have called vcpu_poweroff() before
// this function is called on it.
bool
vcpu_poweron(thread_t *vcpu, paddr_t entry_point, register_t context)
	REQUIRE_SCHEDULER_LOCK(vcpu);

// Tear down the current thread's VCPU execution state.
//
// The caller must be a runnable VCPU. This function will block the caller with
// the SCHEDULER_BLOCK_VCPU_OFF flag and yield. It does not return on success;
// if the thread is re-activated by a call to vcpu_poweron(), it will jump
// directly to the new userspace context.
error_t
vcpu_poweroff(void);

// Suspend a VCPU.
//
// This function will block the caller with the SCHEDULER_BLOCK_VCPU_SUSPEND
// flag and yield, unless the suspend is aborted by a handler for the
// vcpu_suspend event, in which case the caller remains runnable.
//
// The return value is OK if the caller was successfully suspended and has
// since resumed, unless a warm reset was requested while the thread was
// suspended, in which case the function does not return. It returns an error if
// the suspend was aborted.
error_t
vcpu_suspend(void);

// Warm-reset a suspended VCPU.
//
// This function initialises all EL1 system registers for the current VCPU,
// including any that have been virtualised, as described by the ARMv8 ARM
// revision E.a section D1.9.1, "PE state on reset to AArch64 state". The
// entry point and context are also set as they would be by vcpu_poweron().
void
vcpu_warm_reset(paddr_t entry_point, register_t context)
	EXCLUDE_PREEMPT_DISABLED;

// Resume a suspended VCPU.
//
// The target thread must be a VCPU, its scheduling lock must be held by the
// caller, and it must be currently blocked by the SCHEDULER_BLOCK_VCPU_SUSPEND
// flag. This function clears that block flag.
void
vcpu_resume(thread_t *vcpu) REQUIRE_SCHEDULER_LOCK(vcpu);

// Wake a specified VCPU from interrupt wait.
//
// The target thread must be a VCPU, and its scheduling lock must be held by the
// caller.
//
// This function clears any block flag associated with a wait for interrupts by
// the target VCPU. If the VCPU is not currently waiting for interrupts, this
// has no effect.
void
vcpu_wakeup(thread_t *vcpu) REQUIRE_SCHEDULER_LOCK(vcpu);

// Prevent the current VCPU entering interrupt wait.
//
// In preemptible configurations, it is possible for a WFI trap handler to be
// interrupted to deliver an IRQ that should prevent the trapped WFI sleeping.
// This function ensures that any currently interrupted WFI trap handler will
// not incorrectly sleep. It must be called whenever an ISR asserts a virtual
// IRQ on the current VCPU.
//
// This function is lock-free and can be safely called from any context. In non-
// preemptible configurations it is a no-op. If called outside of an ISR, it
// has no effect.
void
vcpu_wakeup_self(void);

// Query whether the specified VCPU is blocked on vcpu_wakeup().
//
// This is intended to be called during context switch to optimise wakeup
// sources by disabling them and/or not checking their states if they are not
// able to wake the thread.
bool
vcpu_expects_wakeup(const thread_t *thread);

// Query whether the current VCPU has a pending wakeup.
//
// This may be called to check whether a long-running operation on behalf of
// the current VCPU should be interrupted so it can return early. If it returns
// true, the caller should return to the VCPU as soon as is practical. The
// caller is responsible for guaranteeing that it can make progress if this
// persistently returns true (e.g. if EL1 is calling with interrupts disabled
// while an interrupt is pending).
//
// This must be called with preemption disabled.
bool
vcpu_pending_wakeup(void) REQUIRE_PREEMPT_DISABLED;

// Prepare to block hypervisor execution on behalf of the current VCPU.
//
// This function should be called before any potentially long running operation
// that will block execution of both the calling VCPU and the hypervisor itself.
// That includes waiting for interrupts on behalf of the VCPU (without switching
// to hypervisor idle), and service calls forwarded to higher privilege levels.
//
// This function returns the same result as vcpu_pending_wakeup(). Additionally,
// if it returns false, it may also reconfigure hardware-virtualised wakeup
// sources to be able to readily interrupt an operation at higher privilege
// levels if they would not be able to do that normally. In this case, the
// caller must also call vcpu_block_finish() to clean up after the operation
// returns or is interrupted.
//
// This must be called with preemption disabled. If it returns false, preemption
// must remain disabled until vpcu_block_finish() returns.
bool
vcpu_block_start(void) REQUIRE_PREEMPT_DISABLED;

// Clean up after blocking hypervisor execution on behalf of the current VCPU.
//
// This must be called exactly once for every call to vcpu_block_start() that
// returns false, prior to re-enabling preemption. It must not be called after
// a call to vcpu_block_start() returns true.
void
vcpu_block_finish(void) REQUIRE_PREEMPT_DISABLED;

// Return the VCPU's general purpose registers
register_t
vcpu_gpr_read(thread_t *thread, uint8_t reg_num);

// Update the VCPU's general purpose registers
void
vcpu_gpr_write(thread_t *thread, uint8_t reg_num, register_t value);
