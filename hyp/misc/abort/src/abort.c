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
#include <preempt.h>
#include <thread.h>
#include <trace.h>

#include <events/abort.h>
#include <events/scheduler.h>
#include <events/thread.h>

#include <asm/event.h>

#include "event_handlers.h"

void NOINLINE
abort_handle_scheduler_stop(void)
{
	if (!idle_is_current()) {
		trigger_thread_save_state_event();
	}
}

noreturn void NOINLINE
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

noreturn void NOINLINE COLD
abort(const char *str, abort_reason_t reason) LOCK_IMPL
{
	void *from  = __builtin_return_address(0);
	void *frame = __builtin_frame_address(0);

	// Stop all cores and disable preemption
	trigger_scheduler_stop_event();

#if defined(ARCH_ARM_FEAT_PAuth)
	__asm__("xpaci %0;" : "+r"(from));
#endif
	TRACE_AND_LOG(ERROR, PANIC, "Abort: {:s} from PC {:#x}, FP {:#x}",
		      (register_t)(uintptr_t)str, (register_t)(uintptr_t)from,
		      (register_t)(uintptr_t)frame);

	trigger_abort_kernel_event(reason);

	while (1) {
		asm_event_wait(str);
	}
}
