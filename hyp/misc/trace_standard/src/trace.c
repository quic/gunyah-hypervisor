// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <cpulocal.h>
#include <hyp_aspace.h>
#include <panic.h>
#include <partition.h>
#include <platform_mem.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/prefetch.h>
#include <asm/timestamp.h>

#include "event_handlers.h"
#include "trace_helpers.h"

static_assert((uintmax_t)PLATFORM_MAX_CORES <
		      ((uintmax_t)1 << TRACE_INFO_CPU_ID_BITS),
	      "CPU-ID does not fit in info");
static_assert((uintmax_t)ENUM_TRACE_ID_MAX_VALUE <
		      ((uintmax_t)1 << TRACE_TAG_TRACE_ID_BITS),
	      "Trace ID does not fit in tag");
static_assert(TRACE_BUFFER_ENTRY_SIZE == TRACE_BUFFER_HEADER_SIZE,
	      "Trace header should be the same size as an entry");

trace_control_t hyp_trace = { .magic = TRACE_MAGIC, .version = TRACE_VERSION };
register_t	trace_public_class_flags;

extern trace_buffer_header_t trace_boot_buffer;

CPULOCAL_DECLARE_STATIC(trace_buffer_header_t *, trace_buffer);
static trace_buffer_header_t *trace_buffer_global;

// Tracing API
//
// A set of function help to log trace easily. The macro TRACE can help to
// construct the correct parameter to call the API.

static void
trace_init_common(partition_t *partition, void *base, size_t size,
		  count_t buffer_count, trace_buffer_header_t *tbuffers[])
{
	count_t global_entries, local_entries;

	assert(size != 0);
	assert(base != NULL);
	assert(buffer_count != 0);

	if (buffer_count == 1) {
		// Allocate all the area to the global buffer
		global_entries = (count_t)(size / TRACE_BUFFER_ENTRY_SIZE);
		local_entries  = 0;
	} else {
		// Ensure the count is one global buffer + one per each CPU
		assert(buffer_count == TRACE_BUFFER_NUM);
		// Ensure the size left for the global buffer is at least equal
		// to the size reserved for each local buffer
		assert(size >= (PER_CPU_TRACE_ENTRIES *
				TRACE_BUFFER_ENTRY_SIZE * TRACE_BUFFER_NUM));
		global_entries = (count_t)(size / TRACE_BUFFER_ENTRY_SIZE) -
				 (PER_CPU_TRACE_ENTRIES * PLATFORM_MAX_CORES);
		local_entries = PER_CPU_TRACE_ENTRIES;
	}

	hyp_trace.header = (trace_buffer_header_t *)base;
	hyp_trace.header_phys =
		partition_virt_to_phys(partition, (uintptr_t)base);

	count_t		       entries;
	trace_buffer_header_t *ptr = (trace_buffer_header_t *)base;
	for (count_t i = 0U; i < buffer_count; i++) {
		if (i == 0) {
			entries = global_entries;
		} else {
			entries = local_entries;
		}
		trace_buffer_header_t *tb = ptr;
		ptr += entries;
		memset(tb, 0, sizeof(*tb));

		tb->buf_magic	= TRACE_MAGIC_BUFFER;
		tb->entries	= entries - 1U;
		tb->not_wrapped = true;

		atomic_init(&tb->head, 0);

		tbuffers[i] = tb;
	}

	hyp_trace.num_bufs = buffer_count;
	// Total size of the trace buffer, in units of 64 bytes.
	hyp_trace.area_size_64 = (uint32_t)(size / 64);
}

void
trace_boot_init(void)
{
	register_t flags = 0U;

	hyp_trace.flags = trace_control_flags_default();
	trace_control_flags_set_format(&hyp_trace.flags, TRACE_FORMAT);

	// Default to enable trace buffer and error traces
	TRACE_SET_CLASS(flags, ERROR);
#if !defined(NDEBUG)
	TRACE_SET_CLASS(flags, TRACE_BUFFER);
#endif
#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
	TRACE_SET_CLASS(flags, DEBUG);
#if !defined(UNITTESTS) || !UNITTESTS
	TRACE_SET_CLASS(flags, USER);
#endif
#endif
	atomic_init(&hyp_trace.enabled_class_flags, flags);

	// Setup internal flags that cannot be changed by hypercalls
	trace_public_class_flags = ~(0U);
	TRACE_CLEAR_CLASS(trace_public_class_flags, LOG_BUFFER);
	TRACE_CLEAR_CLASS(trace_public_class_flags, LOG_TRACE_BUFFER);

	trace_init_common(partition_get_private(), &trace_boot_buffer,
			  TRACE_BOOT_ENTRIES * TRACE_BUFFER_ENTRY_SIZE, 1U,
			  &trace_buffer_global);
}

void
trace_init(partition_t *partition, size_t size)
{
	assert(size != 0);

	void_ptr_result_t alloc_ret = partition_alloc(
		partition, size, alignof(trace_buffer_header_t));
	if (alloc_ret.e != OK) {
		panic("Error allocating trace buffer");
	}

	trace_buffer_header_t *tbs[TRACE_BUFFER_NUM];
	trace_init_common(partition, alloc_ret.r, size, TRACE_BUFFER_NUM, tbs);
	// The global buffer will be the first, followed by the local buffers
	trace_buffer_global = tbs[0];
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		bitmap_set(tbs[i + 1]->cpu_mask, i);
		// The global buffer is first, hence the increment by 1
		CPULOCAL_BY_INDEX(trace_buffer, i) = tbs[i + 1];
	}

	// Copy the log entries from the boot trace into the newly allocated
	// trace of the boot CPU, which is the current CPU
	cpu_index_t	       cpu_id = cpulocal_get_index();
	trace_buffer_header_t *trace_buffer =
		CPULOCAL_BY_INDEX(trace_buffer, cpu_id);
	assert(trace_boot_buffer.entries < trace_buffer->entries);

	trace_buffer_header_t *tb = &trace_boot_buffer;
	index_t head = atomic_load_explicit(&tb->head, memory_order_relaxed);
	size_t	cpy_size = head * sizeof(trace_buffer_entry_t);

	if (cpy_size) {
		trace_buffer_entry_t *src_buf =
			(trace_buffer_entry_t *)((uintptr_t)tb +
						 TRACE_BUFFER_HEADER_SIZE);
		trace_buffer_entry_t *dst_buf =
			(trace_buffer_entry_t *)((uintptr_t)trace_buffer +
						 TRACE_BUFFER_HEADER_SIZE);

		memcpy(dst_buf, src_buf, cpy_size);

		CACHE_CLEAN_INVALIDATE_RANGE(dst_buf, cpy_size);
	}

	atomic_store_release(&trace_buffer->head, head);
}

// Log a trace with specified trace class.
//
// id: ID of this trace event.
// argn: information to store for this trace.
void
trace_standard_handle_trace_log(trace_id_t id, trace_action_t action,
				const char *fmt, register_t arg0,
				register_t arg1, register_t arg2,
				register_t arg3, register_t arg4)
{
	trace_buffer_header_t *tb;
	trace_info_t	       trace_info;
	trace_tag_t	       trace_tag;
	index_t		       head, entries;

	cpu_index_t cpu_id;
	uint64_t    timestamp;

	// Add the data to the trace buffer only if:
	// - The requested action is tracing, and tracing is enabled, or
	// - The requested action is logging, and putting log messages in the
	// trace buffer is enabled.
	bool	   trace_action = ((action == TRACE_ACTION_TRACE) ||
				   (action == TRACE_ACTION_TRACE_LOCAL) ||
				   (action == TRACE_ACTION_TRACE_AND_LOG));
	bool	   log_action	= ((action == TRACE_ACTION_LOG) ||
			   (action == TRACE_ACTION_TRACE_AND_LOG));
	register_t class_flags	= trace_get_class_flags();
	if (compiler_unexpected(
		    ((!trace_action ||
		      ((class_flags & TRACE_CLASS_BITS(TRACE_BUFFER)) == 0))) &&
		    ((!log_action ||
		      ((class_flags & TRACE_CLASS_BITS(TRACE_LOG_BUFFER)) ==
		       0))))) {
		goto out;
	}

	cpu_id	  = cpulocal_get_index();
	timestamp = arch_get_timestamp();

	trace_info_init(&trace_info);
	trace_info_set_cpu_id(&trace_info, cpu_id);
	trace_info_set_timestamp(&trace_info, timestamp);

	trace_tag_init(&trace_tag);
	trace_tag_set_trace_id(&trace_tag, id);
#if TRACE_FORMAT == 1
	thread_t *thread = thread_get_self();
	trace_tag_set_trace_ids(&trace_tag, trace_ids_raw(thread->trace_ids));
#else
#error unsupported format
#endif

	// Use the local buffer if the requested action is TRACE_LOCAL and we
	// are not still using the boot trace
	trace_buffer_header_t *cpu_tb = CPULOCAL_BY_INDEX(trace_buffer, cpu_id);
	if ((action == TRACE_ACTION_TRACE_LOCAL) && (cpu_tb != NULL)) {
		tb = cpu_tb;
	} else {
		tb = trace_buffer_global;
	}

	entries = tb->entries;

	// Atomically grab the next entry in the buffer
	head = atomic_fetch_add_explicit(&tb->head, 1, memory_order_consume);
	if (compiler_unexpected(head >= entries)) {
		index_t new_head = head + 1;

		tb->not_wrapped = false;
		head -= entries;

		(void)atomic_compare_exchange_strong_explicit(
			&tb->head, &new_head, head + 1, memory_order_relaxed,
			memory_order_relaxed);
	}

	trace_buffer_entry_t *buffers =
		(trace_buffer_entry_t *)((uintptr_t)tb +
					 TRACE_BUFFER_HEADER_SIZE);

#if defined(ARCH_ARM) && defined(ARCH_IS_64BIT) && ARCH_IS_64BIT
	// Store using non-temporal store instructions. Also, if the entry
	// fits within a DC ZVA block (typically one cache line), then zero
	// the entry first so the CPU doesn't waste time filling the cache;
	// and if the entry covers an entire cache line, flush it immediately
	// so it doesn't hang around if stnp is ineffective (as the manuals
	// suggest is the case for Cortex-A7x).
	__asm__ volatile(
#if ((1 << CPU_DCZVA_BITS) <= TRACE_BUFFER_ENTRY_SIZE) &&                      \
	((1 << CPU_DCZVA_BITS) <= TRACE_BUFFER_ENTRY_ALIGN)
		"dc zva, %[entry_addr];"
#endif
		"stnp %[info], %[tag], [%[entry_addr], 0];"
		"stnp %[fmt], %[arg0], [%[entry_addr], 16];"
		"stnp %[arg1], %[arg2], [%[entry_addr], 32];"
		"stnp %[arg3], %[arg4], [%[entry_addr], 48];"
#if ((1 << CPU_L1D_LINE_BITS) <= TRACE_BUFFER_ENTRY_SIZE) &&                   \
	((1 << CPU_L1D_LINE_BITS) <= TRACE_BUFFER_ENTRY_ALIGN)
		"dc civac, %[entry_addr];"
#endif
		: [entry] "=m"(buffers[head])
		: [entry_addr] "r"(&buffers[head]),
		  [info] "r"(trace_info_raw(trace_info)),
		  [tag] "r"(trace_tag_raw(trace_tag)), [fmt] "r"(fmt),
		  [arg0] "r"(arg0), [arg1] "r"(arg1), [arg2] "r"(arg2),
		  [arg3] "r"(arg3), [arg4] "r"(arg4));
#else
	prefetch_store_stream(&buffers[head]);

	buffers[head].info    = trace_info;
	buffers[head].tag     = trace_tag;
	buffers[head].fmt     = fmt;
	buffers[head].args[0] = arg0;
	buffers[head].args[1] = arg1;
	buffers[head].args[2] = arg2;
	buffers[head].args[3] = arg3;
	buffers[head].args[4] = arg4;
#endif

out:
	return;
}

void
trace_set_class_flags(register_t flags)
{
	atomic_fetch_or_explicit(&hyp_trace.enabled_class_flags, flags,
				 memory_order_relaxed);
}

void
trace_clear_class_flags(register_t flags)
{
	atomic_fetch_and_explicit(&hyp_trace.enabled_class_flags, ~flags,
				  memory_order_relaxed);
}

void
trace_update_class_flags(register_t set_flags, register_t clear_flags)
{
	register_t flags = atomic_load_explicit(&hyp_trace.enabled_class_flags,
						memory_order_relaxed);

	register_t new_flags;
	do {
		new_flags = flags & ~clear_flags;
		new_flags |= set_flags;
	} while (!atomic_compare_exchange_strong_explicit(
		&hyp_trace.enabled_class_flags, &flags, new_flags,
		memory_order_relaxed, memory_order_relaxed));
}

register_t
trace_get_class_flags()
{
	return atomic_load_explicit(&hyp_trace.enabled_class_flags,
				    memory_order_relaxed);
}
