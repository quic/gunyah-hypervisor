// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if !defined(NDEBUG) && !defined(__KLOCWORK__)
#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <attributes.h>
#include <compiler.h>
#include <log.h>
#include <preempt.h>
#include <trace.h>

#include <events/abort.h>

#include <asm/event.h>

noreturn void NOINLINE
assert_failed(const char *file, int line, const char *func, const char *err)
{
	const char *file_short;

	preempt_disable();

	size_t len = strlen(file);
	if (len < 64) {
		file_short = file;
	} else {
		file_short = file + len - 64;

		char *file_strchr = strchr(file_short, '/');
		if (file_strchr != NULL) {
			file_short = file_strchr + 1;
		}
	}

	TRACE_AND_LOG(ERROR, ASSERT_FAILED,
		      "Assert failed in {:s} at {:s}:{:d}: {:s}",
		      (register_t)func, (register_t)(uintptr_t)file_short,
		      (register_t)(uintptr_t)line, (register_t)(uintptr_t)err);

	trigger_abort_kernel_event(ABORT_REASON_ASSERTION);

	while (1) {
		asm_event_wait(err);
	}
}
#else
extern char __empty_assert_c;
#endif
