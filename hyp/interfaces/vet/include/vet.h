// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// The Virtual Embedded Trace (VET) interface.

// The vet_ordering variable is used as an artificial assembly ordering
// dependency for modules implementing this API. It orders individual asm
// statements with respect to each other in a way that is lighter weight than a
// full "memory" clobber.
extern asm_ordering_dummy_t vet_ordering;

// Flush data for trace unit.
//
// Since a HW trace unit may have delays in transferring the trace byte stream
// to system infrastructure, we may need to explicitly flush it to ensure the
// trace stream is observable (mostly the trace buffer unit).
void
vet_flush_trace(thread_t *self);

// Disable trace unit.
//
// Trace unit should be configured to not generate additional trace data after
// disabling.
void
vet_disable_trace(void);

// Enable trace unit.
void
vet_enable_trace(void);

// Save current trace unit's thread context.
//
// After thread context is saved, access to the trace unit registers is
// disabled.
//
// The implementation depends on the configured policy. This can save all
// registers or just control the trace's enable/disable.
void
vet_save_trace_thread_context(thread_t *self) REQUIRE_PREEMPT_DISABLED;

// Restore a thread's trace buffer unit context.
//
// This reverses the actions of vet_save_trace_thread_context.
void
vet_restore_trace_thread_context(thread_t *self) REQUIRE_PREEMPT_DISABLED;

// Save trace unit context for local CPU before suspend.
//
// Note that this may modify the trace unit state, so an aborted suspend must
// be followed by a call to vet_restore_trace_power_context().
void
vet_save_trace_power_context(bool may_poweroff) REQUIRE_PREEMPT_DISABLED;

// Restore trace unit context for local CPU after resume or aborted suspend.
void
vet_restore_trace_power_context(bool was_poweroff) REQUIRE_PREEMPT_DISABLED;

// Flush data in the trace buffer unit.
//
// After this flush, all data pending in the trace buffer should be committed
// to memory. The implementation should ensure that this completes in finite
// time. If the trace buffer is located in memory with normal non-cacheable or
// device memory attributes, the write of trace data reaches the endpoint of
// that location in finite time.
void
vet_flush_buffer(thread_t *self);

// Disable trace buffer unit.
//
// After disabling the trace buffer, it still host software stack's
// responsibility to check if all data is written out to the buffer.
void
vet_disable_buffer(void);

// Enable trace buffer unit.
void
vet_enable_buffer(void);

// Save trace buffer unit thread context before power-off.
//
// Similar to vet_save_trace_thread_context, this may save trace buffer
// registers / information. However, it does not change any configuration
// and does not need to be called for non-poweroff suspends.
void
vet_save_buffer_thread_context(thread_t *self) REQUIRE_PREEMPT_DISABLED;

// Restore trace buffer unit thread context after power-off.
//
// This must be called when resuming from a power-off state. It need not be
// called when resuming from a retention state or aborting a power-off suspend.
void
vet_restore_buffer_thread_context(thread_t *self) REQUIRE_PREEMPT_DISABLED;

// Save trace buffer context for local CPU before power-off.
//
// This does not need to save any information which is already saved by thread
// context.
// NOTE: if register access is disabled, then we need to enable it before save/
// restore of the context.
void
vet_save_buffer_power_context(void) REQUIRE_PREEMPT_DISABLED;

// Restore trace buffer context for local CPU after power-on.
void
vet_restore_buffer_power_context(void) REQUIRE_PREEMPT_DISABLED;

// Update trace unit status for the current thread.
//
// This function checks the thread's current usage of trace infrastructure
// to guide the subsequent context-switch behaviour such as saving context.
void
vet_update_trace_unit_status(thread_t *self);

// Update trace buffer status for the current thread
//
// Similar to vet_update_trace_unit_status() for the trace buffer.
void
vet_update_trace_buffer_status(thread_t *self);
