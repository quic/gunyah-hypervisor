// © 2022 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdio.h>
#include <stdlib.h>
#undef register_t

#include <compiler.h>
#include <gpt.h>
#include <log.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"
#include "string_util.h"
#include "trace_helpers.h"

gpt_t		gpt;
partition_t	host_partition;
trace_control_t hyp_trace;

void
assert_failed(const char *file, int line, const char *func, const char *err)
{
	printf("Assert failed in %s at %s:%d: %s\n", func, file, line, err);
	exit(-1);
}

void
panic(const char *str)
{
	printf("Panic: %s\n", str);
	exit(-1);
}

static void
trace_and_log_init(void)
{
	register_t flags = 0U;

	TRACE_SET_CLASS(flags, ERROR);
	TRACE_SET_CLASS(flags, TRACE_BUFFER);
	TRACE_SET_CLASS(flags, DEBUG);

	atomic_init(&hyp_trace.enabled_class_flags, flags);
}

void
trigger_trace_log_event(trace_id_t id, trace_action_t action, const char *arg0,
			register_t arg1, register_t arg2, register_t arg3,
			register_t arg4, register_t arg5)
{
	char log[1024];
	(void)snprint(log, util_array_size(log), arg0, arg1, arg2, arg3, arg4,
		      arg5);
	puts(log);
}

partition_t *
object_get_partition_additional(partition_t *partition)
{
	assert(partition != NULL);

	return partition;
}

void
object_put_partition(partition_t *partition)
{
	assert(partition != NULL);
}

partition_t *
partition_get_root(void)
{
	return &host_partition;
}

void_ptr_result_t
partition_alloc(partition_t *partition, size_t bytes, size_t min_alignment)
{
	assert(partition != NULL);
	assert(bytes > 0U);

	void *mem = aligned_alloc(min_alignment, bytes);

	return (mem != NULL) ? void_ptr_result_ok(mem)
			     : void_ptr_result_error(ERROR_NOMEM);
}

error_t
partition_free(partition_t *partition, void *mem, size_t bytes)
{
	assert(partition != NULL);
	assert(bytes > 0U);

	free(mem);

	return OK;
}

void
preempt_disable(void)
{
}

void
preempt_enable(void)
{
}

void
rcu_read_start(void)
{
}

void
rcu_read_finish(void)
{
}

void
rcu_enqueue(rcu_entry_t *rcu_entry, rcu_update_class_t rcu_update_class)
{
	assert(rcu_update_class == RCU_UPDATE_CLASS_GPT_FREE_LEVEL);

	(void)gpt_handle_rcu_free_level(rcu_entry);
}

cpu_index_t
cpulocal_check_index(cpu_index_t cpu)
{
	return cpu;
}

cpu_index_t
cpulocal_get_index_unsafe(void)
{
	return 0U;
}

void
trigger_gpt_value_add_offset_event(gpt_type_t type, gpt_value_t *value,
				   size_t offset)
{
	if ((type == GPT_TYPE_TEST_A) || (type == GPT_TYPE_TEST_B) ||
	    (type == GPT_TYPE_TEST_C)) {
		gpt_tests_add_offset(type, value, offset);
	} else {
		// Nothing to do
	}
}

bool
trigger_gpt_values_equal_event(gpt_type_t type, gpt_value_t x, gpt_value_t y)
{
	bool ret;

	if ((type == GPT_TYPE_TEST_A) || (type == GPT_TYPE_TEST_B) ||
	    (type == GPT_TYPE_TEST_C)) {
		ret = gpt_tests_values_equal(x, y);
	} else if (GPT_TYPE_EMPTY) {
		ret = gpt_handle_empty_values_equal();
	} else {
		ret = false;
	}

	return ret;
}

error_t
trigger_gpt_walk_callback_event(gpt_callback_t callback, gpt_entry_t entry,
				size_t base, size_t size, gpt_arg_t arg)
{
	error_t ret;

	if (callback == GPT_CALLBACK_RESERVED) {
		gpt_handle_reserved_callback();
	} else if (callback == GPT_CALLBACK_TEST) {
		ret = gpt_tests_callback(entry, base, size, arg);
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}

	return ret;
}

int
main(void)
{
	trace_and_log_init();

	gpt_handle_tests_init();

	gpt_handle_tests_start();

	return 0;
}
