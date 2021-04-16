// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if VIC_BASE_FORWARD_PRIVATE

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <cpulocal.h>
#include <irq.h>
#include <log.h>
#include <partition.h>
#include <platform_irq.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <vic.h>
#include <virq.h>

#include <events/virq.h>

#include "event_handlers.h"
#include "gicv3.h"
#include "vic_base.h"

#define fwd_private_from_virq_source(p)                                        \
	(assert(p != NULL),                                                    \
	 assert(p->trigger == VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE),          \
	 vic_forward_private_container_of_source(p))

static vic_t *
vic_unbind_hwirq_helper(hwirq_t *hwirq)
{
	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	vic_t *vic = NULL;

	// Remove the VIRQ binding.
	for (cpu_index_t i = 0; i < PLATFORM_MAX_CORES; ++i) {
		vic_forward_private_t *fp = &hwirq->vic_base_forward_private[i];

		vic_t *cur_vic = atomic_load_relaxed(&fp->source.vic);
		if (cur_vic != NULL) {
			vic = cur_vic;
		} else {
			continue;
		}

		vic_unbind(&fp->source);
	}

	rcu_sync();

	return vic;
}

error_t
vic_bind_hwirq_forward_private(vic_t *vic, hwirq_t *hwirq, virq_t virq)
{
	error_t err = OK;

	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	// allocate for private forward
	struct partition *partition = vic->header.partition;
	assert(partition != NULL);

	size_t size = sizeof(hwirq->vic_base_forward_private[0]) * GIC_PPI_NUM;
	void_ptr_result_t alloc_r = partition_alloc(
		partition, size, alignof(hwirq->vic_base_forward_private[0]));
	if (alloc_r.e != OK) {
		err = ERROR_NOMEM;
		goto out;
	}

	memset(alloc_r.r, 0, size);
	hwirq->vic_base_forward_private = (vic_forward_private_t *)alloc_r.r;

	count_t total_vcpu_cnt = vic->gicr_count;
	while (total_vcpu_cnt > 0) {
		index_t idx = total_vcpu_cnt - 1;
		rcu_read_start();

		thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[idx]);
		if (vcpu == NULL) {
			rcu_read_finish();
			err = ERROR_OBJECT_CONFIG;
			goto err_vcpu;
		}

		cpu_index_t pcpu = vcpu->scheduler_affinity;
		assert(pcpu < PLATFORM_MAX_CORES);

		vic_forward_private_t *forward_private =
			&hwirq->vic_base_forward_private[pcpu];

		err = vic_bind_private_forward_private(
			forward_private, vic, vcpu, virq, hwirq->irq, pcpu);
		if (err != OK) {
			rcu_read_finish();
			goto err_bind;
		}

		forward_private->cpu = pcpu;
		atomic_store_relaxed(&forward_private->hw_active, false);
		forward_private->pirq = hwirq->irq;

		rcu_read_finish();

		total_vcpu_cnt--;
	}

err_bind:
err_vcpu:
	if (err != OK) {
		(void)vic_unbind_hwirq_helper(hwirq);
		partition_free(partition, hwirq->vic_base_forward_private,
			       size);
	}
out:
	return err;
}

error_t
vic_unbind_hwirq_forward_private(hwirq_t *hwirq)
{
	error_t ret = OK;

	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	vic_t *vic = vic_unbind_hwirq_helper(hwirq);
	if (vic == NULL) {
		// if non of the vcpu is bound, something wrong
		ret = OK;
		goto out;
	}

	// free for private forward
	struct partition *partition = vic->header.partition;
	if (partition == NULL) {
		ret = ERROR_DENIED;
		goto out;
	}

	size_t size = sizeof(hwirq->vic_base_forward_private[0]) * GIC_PPI_NUM;
	partition_free(partition, hwirq->vic_base_forward_private, size);
out:
	return ret;
}

bool
vic_handle_irq_received_forward_private(hwirq_t *hwirq)
{
	assert(hwirq != NULL);
	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);
	assert(hwirq->vic_base_forward_private != NULL);

	bool_result_t is_edge_r;
	bool	      deactivate = false;
	_Atomic bool *hw_active	 = NULL;

	cpu_index_t source_pcpu = cpulocal_get_index();

	rcu_read_start();

	vic_forward_private_t *fwd_private_percpu =
		hwirq->vic_base_forward_private;
	hw_active = &fwd_private_percpu[source_pcpu].hw_active;
	atomic_store_release(hw_active, true);

	virq_source_t *source = &fwd_private_percpu[source_pcpu].source;
	is_edge_r	      = virq_assert(source, false);

	if (compiler_unexpected(is_edge_r.e != OK)) {
		// We were unable to deliver the IRQ (because we lost a
		// race with unbind), so disable it.
		irq_disable_local(hwirq);
		deactivate = true;
	} else if (is_edge_r.r) {
		// The IRQ was delivered successfully in edge-triggered
		// mode; we must deactivate it on return (if a VIRQ
		// handler has not already done so), because we have no
		// guarantee that the check-pending handler will be
		// called after deactivate.
		//
		// We are relying here on the physical interrupt also
		// being edge-triggered! If it is level-triggered there
		// will be an interrupt storm. The vic_bind_hwirq and
		// virq_set_mode handlers must ensure that the mode
		// remains consistent between the VIRQ and hardware.
		assert(hw_active != NULL);
		deactivate = atomic_fetch_and_explicit(hw_active, false,
						       memory_order_relaxed);
	} else {
		// The IRQ was delivered successfully in level-triggered
		// mode; it will be deactivated in the check-pending
		// handler.
		deactivate = false;
	}
	rcu_read_finish();

	return deactivate;
}

bool
vic_handle_virq_check_pending_forward_private(virq_source_t *source,
					      bool	     reasserted)
{
	vic_forward_private_t *fwd_private_percpu =
		fwd_private_from_virq_source(source);

	if (!reasserted &&
	    atomic_fetch_and_explicit(&fwd_private_percpu->hw_active, false,
				      memory_order_relaxed)) {
		if (compiler_expected(cpulocal_get_index() ==
				      fwd_private_percpu->cpu)) {
			platform_irq_deactivate(fwd_private_percpu->pirq);
		} else {
			platform_irq_deactivate_percpu(fwd_private_percpu->pirq,
						       fwd_private_percpu->cpu);
		}
	}

	return reasserted;
}

bool
vic_handle_virq_set_enabled_forward_private(virq_source_t *source, bool enabled)
{
	vic_forward_private_t *fwd_private_percpu =
		fwd_private_from_virq_source(source);
	assert(fwd_private_percpu->source.is_private);
	assert(platform_irq_is_percpu(fwd_private_percpu->pirq));

	if (enabled) {
		platform_irq_enable_percpu(fwd_private_percpu->pirq,
					   fwd_private_percpu->cpu);
	} else {
		platform_irq_disable_percpu(fwd_private_percpu->pirq,
					    fwd_private_percpu->cpu);
	}

	return true;
}

irq_trigger_result_t
vic_handle_virq_set_mode_forward_private(virq_source_t *source,
					 irq_trigger_t	mode)
{
	vic_forward_private_t *fwd_private_percpu =
		fwd_private_from_virq_source(source);

	assert(source->is_private);
	assert(platform_irq_is_percpu(fwd_private_percpu->pirq));

	return gicv3_irq_set_trigger_percpu(fwd_private_percpu->pirq, mode,
					    fwd_private_percpu->cpu);
}

#endif
