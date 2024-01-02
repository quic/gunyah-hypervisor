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
#include <list.h>
#include <log.h>
#include <object.h>
#include <partition.h>
#include <platform_irq.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vic.h>
#include <virq.h>

#include <events/virq.h>

#include "event_handlers.h"
#include "panic.h"
#include "vic_base.h"

static vic_private_irq_info_t *
private_irq_info_from_virq_source(virq_source_t *source)
{
	assert(source != NULL);
	assert(source->trigger == VIRQ_TRIGGER_VIC_BASE_FORWARD_PRIVATE);

	return vic_private_irq_info_container_of_source(source);
}

// Called with the forward-private lock held.
static error_t
vic_bind_private_hwirq_helper(vic_forward_private_t *fp, thread_t *vcpu)
{
	error_t	    err;
	cpu_index_t cpu;

	assert(vcpu->vic_base_forward_private_active);

	if (!vcpu_option_flags_get_pinned(&vcpu->vcpu_options)) {
		err = ERROR_DENIED;
		goto out;
	}

	scheduler_lock(vcpu);
	cpu = vcpu->scheduler_affinity;
	scheduler_unlock(vcpu);

	assert(cpulocal_index_valid(cpu));

	vic_private_irq_info_t *irq_info = &fp->irq_info[cpu];

	err = vic_bind_private_forward_private(&irq_info->source, fp->vic, vcpu,
					       fp->virq);

	if ((err == OK) && vcpu->vic_base_forward_private_in_sync) {
		vic_sync_private_forward_private(&irq_info->source, fp->vic,
						 vcpu, fp->virq, irq_info->irq,
						 cpu);
	}

out:
	return err;
}

// Called with the forward-private lock held.
static void
vic_unbind_private_hwirq_helper(hwirq_t *hwirq)
{
	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	vic_forward_private_t *fp = atomic_exchange_explicit(
		&hwirq->vic_base_forward_private, NULL, memory_order_consume);
	if (fp != NULL) {
		vic_t *vic = fp->vic;
		assert(vic != NULL);

		spinlock_acquire(&vic->forward_private_lock);

		for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
			vic_unbind(&fp->irq_info[i].source);
		}

		(void)list_delete_node(&vic->forward_private_list,
				       &fp->list_node);

		spinlock_release(&vic->forward_private_lock);

		rcu_enqueue(&fp->rcu_entry,
			    RCU_UPDATE_CLASS_VIC_BASE_FREE_FORWARD_PRIVATE);
	}
}

// Called with the forward-private lock held.
static void
vic_sync_private_hwirq_helper(vic_forward_private_t *fp, thread_t *vcpu)
{
	assert(vcpu->vic_base_forward_private_active);
	assert(vcpu_option_flags_get_pinned(&vcpu->vcpu_options));

	scheduler_lock(vcpu);
	cpu_index_t cpu = vcpu->scheduler_affinity;
	scheduler_unlock(vcpu);

	assert(cpulocal_index_valid(cpu));

	vic_private_irq_info_t *irq_info = &fp->irq_info[cpu];

	vic_sync_private_forward_private(&irq_info->source, fp->vic, vcpu,
					 fp->virq, irq_info->irq, cpu);
}

// Called with the forward-private lock held.
static void
vic_disable_private_hwirq_helper(vic_forward_private_t *fp, thread_t *vcpu)
{
	assert(vcpu->vic_base_forward_private_active);
	assert(vcpu_option_flags_get_pinned(&vcpu->vcpu_options));

	scheduler_lock(vcpu);
	cpu_index_t cpu = vcpu->scheduler_affinity;
	scheduler_unlock(vcpu);

	assert(cpulocal_index_valid(cpu));

	vic_private_irq_info_t *irq_info = &fp->irq_info[cpu];

	platform_irq_disable_percpu(irq_info->irq, cpu);
}

error_t
vic_bind_hwirq_forward_private(vic_t *vic, hwirq_t *hwirq, virq_t virq)
{
	error_t err = OK;

	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	partition_t *partition = vic->header.partition;
	assert(partition != NULL);

	size_t		  size	  = sizeof(vic_forward_private_t);
	void_ptr_result_t alloc_r = partition_alloc(
		partition, size, alignof(vic_forward_private_t));
	if (alloc_r.e != OK) {
		err = ERROR_NOMEM;
		goto out;
	}

	vic_forward_private_t *fp = (vic_forward_private_t *)alloc_r.r;
	(void)memset_s(fp, sizeof(*fp), 0, sizeof(*fp));

	fp->vic	 = object_get_vic_additional(vic);
	fp->virq = virq;

	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		fp->irq_info[i].cpu = i;
		fp->irq_info[i].irq = hwirq->irq;
	}

	// We must acquire this lock before setting the fp pointer in the
	// hwirq object. This prevents a race with a concurrent unbind on the
	// same hwirq, which might otherwise be able to clear the fp pointer and
	// run its vic_unbind() calls too early, before the bind calls below,
	// leading to the fp structure being freed while the sources in it are
	// still bound.
	spinlock_acquire(&vic->forward_private_lock);

	vic_forward_private_t *expected = NULL;
	if (!atomic_compare_exchange_strong_explicit(
		    &hwirq->vic_base_forward_private, &expected, fp,
		    memory_order_release, memory_order_relaxed)) {
		spinlock_release(&vic->forward_private_lock);
		(void)partition_free(partition, fp, size);
		err = ERROR_DENIED;
		goto out;
	}

	list_insert_at_tail(&vic->forward_private_list, &fp->list_node);

	// Bind for VCPUs that are attached to the VIC and active.
	for (cpu_index_t i = 0U; i < PLATFORM_MAX_CORES; i++) {
		rcu_read_start();

		thread_t *vcpu = atomic_load_consume(&vic->gicr_vcpus[i]);
		if ((vcpu != NULL) && vcpu->vic_base_forward_private_active) {
			err = vic_bind_private_hwirq_helper(fp, vcpu);
			if (err != OK) {
				rcu_read_finish();
				break;
			}
		}

		rcu_read_finish();
	}

	spinlock_release(&vic->forward_private_lock);

	if (err != OK) {
		vic_unbind_private_hwirq_helper(hwirq);
	}

out:
	return err;
}

error_t
vic_unbind_hwirq_forward_private(hwirq_t *hwirq)
{
	vic_unbind_private_hwirq_helper(hwirq);

	return OK;
}

bool
vic_handle_vcpu_activate_thread_forward_private(thread_t *thread)
{
	bool   ret = true;
	vic_t *vic = vic_get_vic(thread);

	if (vic != NULL) {
		spinlock_acquire(&vic->forward_private_lock);

		thread->vic_base_forward_private_active	 = true;
		thread->vic_base_forward_private_in_sync = false;

		vic_forward_private_t *fp;

		list_foreach_container (fp, &vic->forward_private_list,
					vic_forward_private, list_node) {
			if (vic_bind_private_hwirq_helper(fp, thread) != OK) {
				ret = false;
				break;
			}
		}

		spinlock_release(&vic->forward_private_lock);
	}

	return ret;
}

error_t
vic_handle_object_create_vic_forward_private(vic_create_t vic_create)
{
	vic_t *vic = vic_create.vic;

	spinlock_init(&vic->forward_private_lock);
	list_init(&vic->forward_private_list);

	return OK;
}

void
vic_handle_object_deactivate_hwirq_forward_private(hwirq_t *hwirq)
{
	vic_unbind_private_hwirq_helper(hwirq);
}

bool
vic_handle_irq_received_forward_private(hwirq_t *hwirq)
{
	assert(hwirq != NULL);
	assert(hwirq->action == HWIRQ_ACTION_VIC_BASE_FORWARD_PRIVATE);

	bool_result_t is_edge_r;
	bool	      deactivate;
	_Atomic bool *hw_active;
	cpu_index_t   cpu = cpulocal_get_index();

	rcu_read_start();

	vic_forward_private_t *fp =
		atomic_load_consume(&hwirq->vic_base_forward_private);
	if (fp == NULL) {
		irq_disable_local(hwirq);
		deactivate = true;
		goto out;
	}

	vic_private_irq_info_t *irq_info = &fp->irq_info[cpu];

	hw_active = &irq_info->hw_active;
	atomic_store_relaxed(hw_active, true);

	virq_source_t *source = &irq_info->source;
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

out:
	rcu_read_finish();

	return deactivate;
}

bool
vic_handle_virq_check_pending_forward_private(virq_source_t *source,
					      bool	     reasserted)
{
	vic_private_irq_info_t *irq_info =
		private_irq_info_from_virq_source(source);

	// FIXME:
	if (!reasserted &&
	    atomic_fetch_and_explicit(&irq_info->hw_active, false,
				      memory_order_relaxed)) {
		if (compiler_expected(cpulocal_get_index() == irq_info->cpu)) {
			platform_irq_deactivate(irq_info->irq);
		} else {
			platform_irq_deactivate_percpu(irq_info->irq,
						       irq_info->cpu);
		}
	}

	return reasserted;
}

bool
vic_handle_virq_set_enabled_forward_private(virq_source_t *source, bool enabled)
{
	vic_private_irq_info_t *irq_info =
		private_irq_info_from_virq_source(source);

	assert(source->is_private);
	assert(platform_irq_is_percpu(irq_info->irq));

	// Note that we don't check the forward-private flag here, because we
	// can't safely take the lock; the vgic module calls this handler with
	// the GICD lock held, and the sync handler above calls a vgic function
	// that acquires the GICD lock with the forward-private lock held.
	// The same applies to the other VIRQ configuration handlers.
	// FIXME:
	if (enabled) {
		platform_irq_enable_percpu(irq_info->irq, irq_info->cpu);
	} else {
		platform_irq_disable_percpu(irq_info->irq, irq_info->cpu);
	}

	return true;
}

irq_trigger_result_t
vic_handle_virq_set_mode_forward_private(virq_source_t *source,
					 irq_trigger_t	mode)
{
	vic_private_irq_info_t *irq_info =
		private_irq_info_from_virq_source(source);

	assert(source->is_private);
	assert(platform_irq_is_percpu(irq_info->irq));

	// FIXME:
	return platform_irq_set_mode_percpu(irq_info->irq, mode, irq_info->cpu);
}

rcu_update_status_t
vic_handle_free_forward_private(rcu_entry_t *entry)
{
	rcu_update_status_t ret = rcu_update_status_default();

	assert(entry != NULL);

	vic_forward_private_t *fp =
		vic_forward_private_container_of_rcu_entry(entry);
	vic_t *vic = fp->vic;

	partition_t *partition = vic->header.partition;
	assert(partition != NULL);
	(void)partition_free(partition, fp, sizeof(vic_forward_private_t));

	object_put_vic(vic);

	return ret;
}

void
vic_base_handle_vcpu_started(bool warm_reset)
{
	thread_t *vcpu = thread_get_self();
	vic_t	 *vic  = vic_get_vic(vcpu);

	if (warm_reset || (vic == NULL) ||
	    !vcpu_option_flags_get_pinned(&vcpu->vcpu_options)) {
		// Nothing to do
		goto out;
	}

	spinlock_acquire(&vic->forward_private_lock);

	assert(!vcpu->vic_base_forward_private_in_sync);

	vic_forward_private_t *fp;
	list_foreach_container (fp, &vic->forward_private_list,
				vic_forward_private, list_node) {
		vic_sync_private_hwirq_helper(fp, vcpu);
	}
	vcpu->vic_base_forward_private_in_sync = true;

	spinlock_release(&vic->forward_private_lock);

out:
	return;
}

void
vic_base_handle_vcpu_stopped(void)
{
	thread_t *vcpu = thread_get_self();
	vic_t	 *vic  = vic_get_vic(vcpu);

	if ((vic == NULL) ||
	    !vcpu_option_flags_get_pinned(&vcpu->vcpu_options)) {
		// Nothing to do
		goto out;
	}

	spinlock_acquire(&vic->forward_private_lock);
	if (vcpu->vic_base_forward_private_in_sync) {
		vic_forward_private_t *fp;
		list_foreach_container (fp, &vic->forward_private_list,
					vic_forward_private, list_node) {
			vic_disable_private_hwirq_helper(fp, vcpu);
		}
		vcpu->vic_base_forward_private_in_sync = false;
	}
	spinlock_release(&vic->forward_private_lock);

out:
	return;
}
#endif
