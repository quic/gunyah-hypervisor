// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Note, do not call panic or assert here, they will recurse!

#include <hyptypes.h>

#include <abort.h>
#include <attributes.h>
#include <compiler.h>
#include <idle.h>
#include <ipi.h>
#include <log.h>
#include <platform_timer.h>
#include <preempt.h>
#include <thread.h>
#include <trace.h>

#include <events/abort.h>
#include <events/thread.h>

#include <asm/barrier.h>
#include <asm/event.h>

#include "event_handlers.h"

void NOINLINE
abort_handle_abort_kernel(void)
{
	assert_preempt_disabled();

	ipi_others(IPI_REASON_ABORT_STOP);

	if (!idle_is_current()) {
		trigger_thread_save_state_event();
	}

	// Delay approx 1ms to allow other cores to complete saving state.
	// We don't wait for acknowledgement since they may be unresponsive.
	uint32_t freq = platform_timer_get_frequency();

	uint64_t now = platform_timer_get_current_ticks();
	uint64_t end = now + (freq / 1024);

	while (now < end) {
		asm_yield();
		now = platform_timer_get_current_ticks();
	}
}

noreturn bool NOINLINE
abort_handle_ipi_received(void)
{
	preempt_disable();

	if (!idle_is_current()) {
		trigger_thread_save_state_event();
	}

	void *mem = NULL;
	while (1) {
		asm_event_wait(&mem);
	}
}

noreturn void NOINLINE
abort(const char *str, abort_reason_t reason)
{
	void *from  = __builtin_return_address(0);
	void *frame = __builtin_frame_address(0);

	preempt_disable();

	TRACE_AND_LOG(ERROR, PANIC, "Abort: {:s} from PC {:#x}, FP {:#x}",
		      (register_t)(uintptr_t)str, (register_t)(uintptr_t)from,
		      (register_t)(uintptr_t)frame);

	trigger_abort_kernel_event(reason);

	while (1) {
		asm_event_wait(str);
	}
}
