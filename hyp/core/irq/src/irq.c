// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <bitmap.h>
#include <compiler.h>
#include <ipi.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <partition_alloc.h>
#include <platform_irq.h>
#include <preempt.h>
#include <rcu.h>
#include <trace.h>

#include <events/irq.h>

#include "event_handlers.h"

#if IRQ_SPARSE_IDS
// Dynamically allocated two-level table of RCU-protected pointers to hwirq
// objects. No lock is needed to protect writes; they are done with
// compare-exchange, at both levels. Empty levels are never freed, on the
// assumption that IRQ numbers are set by hardware and therefore are likely to
// be reused.
#define IRQ_TABLE_L2_SIZE PGTABLE_HYP_PAGE_SIZE
#define IRQ_TABLE_L2_ENTRIES                                                   \
	(count_t)((IRQ_TABLE_L2_SIZE / sizeof(hwirq_t *_Atomic)))
static hwirq_t *_Atomic *_Atomic *irq_table_l1;
#else
// Dynamically allocated array of RCU-protected pointers to hwirq objects.
// No lock is needed to protect writes; they are done with compare-exchange.
static hwirq_t *_Atomic *irq_table;
#endif
static count_t irq_max_cache;

#if IRQ_HAS_MSI
static index_t		   irq_msi_bitmap_size;
static _Atomic register_t *irq_msi_bitmap;
#endif

void
irq_handle_boot_cold_init(void)
{
	irq_max_cache = (count_t)platform_irq_max();

#if IRQ_SPARSE_IDS
	count_t irq_table_entries =
		(irq_max_cache + IRQ_TABLE_L2_ENTRIES) / IRQ_TABLE_L2_ENTRIES;
#else
	count_t irq_table_entries = irq_max_cache + 1U;
#endif
	assert(irq_table_entries != 0U);

	size_t alloc_size  = irq_table_entries * sizeof(void *);
	size_t alloc_align = alignof(void *);

	void_ptr_result_t ptr_r = partition_alloc(partition_get_private(),
						  alloc_size, alloc_align);

	if (ptr_r.e != OK) {
		panic("Unable to allocate IRQ table");
	}

#if IRQ_SPARSE_IDS
	irq_table_l1 = ptr_r.r;
	(void)memset_s(irq_table_l1, alloc_size, 0, alloc_size);
#else
	irq_table		  = ptr_r.r;
	(void)memset_s(irq_table, alloc_size, 0, alloc_size);
#endif

#if IRQ_HAS_MSI
	irq_msi_bitmap_size =
		platform_irq_msi_max() - platform_irq_msi_base + 1U;
	count_t msi_bitmap_words = BITMAP_NUM_WORDS(irq_msi_bitmap_size);
	alloc_size		 = msi_bitmap_words * sizeof(register_t);

	ptr_r = partition_alloc(partition_get_private(), alloc_size,
				alignof(register_t));
	if (ptr_r.e != OK) {
		panic("Unable to allocate MSI allocator bitmap");
	}

	irq_msi_bitmap = ptr_r.r;
	(void)memset_s(irq_msi_bitmap, alloc_size, 0, alloc_size);
#endif
}

static hwirq_t *_Atomic *
irq_find_entry(irq_t irq, bool allocate)
{
	assert((count_t)irq <= irq_max_cache);

#if IRQ_SPARSE_IDS
	count_t		  irq_l1_index = irq / IRQ_TABLE_L2_ENTRIES;
	count_t		  irq_l2_index = irq % IRQ_TABLE_L2_ENTRIES;
	hwirq_t *_Atomic *irq_table_l2 =
		atomic_load_consume(&irq_table_l1[irq_l1_index]);

	if ((irq_table_l2 == NULL) && allocate) {
		size_t		  alloc_size  = IRQ_TABLE_L2_SIZE;
		size_t		  alloc_align = alignof(void *);
		void_ptr_result_t ptr_r	      = partition_alloc(
			      partition_get_private(), alloc_size, alloc_align);

		if (ptr_r.e == OK) {
			(void)memset_s(ptr_r.r, alloc_size, 0, alloc_size);

			if (atomic_compare_exchange_strong_explicit(
				    &irq_table_l1[irq_l1_index], &irq_table_l2,
				    (hwirq_t *_Atomic *)ptr_r.r,
				    memory_order_release,
				    memory_order_consume)) {
				irq_table_l2 = (hwirq_t *_Atomic *)ptr_r.r;
			} else {
				assert(irq_table_l2 != NULL);
				(void)partition_free(partition_get_private(),
						     ptr_r.r, alloc_size);
			}
		}
	}

	return (irq_table_l2 == NULL) ? NULL : &irq_table_l2[irq_l2_index];
#else
	(void)allocate;
	return &irq_table[irq];
#endif
}

hwirq_t *
irq_lookup_hwirq(irq_t irq)
{
	hwirq_t *_Atomic *entry = irq_find_entry(irq, false);
	return (entry == NULL) ? NULL : atomic_load_consume(entry);
}

error_t
irq_handle_object_create_hwirq(hwirq_create_t params)
{
	hwirq_t *hwirq = params.hwirq;
	assert(hwirq != NULL);

	hwirq->irq    = params.irq;
	hwirq->action = params.action;

	return OK;
}

error_t
irq_handle_object_activate_hwirq(hwirq_t *hwirq)
{
	error_t err = platform_irq_check(hwirq->irq);
	if (err != OK) {
		goto out;
	}

	// Locate the IRQ's entry in the global IRQ table, allocating table
	// levels if necessary.
	hwirq_t *_Atomic *entry = irq_find_entry(hwirq->irq, true);
	if (entry == NULL) {
		err = ERROR_NOMEM;
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
	return err;
}

irq_t
irq_max(void)
{
	return irq_max_cache;
}

void
irq_enable_shared(hwirq_t *hwirq)
{
	platform_irq_enable_shared(hwirq->irq);
}

void
irq_enable_local(hwirq_t *hwirq)
{
	platform_irq_enable_local(hwirq->irq);
}

void
irq_disable_shared_nosync(hwirq_t *hwirq)
{
	platform_irq_disable_shared(hwirq->irq);
}

void
irq_disable_local(hwirq_t *hwirq)
{
	platform_irq_disable_local(hwirq->irq);
}

void
irq_disable_local_nowait(hwirq_t *hwirq)
{
	platform_irq_disable_local_nowait(hwirq->irq);
}

void
irq_disable_shared_sync(hwirq_t *hwirq)
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
irq_deactivate(hwirq_t *hwirq)
{
	platform_irq_deactivate(hwirq->irq);
}

void
irq_handle_object_deactivate_hwirq(hwirq_t *hwirq)
{
	assert(hwirq != NULL);
	assert(hwirq->irq <= irq_max_cache);

	// This object was activated successfully, so it must already be in the
	// global table.
	hwirq_t *_Atomic *entry = irq_find_entry(hwirq->irq, false);
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
}

static void
disable_unhandled_irq(irq_result_t irq_r) REQUIRE_PREEMPT_DISABLED
{
	TRACE(ERROR, WARN, "disabling unhandled HW IRQ {:d}", irq_r.r);
	if (platform_irq_is_percpu(irq_r.r)) {
		platform_irq_disable_local(irq_r.r);
	} else {
		platform_irq_disable_shared(irq_r.r);
	}
	platform_irq_priority_drop(irq_r.r);
	platform_irq_deactivate(irq_r.r);
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
		TRACE(INFO, INFO, "acknowledged HW IRQ {:d}", irq_r.r);

		// The entire IRQ delivery is an RCU critical section.
		//
		// Note that this naturally true anyway if we don't
		// allow interrupt nesting.
		//
		// Also, the alternative is to take a reference to the
		// hwirq, which might force us to tear down the hwirq
		// (and potentially the whole partition) in the
		// interrupt handler.
		rcu_read_start();
		hwirq_t *hwirq = irq_lookup_hwirq(irq_r.r);

		if (compiler_unexpected(hwirq == NULL)) {
			disable_unhandled_irq(irq_r);
			rcu_read_finish();
			goto out;
		}

		assert(hwirq->irq == irq_r.r);

		bool handled = trigger_irq_received_event(hwirq->action,
							  irq_r.r, hwirq);
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
irq_interrupt_dispatch(void)
{
	bool spurious = true;

	while (irq_interrupt_dispatch_one()) {
		spurious = false;
	}

	if (spurious) {
		TRACE(INFO, INFO, "spurious EL2 IRQ");
	}

	return ipi_handle_relaxed();
}

#if IRQ_HAS_MSI

hwirq_ptr_result_t
irq_allocate_msi(partition_t *partition, hwirq_action_t action)
{
	hwirq_ptr_result_t ret;
	index_t		   msi;

	assert(irq_msi_bitmap != NULL);

	do {
		if (!bitmap_atomic_ffc(irq_msi_bitmap, irq_msi_bitmap_size,
				       &msi)) {
			ret = hwirq_ptr_result_error(ERROR_BUSY);
			goto out;
		}
	} while (bitmap_atomic_test_and_set(irq_msi_bitmap, msi,
					    memory_order_relaxed));

	irq_t	       irq	    = msi + platform_irq_msi_base;
	hwirq_create_t hwirq_params = { .action = action, .irq = irq };
	ret = partition_allocate_hwirq(partition, hwirq_params);
	if (ret.e != OK) {
		bitmap_atomic_clear(irq_msi_bitmap, msi, memory_order_relaxed);
		goto out;
	}

	ret.e = object_activate_hwirq(ret.r);
	if (ret.e != OK) {
		// IRQ number will be freed by cleanup handler
		object_put_hwirq(ret.r);
		goto out;
	}

out:
	return ret;
}

void
irq_handle_object_cleanup_hwirq(hwirq_t *hwirq)
{
	if (hwirq->irq >= platform_irq_msi_base) {
		index_t msi = hwirq->irq - platform_irq_msi_base;
		if (msi < irq_msi_bitmap_size) {
			assert(irq_msi_bitmap != NULL);

			// Free the IRQ number from the MSI allocator
			bitmap_atomic_clear(irq_msi_bitmap, msi,
					    memory_order_release);
		}
	}
}
#endif // IRQ_HAS_MSI
