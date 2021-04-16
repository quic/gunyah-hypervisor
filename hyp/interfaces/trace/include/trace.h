// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Trace interface and helper macros
//
// Traces are enabled and disabled by trace class. There up to 64 classes, and
// they are mapped to corresponding bits in a global register_t sized value.
//
// Trace ID is used to identify the trace event. There is no correlation between
// trace-id and trace-class, the caller shall select the class, or classes
// they wish the trace event to be dependent on.
//
// Note, some trace classes may be used internally by implementations, for
// example TRACE_BUFFER or TRACE_ADD classes.

#include <events/trace.h>

#define TRACE_ID(id)		 (TRACE_ID_##id)
#define TRACE_CLASS(tclass)	 (TRACE_CLASS_##tclass)
#define TRACE_CLASS_BITS(tclass) (1U << TRACE_CLASS_##tclass)

#define TRACE_FUNC_I(id, action, a0, a1, a2, a3, a4, a5, n, ...)               \
	TRACE_ADD##n(TRACE_ID(id), action, a0, a1, a2, a3, a4, a5, __VA_ARGS__)
#define TRACE_FUNC(...)                                                        \
	TRACE_FUNC_I(__VA_ARGS__, 6, 5, 4, 3, 2, 1, 0, _unspecified_id)

extern trace_control_t hyp_trace;
extern register_t      trace_public_class_flags;

#define TRACE_MAYBE(classes, X)                                                \
	do {                                                                   \
		register_t enabled = atomic_load_explicit(                     \
			&hyp_trace.enabled_class_flags, memory_order_relaxed); \
		if (compiler_unexpected((enabled & classes) != 0)) {           \
			X;                                                     \
		}                                                              \
	} while (0)

// Used for single class trace
#define TRACE(tclass, id, ...)                                                 \
	TRACE_EVENT(tclass, id, TRACE_ACTION_TRACE, __VA_ARGS__)

#define TRACE_LOCAL(tclass, id, ...)                                           \
	TRACE_EVENT(tclass, id, TRACE_ACTION_TRACE_LOCAL, __VA_ARGS__)

#define TRACE_EVENT(tclass, id, action, ...)                                   \
	TRACE_MAYBE(TRACE_CLASS_BITS(tclass),                                  \
		    TRACE_FUNC(id, action, __VA_ARGS__))

#define TRACE_ADD0(id, action, ...)                                            \
	trigger_trace_log_event(id, action, 0, 0, 0, 0, 0, 0)

#define TRACE_ADD1(id, action, a1, ...)                                        \
	trigger_trace_log_event(id, action, a1, 0, 0, 0, 0, 0)

#define TRACE_ADD2(id, action, a1, a2, ...)                                    \
	trigger_trace_log_event(id, action, a1, a2, 0, 0, 0, 0)

#define TRACE_ADD3(id, action, a1, a2, a3, ...)                                \
	trigger_trace_log_event(id, action, a1, a2, a3, 0, 0, 0)

#define TRACE_ADD4(id, action, a1, a2, a3, a4, ...)                            \
	trigger_trace_log_event(id, action, a1, a2, a3, a4, 0, 0)

#define TRACE_ADD5(id, action, a1, a2, a3, a4, a5, ...)                        \
	trigger_trace_log_event(id, action, a1, a2, a3, a4, a5, 0)

#define TRACE_ADD6(id, action, a1, a2, a3, a4, a5, a6, ...)                    \
	trigger_trace_log_event(id, action, a1, a2, a3, a4, a5, a6)

// Enable a set of trace classes.
//
// flags: the new flags to be enabled.
void
trace_set_class_flags(register_t flags);

// Disable a set of trace classes.
//
// flags: the flags to be disabled.
void
trace_clear_class_flags(register_t flags);

// Atomically update a set of trace classes.
//
// set_flags: the flags to be enabled.
// clear_flags: the flags to be disabled.
//
// Note: flags both set and cleared will remain set.
void
trace_update_class_flags(register_t set_flags, register_t clear_flags);

// Return the current status of trace classes.
register_t
trace_get_class_flags(void);

// Allocate and relocate trace buffer
//
// It stops using the trace boot buffer and starts using a dynamically allocated
// trace buffer of bigger size
void
trace_init(partition_t *partition, size_t size);
