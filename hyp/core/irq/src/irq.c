// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <compiler.h>
#include <ipi.h>
#include <irq.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <platform_irq.h>
#include <rcu.h>
#include <trace.h>

#include <events/irq.h>

#include "event_handlers.h"

// Dynamically allocated array of RCU-protected pointers to hwirq objects.
// No lock is needed to protect writes; they are done with compare-exchange.
//
// Note: in future we might need to split this module and implement a more
// sophisticated data structure for platforms with sparse IRQ numbers.
static hwirq_t *_Atomic *irq_table;
static count_t		 irq_table_size;

void
irq_handle_boot_cold_init(void)
{
	irq_table_size = (count_t)platform_irq_max() + 1U;
	assert(irq_table_size != 0U);

	size_t alloc_size	= irq_table_size * sizeof(_Atomic(hwirq_t *));
	void_ptr_result_t ptr_r = partition_alloc(partition_get_private(),
						  alloc_size,
						  alignof(_Atomic(hwirq_t *)));

	if (ptr_r.e != OK) {
		panic("Unable to allocate IRQ table");
	}

	irq_table = memset(ptr_r.r, 0, alloc_size);
}

error_t
irq_handle_object_create_hwirq(hwirq_create_t params)
{
	hwirq_t *hwirq = params.hwirq;
	assert(hwirq != NULL);

	hwirq->irq    = params.irq;
	hwirq->action = params.action;

	error_t err = platform_irq_check(params.irq);
	if (err == OK) {
		// Insert the pointer in the global table if the current entry
		// in the table is NULL. We do not keep a reference; this is an
		// RCU-protected pointer which is automatically set to NULL on
		// object deletion. The release ordering here matches the
		// consume ordering in lookup.
		hwirq_t *prev = NULL;
		if (!atomic_compare_exchange_strong_explicit(
			    &irq_table[params.irq], &prev, hwirq,
			    memory_order_release, memory_order_relaxed)) {
			// This IRQ is already registered.
			err = ERROR_BUSY;
		}
	}

	if (err == OK) {
		// The IRQ is fully registered; give the handler an opportunity
		// to enable it if desired.
		trigger_irq_registered_event(hwirq->action, hwirq->irq, hwirq);
	}

	return err;
}

irq_t
irq_max(void)
{
	return irq_table_size - 1U;
}

void
irq_enable(hwirq_t *hwirq)
{
	platform_irq_enable(hwirq->irq);
}

void
irq_enable_local(hwirq_t *hwirq)
{
	platform_irq_enable_local(hwirq->irq);
}

void
irq_disable_nosync(hwirq_t *hwirq)
{
	platform_irq_disable(hwirq->irq);
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
irq_disable_sync(hwirq_t *hwirq)
{
	irq_disable_nosync(hwirq);

	// Wait for any in-progress IRQ deliveries on other CPUs to complete.
	//
	// This works regardless of the RCU implementation because IRQ delivery
	// itself is in an RCU critical section, and the irq_disable_nosync()
	// is enough to guarantee that any delivery that hasn't started its
	// critical section yet will not receive the IRQ.
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

	// If this object finished creation, then it must already be in
	// the global table.
	assert(hwirq->irq < irq_table_size);
	assert(atomic_load_relaxed(&irq_table[hwirq->irq]) == hwirq);

	// Disable the physical IRQ if possible.
	if (platform_irq_is_percpu(hwirq->irq)) {
		// To make this take effect immediately across all CPUs we would
		// need to perform an IPI. That is a waste of effort since
		// irq_interrupt_dispatch() will disable IRQs with no handler
		// anyway, so we just disable it locally.
		platform_irq_disable_local(hwirq->irq);
	} else {
		platform_irq_disable(hwirq->irq);
	}

	// Remove this HWIRQ from the dispatch table.
	atomic_store_relaxed(&irq_table[hwirq->irq], NULL);
}

hwirq_t *
irq_lookup_hwirq(irq_t irq)
{
	assert((count_t)irq < irq_table_size);

	return atomic_load_consume(&irq_table[irq]);
}

bool
irq_interrupt_dispatch(void)
{
	bool spurious = true;

	while (true) {
		irq_result_t irq_r = platform_irq_acknowledge();

		if (irq_r.e == ERROR_RETRY) {
			// IRQ handled by the platform, probably an IPI
			spurious = false;
			continue;
		} else if (compiler_unexpected(irq_r.e == ERROR_IDLE)) {
			// No IRQs are pending; exit
			break;
		} else {
			assert(irq_r.e == OK);

			spurious = false;
			TRACE(DEBUG, INFO, "acknowledged HW IRQ {:d}", irq_r.r);

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
				TRACE(DEBUG, WARN,
				      "disabling unhandled HW IRQ {:d}",
				      irq_r.r);
				if (platform_irq_is_percpu(irq_r.r)) {
					platform_irq_disable_local(irq_r.r);
				} else {
					platform_irq_disable(irq_r.r);
				}
				platform_irq_disable(irq_r.r);
				platform_irq_priority_drop(irq_r.r);
				platform_irq_deactivate(irq_r.r);
				rcu_read_finish();
				continue;
			}
			assert(hwirq->irq == irq_r.r);

			bool handled = trigger_irq_received_event(
				hwirq->action, irq_r.r, hwirq);
			platform_irq_priority_drop(irq_r.r);
			if (handled) {
				platform_irq_deactivate(irq_r.r);
			}
			rcu_read_finish();
		}
	}

	if (spurious) {
		TRACE(DEBUG, INFO, "spurious EL2 IRQ");
	}

	return ipi_handle_relaxed();
}
