// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>

#include <attributes.h>
#include <compiler.h>
#include <log.h>
#include <panic.h>
#include <preempt.h>
#include <trace.h>

#include <events/abort.h>

#include <asm/event.h>

noreturn void NOINLINE
panic(const char *str)
{
	void *from  = __builtin_return_address(0);
	void *frame = __builtin_frame_address(0);

	preempt_disable();

	TRACE_AND_LOG(ERROR, PANIC, "Panic: {:s} from PC {:#x}, FP {:#x}",
		      (register_t)(uintptr_t)str, (register_t)(uintptr_t)from,
		      (register_t)(uintptr_t)frame);

	trigger_abort_kernel_event(ABORT_REASON_PANIC);

	while (1) {
		asm_event_wait(str);
	}
}
