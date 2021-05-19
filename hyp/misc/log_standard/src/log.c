// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdatomic.h>
#include <string.h>

#include <hypconstants.h>

#include <compiler.h>
#include <trace.h>
#include <util.h>

#include <events/log.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include <asm-generic/prefetch.h>

#include "event_handlers.h"
#include "string_util.h"
#include "trace_helpers.h"

// FIXME: the log temporary buffer is placed on the stack
// Note: thread_stack_size_default = 4096
#define LOG_TEMP_BUFFER_SIZE 256

extern char hyp_log_buffer[];
char	    hyp_log_buffer[LOG_BUFFER_SIZE];

// Global visibility - for debug
extern log_control_t hyp_log;

log_control_t hyp_log = { .log_magic   = LOG_MAGIC,
			  .head	       = 0,
			  .buffer_size = LOG_BUFFER_SIZE,
			  .log_buffer  = hyp_log_buffer };

void
log_init()
{
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, LOG_BUFFER);
	trace_set_class_flags(flags);
}

void
log_standard_handle_trace_log(trace_id_t id, trace_action_t action,
			      const char *fmt, register_t arg0, register_t arg1,
			      register_t arg2, register_t arg3, register_t arg4)
{
	index_t	      idx, prev_idx;
	size_result_t ret;
	size_t	      size;
	char	      temp_buf[LOG_TEMP_BUFFER_SIZE];

	// Add the data to the log buffer only if:
	// - The requested action is logging, and logging is enabled, or
	// - The requested action is tracing, and putting trace messages in the
	// log buffer is enabled.
	bool	   trace_action = ((action == TRACE_ACTION_TRACE) ||
				   (action == TRACE_ACTION_TRACE_AND_LOG));
	bool	   log_action	= ((action == TRACE_ACTION_LOG) ||
			   (action == TRACE_ACTION_TRACE_AND_LOG));
	register_t class_flags	= trace_get_class_flags();
	if (compiler_unexpected(
		    ((!log_action ||
		      ((class_flags & TRACE_CLASS_BITS(LOG_BUFFER)) == 0))) &&
		    ((!trace_action ||
		      ((class_flags & TRACE_CLASS_BITS(LOG_TRACE_BUFFER)) ==
		       0))))) {
		goto out;
	}

	ret = snprint(temp_buf, LOG_TEMP_BUFFER_SIZE, fmt, arg0, arg1, arg2,
		      arg3, arg4);
	if (ret.e == ERROR_STRING_TRUNCATED) {
		size = LOG_TEMP_BUFFER_SIZE;
	} else if ((ret.e == ERROR_STRING_MISSING_ARGUMENT) || (ret.r == 0U)) {
		goto out;
	} else {
		size = ret.r + 1;
	}

	trigger_log_message_event(id, temp_buf);

	/* Atomically update the index first */
	prev_idx = atomic_fetch_add_explicit(&hyp_log.head, size,
					     memory_order_relaxed);
	idx	 = prev_idx + (index_t)size;
	while (compiler_unexpected(idx >= hyp_log.buffer_size)) {
		index_t old_idx = idx;

		idx -= hyp_log.buffer_size;

		(void)atomic_compare_exchange_strong_explicit(
			&hyp_log.head, &old_idx, idx, memory_order_relaxed,
			memory_order_relaxed);

		if (compiler_unexpected(prev_idx >= hyp_log.buffer_size)) {
			prev_idx -= hyp_log.buffer_size;
		}
	}

	prefetch_store_stream(&hyp_log.log_buffer[prev_idx]);

	/* check for log wrap around, and split the string if required */
	if (compiler_expected((prev_idx < idx))) {
		memcpy(&hyp_log.log_buffer[prev_idx], temp_buf, size);
		CACHE_CLEAN_RANGE(&hyp_log.log_buffer[prev_idx], size);
	} else {
		size_t first_part = hyp_log.buffer_size - (size_t)prev_idx;
		memcpy(&hyp_log.log_buffer[prev_idx], temp_buf, first_part);
		CACHE_CLEAN_RANGE(&hyp_log.log_buffer[prev_idx], first_part);

		size_t remaining = size - first_part;
		if (remaining > 0) {
			memcpy(&hyp_log.log_buffer[0], temp_buf + first_part,
			       remaining);
			CACHE_CLEAN_RANGE(&hyp_log.log_buffer[0], remaining);
		}
	}
out:
	// Nothing to do
	return;
}
