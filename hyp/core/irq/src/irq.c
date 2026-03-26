// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <globals.h>
#include <ipi.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_irq.h>
#include <preempt.h>
#include <range_tree.h>
#include <rcu.h>
#include <trace.h>
#include <util.h>

#include <events/irq.h>

#include "event_handlers.h"

static range_tree_t irq_range_tree;

void
irq_handle_boot_cold_init(cpu_index_t boot_cpu_index)
{
	error_t err = range_tree_init(&irq_range_tree, partition_get_private());
	if (err != OK) {
		panic("Unable to initialise IRQ range tree");
	}

	trigger_irq_init_event(boot_cpu_index);
}

error_t
irq_range_add(irq_range_t *range, irq_range_type_t range_type, irq_t base,
	      count_t size)
{
	error_t ret;

	if (range->range_type != IRQ_RANGE_TYPE_INVALID) {
		ret = ERROR_BUSY;
		goto out;
	}

	if (range_type == IRQ_RANGE_TYPE_INVALID) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	range->range_type = range_type;
	range_tree_lock(&irq_range_tree);
	ret = range_tree_insert(&irq_range_tree, &range->node, (size_t)base,
				(size_t)size);
	range_tree_unlock(&irq_range_tree);
	if (ret != OK) {
		range->range_type = IRQ_RANGE_TYPE_INVALID;
	}

out:
	return ret;
}

error_t
irq_range_remove(irq_range_t *range)
{
	range_tree_lock(&irq_range_tree);
	error_t ret = range_tree_remove(&irq_range_tree, &range->node);
	range_tree_unlock(&irq_range_tree);
	return ret;
}

irq_range_t *
irq_lookup_range(irq_t irq) REQUIRE_RCU_READ
{
	range_tree_lookup_result_t lookup_r =
		range_tree_lookup(&irq_range_tree, irq, 1U);
	return (lookup_r.node != NULL)
		       ? irq_range_container_of_node(lookup_r.node)
		       : NULL;
}

error_t
irq_range_add_hwirq(irq_t base, count_t size)
{
	error_t ret;

	if ((size_t)size > (SIZE_MAX / sizeof(hwirq_t *))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	if (util_add_overflows(base, size - 1U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	size_t		  alloc_size = sizeof(irq_range_hwirq_t);
	void_ptr_result_t ptr_r = partition_alloc(partition_get_private(),
						  alloc_size,
						  alignof(irq_range_hwirq_t));
	if (ptr_r.e != OK) {
		ret = ptr_r.e;
		goto out;
	}
	(void)memset_s(ptr_r.r, alloc_size, 0, alloc_size);
	irq_range_hwirq_t *range = (irq_range_hwirq_t *)ptr_r.r;

	alloc_size = size * sizeof(range->hwirqs[0]);
	ptr_r	   = partition_alloc(partition_get_private(), alloc_size,
				     alignof(range->hwirqs[0]));
	if (ptr_r.e != OK) {
		partition_free(partition_get_private(), range,
			       sizeof(irq_range_hwirq_t));
		ret = ptr_r.e;
		goto out;
	}
	(void)memset_s(ptr_r.r, alloc_size, 0, alloc_size);
	range->hwirqs = (hwirq_t *_Atomic *)ptr_r.r;

	ret = irq_range_add(&range->range, IRQ_RANGE_TYPE_HWIRQ, base, size);
	if (ret != OK) {
		partition_free(partition_get_private(), range->hwirqs,
			       size * sizeof(range->hwirqs[0]));
		partition_free(partition_get_private(), range,
			       sizeof(irq_range_hwirq_t));
	}

out:
	return ret;
}

static hwirq_t *_Atomic *
irq_find_entry_hwirq(irq_t irq) REQUIRE_RCU_READ
{
	hwirq_t *_Atomic *entry;

	irq_range_t *range = irq_lookup_range(irq);
	if ((range != NULL) && (range->range_type == IRQ_RANGE_TYPE_HWIRQ)) {
		irq_range_hwirq_t *hwirq_range =
			irq_range_hwirq_container_of_range(range);
		size_t offset = (size_t)irq - range->node.base;
		assert(offset < range->node.size);
		entry = &hwirq_range->hwirqs[offset];
	} else {
		entry = NULL;
	}

	return entry;
}

hwirq_t *
irq_lookup_hwirq(irq_t irq)
{
	hwirq_t *_Atomic *entry = irq_find_entry_hwirq(irq);
	return (entry == NULL) ? NULL : atomic_load_consume(entry);
}

irq_t
irq_get_irq(const hwirq_t *hwirq)
{
	assert_debug(hwirq != NULL);
	return hwirq->irq;
}

uint64_t
irq_get_opaque(const hwirq_t *hwirq)
{
	assert_debug(hwirq != NULL);
	return hwirq->opaque;
}

error_t
irq_handle_object_create_hwirq(hwirq_create_t hwirq_create)
{
	hwirq_t *hwirq = hwirq_create.hwirq;
	assert_debug(hwirq != NULL);

	hwirq->irq    = hwirq_create.irq;
	hwirq->action = hwirq_create.action;
	hwirq->opaque = hwirq_create.opaque;

	return OK;
}

error_t
irq_handle_object_activate_hwirq(hwirq_t *hwirq)
{
	// The entry pointer is strictly RCU-pratected, though we never
	// delete hwirq tables so it doesn't really need to be.
	rcu_read_start();

	error_t err = platform_irq_check(hwirq->irq);
	if (err != OK) {
		goto out;
	}

	// Locate the IRQ's entry in a registered hwirq range.
	hwirq_t *_Atomic *entry = irq_find_entry_hwirq(hwirq->irq);
	if (entry == NULL) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	// Insert the pointer in the global table if the current entry in the
	// table is NULL. We do not keep a reference; this is an RCU-protected
	// pointer which is automatically set to NULL on object deletion. The
	// release ordering here matches the consume ordering in
	// lookup.
	hwirq_t *prev = NULL;
	if (!atomic_compare_exchange_strong_explicit(entry, &prev, hwirq,
						     memory_order_release,
						     memory_order_relaxed)) {
		// This IRQ is already registered.
		err = ERROR_BUSY;
		goto out;
	}

	// The IRQ is fully registered; give the handler an opportunity to
	// enable it if desired.
	(void)trigger_irq_registered_event(hwirq->action, hwirq->irq, hwirq);

out:
	rcu_read_finish();
	return err;
}

void
irq_enable_shared(const hwirq_t *hwirq)
{
	platform_irq_enable_shared(hwirq->irq);
}

void
irq_enable_local(const hwirq_t *hwirq)
{
	platform_irq_enable_local(hwirq->irq);
}

void
irq_disable_shared_nosync(const hwirq_t *hwirq)
{
	platform_irq_disable_shared(hwirq->irq);
}

void
irq_disable_local(const hwirq_t *hwirq)
{
	platform_irq_disable_local(hwirq->irq);
}

void
irq_disable_local_nowait(const hwirq_t *hwirq)
{
	platform_irq_disable_local_nowait(hwirq->irq);
}

void
irq_disable_shared_sync(const hwirq_t *hwirq)
{
	irq_disable_shared_nosync(hwirq);

	// Wait for any in-progress IRQ deliveries on other CPUs to complete.
	//
	// This works regardless of the RCU implementation because IRQ delivery
	// itself is in an RCU critical section, and the
	// irq_disable_shared_nosync() is enough to guarantee that any delivery
	// that hasn't started its critical section yet will not receive the
	// IRQ.
	rcu_sync();
}

void
irq_deactivate_forwarded(const hwirq_t *hwirq)
{
	platform_irq_deactivate_forwarded(hwirq->irq);
}

void
irq_deactivate(const hwirq_t *hwirq)
{
	platform_irq_deactivate(hwirq->irq);
}

void
irq_handle_object_deactivate_hwirq(hwirq_t *hwirq)
{
	assert(hwirq != NULL);

	// The entry pointer is strictly RCU-pratected, though we never
	// delete hwirq tables so it doesn't really need to be.
	rcu_read_start();

	// This object was activated successfully, so it must already be in the
	// global table.
	hwirq_t *_Atomic *entry = irq_find_entry_hwirq(hwirq->irq);
	assert(entry != NULL);
	assert(atomic_load_relaxed(entry) == hwirq);

	// Disable the physical IRQ if possible.
	if (platform_irq_is_percpu(hwirq->irq)) {
		// To make this take effect immediately across all CPUs we would
		// need to perform an IPI. That is a waste of effort since
		// irq_interrupt_dispatch() will disable IRQs with no handler
		// anyway, so we just disable it locally.
		preempt_disable();
		platform_irq_disable_local(hwirq->irq);
		preempt_enable();
	} else {
		platform_irq_disable_shared(hwirq->irq);
	}

	// Remove this HWIRQ from the dispatch table.
	atomic_store_relaxed(entry, NULL);

	rcu_read_finish();
}

static void
disable_unhandled_irq(irq_t irq) REQUIRE_PREEMPT_DISABLED
{
	TRACE(ERROR, WARN, "disabling out-of-range HW IRQ {:d}", irq);
	if (platform_irq_is_percpu(irq)) {
		platform_irq_disable_local(irq);
	} else {
		platform_irq_disable_shared(irq);
	}
}

static bool
irq_interrupt_dispatch_one(void) REQUIRE_PREEMPT_DISABLED
{
	irq_result_t irq_r = platform_irq_acknowledge();
	bool	     ret   = true;

	if (irq_r.e == ERROR_RETRY) {
		// IRQ handled by the platform, probably an IPI
		goto out;
	} else if (compiler_unexpected(irq_r.e == ERROR_IDLE)) {
		// No IRQs are pending; exit
		ret = false;
		goto out;
	} else {
		assert(irq_r.e == OK);
		TRACE_PROFILE(2, TRACE_CLASS_BITS(INFO), INFO,
			      "acknowledged HW IRQ {:d}", irq_r.r);

		// The IRQ range is RCU-protected.
		rcu_read_start();

		irq_range_t *range = irq_lookup_range(irq_r.r);

		if (compiler_unexpected(range == NULL)) {
			disable_unhandled_irq(irq_r.r);
			platform_irq_priority_drop(irq_r.r);
			platform_irq_deactivate(irq_r.r);
			rcu_read_finish();
			goto out;
		}

		size_t offset = (size_t)irq_r.r - range->node.base;
		assert(offset < range->node.size);

		bool handled = trigger_irq_dispatch_event(
			range->range_type, irq_r.r, range, offset);
		platform_irq_priority_drop(irq_r.r);
		if (handled) {
			platform_irq_deactivate(irq_r.r);
		}

		rcu_read_finish();
	}

out:
	return ret;
}

bool
irq_handle_irq_dispatch(irq_t irq, irq_range_t *range, size_t offset)
{
	irq_range_hwirq_t *hwirq_range =
		irq_range_hwirq_container_of_range(range);
	hwirq_t *hwirq = atomic_load_consume(&hwirq_range->hwirqs[offset]);
	bool	 handled;

	if (compiler_unexpected(hwirq == NULL)) {
		disable_unhandled_irq(irq);
		handled = true;
	} else {
		handled = trigger_irq_received_event(hwirq->action, irq, hwirq);
	}

	return handled;
}

bool
irq_interrupt_dispatch(void)
{
	bool spurious = true;

	while (irq_interrupt_dispatch_one()) {
		spurious = false;

#if defined(CPU_ERRATUM_1297) && CPU_ERRATUM_1297
		const global_options_t *global_options = globals_get_options();
		if (compiler_unexpected(global_options_get_cpu_erratum_1297(
			    global_options))) {
			// Don't try to handle multiple IRQs in one ISR entry.
			// This significantly reduces the chances of hitting the
			// erratum.
			break;
		}
#endif
	}

	if (spurious) {
		TRACE(INFO, INFO, "{:s}: no IRQs pending", (uintptr_t)__func__);
	}

	return ipi_handle_relaxed();
}
