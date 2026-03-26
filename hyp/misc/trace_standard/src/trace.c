// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
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
#if defined(INTERFACE_ROOTVM)
#include <cspace.h>
#include <doorbell.h>
#include <log.h>
#endif
#include <hyp_aspace.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_cpu.h>
#include <platform_mem.h>
#include <platform_security.h>
#include <platform_timer.h>
#include <qcbor.h>
#include <thread.h>
#include <trace.h>
#include <util.h>

#include <asm/cache.h>
#include <asm/cpu.h>
#include <asm/prefetch.h>

#include "event_handlers.h"
#include "trace_helpers.h"
#include "trace_internal.h"

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

size_t
trace_get_minimum_size(void)
{
	count_t local_buffer_count = platform_get_existing_cpus_count();

	// Ensure the size left for the global buffer is at least equal
	// to the size reserved for each local buffer
	return (size_t)PER_CPU_TRACE_ENTRIES * (size_t)TRACE_BUFFER_ENTRY_SIZE *
	       ((size_t)local_buffer_count + 1U);
}

static void
trace_init_common(void *base, paddr_t phys, size_t size, count_t buffer_count,
		  trace_buffer_header_t *tbuffers[])
{
	count_t global_entries, local_entries;

	assert(size != 0U);
	assert(base != NULL);
	assert(buffer_count != 0U);

	if (buffer_count == 1U) {
		// Allocate all the area to the global buffer
		global_entries =
			(count_t)(size / (size_t)TRACE_BUFFER_ENTRY_SIZE);
		local_entries = 0;
	} else {
		// Ensure the total count is one global buffer + one per CPU
		assert(buffer_count == (count_t)TRACE_BUFFER_NUM);

		// The actual local buffer count, one for each existing CPU
		count_t local_buffer_count = platform_get_existing_cpus_count();
		assert(local_buffer_count < buffer_count);

		// Ensure the size left for the global buffer is at least equal
		// to the size reserved for each local buffer
		assert(size >= trace_get_minimum_size());

		global_entries =
			(count_t)((size / (size_t)TRACE_BUFFER_ENTRY_SIZE) -
				  ((size_t)PER_CPU_TRACE_ENTRIES *
				   local_buffer_count));
		local_entries = PER_CPU_TRACE_ENTRIES;
	}

	hyp_trace.header      = (trace_buffer_header_t *)base;
	hyp_trace.header_phys = phys;

	count_t		       entries;
	trace_buffer_header_t *ptr = (trace_buffer_header_t *)base;
	for (count_t i = 0U; i < buffer_count; i++) {
		if (i == 0U) {
			entries = global_entries;
		} else {
			entries = local_entries;
		}

		if ((i == 0U) || platform_cpu_exists((cpu_index_t)(i - 1U))) {
			trace_buffer_header_t *tb = ptr;
			ptr += entries;

			*tb	      = (trace_buffer_header_t){ 0U };
			tb->buf_magic = TRACE_MAGIC_BUFFER;
			tb->version   = hyp_trace.version;
			tb->flags     = hyp_trace.flags;
			tb->entries   = entries - 1U;

			// Notify mask is initially disabled
			tb->notify_mask = TRACE_NOTIFY_DISABLED;

			atomic_init(&tb->head, 0);
			atomic_init(&tb->wrap_count, 0);

			tbuffers[i] = tb;
		} else {
			tbuffers[i] = NULL;
		}
	}

	hyp_trace.num_bufs = buffer_count;
	// Total size of the trace buffer, in units of 64 bytes.
	hyp_trace.area_size_64 = (uint32_t)(size / 64U);
}

void
trace_boot_init(void)
{
	register_t flags = 0U;

	hyp_trace.flags = trace_control_flags_default();
	trace_control_flags_set_format(&hyp_trace.flags,
				       (trace_format_t)TRACE_FORMAT);

	// Default to enable trace buffer and error traces
	TRACE_SET_CLASS(flags, ERROR);
#if !defined(NDEBUG)
	TRACE_SET_CLASS(flags, INFO);
#if !defined(UNIT_TESTS) || !UNIT_TESTS
	TRACE_SET_CLASS(flags, USER);
#endif
#endif
#if defined(VERBOSE_TRACE) && VERBOSE_TRACE
	TRACE_SET_CLASS(flags, DEBUG);
#endif
	atomic_init(&hyp_trace.enabled_class_flags, flags);

	// Setup internal flags that cannot be changed by hypercalls
	trace_public_class_flags = ~(register_t)0;
	TRACE_CLEAR_CLASS(trace_public_class_flags, ERROR);
	TRACE_CLEAR_CLASS(trace_public_class_flags, LOG_BUFFER);

	paddr_t phys =
		partition_image_virt_to_phys((uintptr_t)&trace_boot_buffer);
	trace_init_common(
		&trace_boot_buffer, phys,
		((size_t)TRACE_BOOT_ENTRIES * (size_t)TRACE_BUFFER_ENTRY_SIZE),
		1U, &trace_buffer_global);
}

static void
trace_buffer_init(partition_t *partition, void *base, size_t size)
	REQUIRE_PREEMPT_DISABLED
{
	assert(size != 0U);
	assert(base != NULL);

	// Only permit one update of the trace buffer
	assert(hyp_trace.header == &trace_boot_buffer);

	paddr_t phys = partition_virt_to_phys(partition, (uintptr_t)base);
	assert(phys != PADDR_INVALID);

	trace_buffer_header_t *tbs[TRACE_BUFFER_NUM];
	trace_init_common(base, phys, size, TRACE_BUFFER_NUM, tbs);
	// The global buffer will be the first, followed by the local buffers
	trace_buffer_global = tbs[0];
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		if (platform_cpu_exists(i)) {
			bitmap_set(tbs[i + 1U]->cpu_mask, i);
		}
		// The global buffer is first, hence the increment by 1
		CPULOCAL_BY_INDEX(trace_buffer, i) = tbs[i + 1U];
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

	if (cpy_size != 0U) {
		// The log entries follow on immediately after the header
		char *src_buf = (char *)(tb + 1);
		char *dst_buf = (char *)(trace_buffer + 1);

		(void)memscpy(dst_buf,
			      trace_buffer->entries *
				      sizeof(trace_buffer_entry_t),
			      src_buf, cpy_size);

		CACHE_CLEAN_INVALIDATE_RANGE(dst_buf, cpy_size);
	}

	atomic_store_release(&trace_buffer->head, head);
}

#if defined(PLATFORM_TRACE_STANDALONE_REGION) &&                               \
	PLATFORM_TRACE_STANDALONE_REGION
void
trace_single_region_init(partition_t *partition, void *base, size_t size)
{
	assert(size != 0);
	assert(base != NULL);

	// Call to initialize the trace buffer
	trace_buffer_init(partition, base, size);
}
#else

void
trace_init(partition_t *partition, size_t size)
{
	assert(size != 0U);

	void_ptr_result_t alloc_ret = partition_alloc(
		partition, size, alignof(trace_buffer_header_t));
	if (alloc_ret.e != OK) {
		panic("Error allocating trace buffer");
	}

	paddr_t phys =
		partition_virt_to_phys(partition, (uintptr_t)alloc_ret.r);

	error_t	     ret;
	partition_t *hyp_partition = partition_get_private();

	// Tag this region as trace memory
	ret = memdb_update(hyp_partition, phys, phys + (size - 1U),
			   (uintptr_t)NULL, MEMDB_TYPE_TRACE,
			   (uintptr_t)&partition->allocator,
			   MEMDB_TYPE_ALLOCATOR);
	if (ret != OK) {
		panic("memdb_update");
	}

	// Call to initialize the trace buffer
	trace_buffer_init(partition, alloc_ret.r, size);
}
#endif

#if defined(INTERFACE_ROOTVM)

void
trace_standard_handle_rootvm_init(partition_t	   *root_partition,
				  cspace_t	   *root_cspace,
				  qcbor_enc_ctxt_t *qcbor_enc_ctxt)
{
	cap_id_t me_cap;

	paddr_t trace_phys = hyp_trace.header_phys;
	size_t	trace_size = (size_t)hyp_trace.area_size_64 * 64U;

	// If debug is disabled, don't expose the trace buffer
	if (platform_security_state_debug_disabled()) {
		goto out;
	}

	// Move trace back to partition
	error_t err = memdb_update(partition_get_private(), trace_phys,
				   trace_phys + (trace_size - 1U),
				   (uintptr_t)root_partition,
				   MEMDB_TYPE_PARTITION, (uintptr_t)NULL,
				   MEMDB_TYPE_TRACE);
	if (err != OK) {
		LOG(DEBUG, INFO, "convert trace to memextent failed: {:d}",
		    (register_t)err);
		goto out;
	}

	memextent_ptr_result_t me_ret = memextent_construct(
		root_partition, root_cspace, trace_phys, trace_size,
		PGTABLE_ACCESS_R, MEMEXTENT_MEMTYPE_ANY, MEMEXTENT_TYPE_BASIC,
		false, &me_cap);
	if (me_ret.e != OK) {
		panic("trace memextent construct");
	}

	memextent_t *me = me_ret.r;

	hyp_trace.me = object_get_memextent_additional(me);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "trace_me_capid", me_cap);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "trace_phys", trace_phys);
	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "trace_size", trace_size);

	// Create a doorbell object with restricted master rights to share with
	// root VM.
	doorbell_ptr_result_t dbl_ret;
	doorbell_create_t     dbl_params = { 0 };
	cap_rights_doorbell_t dbl_rights = cap_rights_doorbell_default();

	dbl_ret = partition_allocate_doorbell(root_partition, dbl_params);
	if (dbl_ret.e != OK) {
		panic("doorbell create");
	}
	doorbell_t *doorbell = dbl_ret.r;

	err = object_activate_doorbell(doorbell);
	if (err != OK) {
		panic("doorbell activate");
	}

	// Only give the bind right, not a receive right so doorbell hypercalls
	// cannot be used.
	cap_rights_doorbell_set_bind(&dbl_rights, true);

	object_ptr_t obj_ptr = {
		.doorbell = object_get_doorbell_additional(doorbell),
	};

	cap_id_result_t capid_ret = cspace_create_restricted_master_cap(
		root_cspace, obj_ptr, OBJECT_TYPE_DOORBELL,
		cap_rights_doorbell_raw(dbl_rights));
	if (capid_ret.e != OK) {
		panic("doorbell cap");
	}

	hyp_trace.dbl = object_get_doorbell_additional(doorbell);

	QCBOREncode_AddUInt64ToMap(qcbor_enc_ctxt, "trace_dbl_capid",
				   capid_ret.r);
out:
	return;
}
#endif

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
		    !trace_action &&
		    (!log_action ||
		     ((class_flags & TRACE_CLASS_BITS(TRACE_LOG_BUFFER)) ==
		      0U)))) {
		goto out;
	}

	cpu_id	  = cpulocal_get_index_unsafe();
	timestamp = platform_timer_get_current_ticks();

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

	// Atomically grab the next entry in the buffer.
	head = atomic_fetch_add_explicit(&tb->head, 1, memory_order_relaxed);

	// If we wrap, decrement by the number of entries.
	if (compiler_unexpected(head >= entries)) {
		if (head == entries) {
			(void)atomic_fetch_add_explicit(&tb->wrap_count, 1,
							memory_order_relaxed);
		}

		index_t cur_head = head + 1U;
		do {
			(void)atomic_compare_exchange_ll_sc_weak(
				&tb->head, &cur_head, cur_head - entries);
		} while (cur_head >= entries);

		head -= entries;
		// If we reached 2x entries, something is really wrong
		assert(head < entries);
	}

	trace_buffer_entry_t *buffers =
		(trace_buffer_entry_t *)((uintptr_t)tb +
					 (uintptr_t)TRACE_BUFFER_HEADER_SIZE);

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

#if defined(INTERFACE_ROOTVM)
	// If a trace buffer notify listener is enabled, signal the virq
	// whenever the head index notify modulus is zero.
	if (compiler_unexpected(tb->notify_mask != TRACE_NOTIFY_DISABLED)) {
		if (compiler_unexpected((head & tb->notify_mask) == 0U)) {
			assert(hyp_trace.dbl != NULL);
			doorbell_flags_t dbl_flags = 0U;
			// signal the event
			doorbell_flags_result_t dbl_ret =
				doorbell_send(hyp_trace.dbl, dbl_flags);
			if (dbl_ret.e != OK) {
				panic("trace signal");
			}
		}
	}
#endif
out:
	return;
}

void
trace_set_class_flags(register_t flags)
{
	(void)atomic_fetch_or_explicit(&hyp_trace.enabled_class_flags, flags,
				       memory_order_relaxed);
}

void
trace_clear_class_flags(register_t flags)
{
	(void)atomic_fetch_and_explicit(&hyp_trace.enabled_class_flags, ~flags,
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
trace_get_class_flags(void)
{
	return atomic_load_explicit(&hyp_trace.enabled_class_flags,
				    memory_order_relaxed);
}

// Calculate optimal trace notify_mask and set for the trace_buffer
static void
trace_set_notify_mask(trace_buffer_header_t *tb)
{
#define COUNT_BITS util_width(count_t)

	// Ensure we notify at least 3 times per buffer
	count_t entries_notify = tb->entries / 3U;
	assert_debug(entries_notify > 1U);

	index_t msb = (index_t)(COUNT_BITS - 1U) -
		      (index_t)compiler_clz(entries_notify);

	tb->notify_mask = (uint32_t)util_mask(msb);
}

#define TRACE_ENTRIES_MIN 1023U

error_t
trace_update_notify_mask(bool enable)
{
	error_t ret;

	if (enable) {
		// If we don't have a large enough trace, we risk causing
		// interrupt storms.
		if (trace_buffer_global->entries < TRACE_ENTRIES_MIN) {
			ret = ERROR_DENIED;
			goto out;
		}
		for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
			trace_buffer_header_t *cpu_tb =
				CPULOCAL_BY_INDEX(trace_buffer, i);
			if ((cpu_tb != NULL) &&
			    (cpu_tb->entries < TRACE_ENTRIES_MIN)) {
				ret = ERROR_DENIED;
				goto out;
			}
		}

		trace_set_notify_mask(trace_buffer_global);
		for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
			trace_buffer_header_t *cpu_tb =
				CPULOCAL_BY_INDEX(trace_buffer, i);
			if (cpu_tb != NULL) {
				trace_set_notify_mask(cpu_tb);
			}
		}
		ret = OK;
	} else {
		trace_buffer_global->notify_mask = TRACE_NOTIFY_DISABLED;

		for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
			if (platform_cpu_exists(i)) {
				CPULOCAL_BY_INDEX(trace_buffer, i)->notify_mask =
					TRACE_NOTIFY_DISABLED;
			}
		}
		ret = OK;
	}

out:
	return ret;
}

phys_range_t
trace_get_area(void)
{
	return (phys_range_t){
		.base = hyp_trace.header_phys,
		.size = (size_t)hyp_trace.area_size_64 * 64U,
	};
}
