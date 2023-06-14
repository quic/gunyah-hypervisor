// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <stdatomic.h>
#include <string.h>

#include <hypconstants.h>
#include <hypregisters.h>

#include <compiler.h>
#include <cpulocal.h>
#include <platform_timer.h>
#include <trace.h>
#include <util.h>

#include <events/log.h>

#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/prefetch.h>

#include "event_handlers.h"
#include "string_util.h"
#include "trace_helpers.h"

// FIXME: the log temporary buffer is placed on the stack
// Note: thread_stack_size_default = 4096
#define LOG_TIMESTAMP_BUFFER_SIZE 24U
#define LOG_ENTRY_BUFFER_SIZE	  256U

extern char hyp_log_buffer[];
char	    hyp_log_buffer[LOG_BUFFER_SIZE];

static_assert(LOG_BUFFER_SIZE > LOG_ENTRY_BUFFER_SIZE,
	      "LOG_BUFFER_SIZE too small");

// Global visibility - for debug
extern log_control_t hyp_log;

log_control_t hyp_log = { .log_magic   = LOG_MAGIC,
			  .head	       = 0,
			  .buffer_size = LOG_BUFFER_SIZE,
			  .log_buffer  = hyp_log_buffer };

void
log_init(void)
{
	register_t flags = 0U;
	TRACE_SET_CLASS(flags, LOG_BUFFER);
	trace_set_class_flags(flags);

	assert_debug(hyp_log.buffer_size == (index_t)LOG_BUFFER_SIZE);
}

void
log_standard_handle_trace_log(trace_id_t id, trace_action_t action,
			      const char *fmt, register_t arg0, register_t arg1,
			      register_t arg2, register_t arg3, register_t arg4)
{
	index_t	      next_idx, prev_idx, orig_idx;
	size_result_t ret;
	size_t	      entry_size, timestamp_size;
	char	      entry_buf[LOG_ENTRY_BUFFER_SIZE];

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

	// Add the time-stamp and core number
	// We could use platform_convert_ticks_to_ns() here, but the resulting
	// values will be too big and unwieldy to use. Since log formatting is a
	// slow process anyway, might as well add some nice time-stamps.
	ticks_t	      now = platform_timer_get_current_ticks();
	nanoseconds_t ns  = platform_convert_ticks_to_ns(now);

	microseconds_t usec = ns / (microseconds_t)1000;
	uint64_t       sec  = usec / TIMER_MICROSECS_IN_SECOND;

	ret = snprint(entry_buf, LOG_TIMESTAMP_BUFFER_SIZE,
		      "{:d} {:4d}.{:06d} ", cpulocal_get_index_unsafe(), sec,
		      usec % TIMER_MICROSECS_IN_SECOND, 0, 0);
	if (ret.e == ERROR_STRING_TRUNCATED) {
		// The truncated string will have a NULL terminator, remove it
		timestamp_size = LOG_TIMESTAMP_BUFFER_SIZE - 1U;
	} else if (ret.e != OK) {
		// Should not happen
		goto out;
	} else {
		// Do not count the NULL character since we will write over it
		// shortly.
		timestamp_size = ret.r;
	}

	// Add the log message after the time-stamp
	ret = snprint(entry_buf + timestamp_size,
		      LOG_ENTRY_BUFFER_SIZE - timestamp_size, fmt, arg0, arg1,
		      arg2, arg3, arg4);
	if (ret.e == ERROR_STRING_TRUNCATED) {
		entry_size = LOG_ENTRY_BUFFER_SIZE;
	} else if ((ret.e == ERROR_STRING_MISSING_ARGUMENT) || (ret.r == 0U)) {
		goto out;
	} else {
		entry_size = timestamp_size + ret.r + 1U;
		assert(entry_size <= LOG_ENTRY_BUFFER_SIZE);
	}

	const index_t buffer_size = (index_t)LOG_BUFFER_SIZE;

	// Inform the subscribers of the entry without the time-stamp
	trigger_log_message_event(id, entry_buf + timestamp_size);

	// Atomically update the index first
	orig_idx = atomic_fetch_add_explicit(&hyp_log.head, entry_size,
					     memory_order_relaxed);
	prev_idx = orig_idx;
	while (compiler_unexpected(prev_idx >= buffer_size)) {
		prev_idx -= buffer_size;
	}

	next_idx = orig_idx + (index_t)entry_size;
	// If we wrap, something is really wrong
	assert(next_idx > orig_idx);

	if (compiler_unexpected(next_idx >= buffer_size)) {
		index_t old_idx = next_idx;

		while (next_idx >= buffer_size) {
			next_idx -= buffer_size;
		}

		// Try to reduce the index in the shared variable so it is no
		// longer overflowed. We don't care if it fails because that
		// means somebody else has done it concurrently.
		(void)atomic_compare_exchange_strong_explicit(
			&hyp_log.head, &old_idx, next_idx, memory_order_relaxed,
			memory_order_relaxed);
	}

	prefetch_store_stream(&hyp_log.log_buffer[prev_idx]);

	size_t buf_remaining = (size_t)buffer_size - (size_t)prev_idx;

	// Copy the whole entry if it fits
	if (compiler_expected(buf_remaining >= entry_size)) {
		(void)memcpy(&hyp_log.log_buffer[prev_idx], entry_buf,
			     entry_size);
		CACHE_CLEAN_RANGE(&hyp_log.log_buffer[prev_idx], entry_size);
	} else {
		// Otherwise copy the first bit of entry to the tail of the
		// buffer and wrap to the start for the remainder.
		size_t first_part = buf_remaining;
		(void)memcpy(&hyp_log.log_buffer[prev_idx], entry_buf,
			     first_part);
		CACHE_CLEAN_RANGE(&hyp_log.log_buffer[prev_idx], first_part);

		size_t second_part = entry_size - first_part;

		(void)memcpy(&hyp_log.log_buffer[0], entry_buf + first_part,
			     second_part);
		CACHE_CLEAN_RANGE(&hyp_log.log_buffer[0], second_part);
	}
out:
	// Nothing to do
	return;
}
