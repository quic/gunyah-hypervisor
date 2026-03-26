// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <vdevice.h>
#include <vic.h>
#include <virq.h>
#include <virtio.h>

#include <events/virtio_backend.h>
#include <events/virtio_mmio.h>

#include "event_handlers.h"
#include "virtio_mmio.h"

error_t
virtio_mmio_handle_virtio_startup(virtio_t *virtio)
{
	error_t ret;
	assert(virtio != NULL);

	// Configuration cache extent must be present and mapped.
	if ((virtio->config_cache_me == NULL) ||
	    (virtio->config_range.base == 0U)) {
		TRACE(ERROR, INFO, "virtio_startup mmio: no config cache");
		ret = ERROR_OBJECT_CONFIG;
		goto out;
	}

	// Device configuration offset must be at least 0x100, so we can
	// place the read-only mirror of the common registers before it.
	if (virtio->config_offset <
	    offsetof(virtio_mmio_regs_t, device_config)) {
		TRACE(ERROR, INFO,
		      "virtio_startup mmio: bad config offset {:#x}",
		      virtio->config_offset);
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}
	if (virtio->config_size <
	    (sizeof(virtio_mmio_regs_t) -
	     offsetof(virtio_mmio_regs_t, device_config))) {
		TRACE(ERROR, INFO, "virtio_startup mmio: bad config size {:#x}",
		      virtio->config_size);
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	virtio_mmio_t *virtio_mmio = &virtio->frontend_data.mmio;

	spinlock_init(&virtio_mmio->banking_lock);
#if defined(PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE) &&                          \
	PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE
	spinlock_init(&virtio_mmio->interrupt_lock);
#endif

	virtio_mmio->regs = virtio_mmio_regs_container_of_device_config(
		virtio->config_cache);

	// Write initial values into the config cache
	atomic_store_relaxed(&virtio_mmio->regs->status, virtio->status);
	if (virtio->device_type != VIRTIO_DEVICE_TYPE_INVALID) {
		atomic_store_relaxed(&virtio_mmio->regs->dev_id,
				     virtio->device_type);
	}

	// Register the config cache extent as a device, so we can trap and
	// handle accesses to read-only mappings. The management VM is expected
	// to map this read-only somewhere in the guest's address space.
	virtio_mmio->vdevice.type = VDEVICE_TYPE_VIRTIO_MMIO;
	ret = vdevice_attach_phys(&virtio_mmio->vdevice,
				  virtio->config_cache_me);
	if (ret != OK) {
		TRACE(ERROR, INFO,
		      "virtio_startup mmio: failed vdevice_attach_phys: {:d}",
		      (register_t)ret);
		goto out;
	}

out:
	return ret;
}

void
virtio_mmio_handle_virtio_shutdown(virtio_t *virtio)
{
	assert(virtio != NULL);

	virtio_mmio_t *virtio_mmio = &virtio->frontend_data.mmio;

	vic_unbind(&virtio_mmio->virq_source);

	vdevice_detach_phys(&virtio_mmio->vdevice, virtio->config_cache_me);
}

error_t
virtio_mmio_frontend_bind_virq(virtio_mmio_t *virtio_mmio, vic_t *vic,
			       virq_t virq)
{
	error_t ret = OK;

	assert(virtio_mmio != NULL);
	assert(vic != NULL);

	ret = vic_bind_shared(&virtio_mmio->virq_source, vic, virq,
			      VIRQ_TRIGGER_VIRTIO_MMIO_FRONTEND);

	if ((ret == OK) && !virtio_interrupt_is_empty(atomic_load_relaxed(
				   &virtio_mmio->regs->interrupt_status))) {
		(void)virq_assert(&virtio_mmio->virq_source, false);
	}

	return ret;
}

void
virtio_mmio_frontend_unbind_virq(virtio_mmio_t *virtio_mmio)
{
	assert(virtio_mmio != NULL);

	vic_unbind_sync(&virtio_mmio->virq_source);
}

static void
virtio_mmio_update_generation(virtio_t *virtio)
{
	rcu_read_start();
	virtio_mmio_t *virtio_mmio = &virtio->frontend_data.mmio;
	uint8_result_t new_gen	   = virtio_get_generation(virtio);
	if (new_gen.e == OK) {
		atomic_store_relaxed(&virtio_mmio->regs->config_gen, new_gen.r);
	}
	rcu_read_finish();
}

static void
virtio_mmio_assert_irq(virtio_t		 *virtio,
		       virtio_interrupt_t set_interrupt_status)
{
	virtio_mmio_t *virtio_mmio = &virtio->frontend_data.mmio;

#if defined(PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE) &&                          \
	PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE
	spinlock_acquire(&virtio_mmio->interrupt_lock);
	virtio_interrupt_t old_interrupt_status =
		atomic_load_relaxed(&virtio_mmio->regs->interrupt_status);
#else
	virtio_interrupt_t old_interrupt_status = virtio_interrupt_atomic_union(
		&virtio_mmio->regs->interrupt_status, set_interrupt_status,
		memory_order_relaxed);
#endif

	virtio_interrupt_t new_interrupt_status = virtio_interrupt_union(
		old_interrupt_status, set_interrupt_status);
	bool raise_virq = !virtio_interrupt_is_empty(new_interrupt_status);

#if defined(PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE) &&                          \
	PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE
	atomic_store_relaxed(&virtio_mmio->regs->interrupt_status,
			     new_interrupt_status);
	spinlock_release(&virtio_mmio->interrupt_lock);
#endif

	if (raise_virq) {
		(void)virq_assert(&virtio_mmio->virq_source, false);
	}
}

void
virtio_mmio_handle_virtio_config_update_begin(virtio_t *virtio)
{
	virtio_mmio_update_generation(virtio);
}

void
virtio_mmio_handle_virtio_config_update_end(virtio_t *virtio)
{
	virtio_mmio_update_generation(virtio);
	virtio_interrupt_t interrupt = virtio_interrupt_default();
	virtio_interrupt_set_config_update(&interrupt, true);
	virtio_mmio_assert_irq(virtio, interrupt);
}

error_t
virtio_mmio_handle_virtio_queue_ready(virtio_t *virtio, index_t vq)
{
	// No per-queue VIRQ support
	(void)vq;
	virtio_interrupt_t interrupt = virtio_interrupt_default();
	virtio_interrupt_set_queue_ready(&interrupt, true);
	virtio_mmio_assert_irq(virtio, interrupt);
	return OK;
}

void
virtio_mmio_handle_virtio_status_updated(virtio_t	*virtio,
					 virtio_status_t new_status)
{
	virtio_mmio_t *virtio_mmio = &virtio->frontend_data.mmio;
	atomic_store_relaxed(&virtio_mmio->regs->status, new_status);
}

bool
virtio_mmio_frontend_handle_virq_check_pending(virq_source_t *source)
{
	assert(source != NULL);

	// Deassert frontend's IRQ when interrupt_status is zero, meaning no
	// interrupts are pending to be handled
	virtio_mmio_t *virtio_mmio =
		virtio_mmio_container_of_virq_source(source);

	// Note: this is an atomic load, so there is no data race; we don't need
	// to acquire interrupt_lock on targets that use it to make updates
	// atomic; if this runs concurrently with an update, we are guaranteed
	// to see either the old or the new status.
	return !virtio_interrupt_is_empty(
		atomic_load_relaxed(&virtio_mmio->regs->interrupt_status));
}
