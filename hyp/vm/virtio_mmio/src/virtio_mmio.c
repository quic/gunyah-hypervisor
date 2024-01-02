// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <hyp_aspace.h>
#include <log.h>
#include <memextent.h>
#include <object.h>
#include <partition.h>
#include <pgtable.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>
#include <vdevice.h>
#include <vic.h>

#include <events/virtio_mmio.h>

#include <asm/cache.h>
#include <asm/cpu.h>

#include "event_handlers.h"
#include "panic.h"
#include "virtio_mmio.h"

error_t
virtio_mmio_handle_object_create_virtio_mmio(virtio_mmio_create_t create)
{
	virtio_mmio_t *virtio_mmio = create.virtio_mmio;
	spinlock_init(&virtio_mmio->lock);

	return OK;
}

error_t
virtio_mmio_configure(virtio_mmio_t *virtio_mmio, memextent_t *memextent,
		      count_t vqs_num, virtio_option_flags_t flags,
		      virtio_device_type_t device_type)
{
	error_t ret = OK;

	assert(virtio_mmio != NULL);
	assert(memextent != NULL);

	// Memextent should only cover one contiguous virtio config page
	if ((memextent->type != MEMEXTENT_TYPE_BASIC) ||
	    (memextent->size != PGTABLE_VM_PAGE_SIZE) ||
	    (vqs_num > VIRTIO_MMIO_MAX_VQS)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	if (virtio_option_flags_get_valid_device_type(&flags)) {
		if (trigger_virtio_mmio_valid_device_type_event(device_type)) {
			virtio_mmio->device_type = device_type;
		} else {
			ret = ERROR_ARGUMENT_INVALID;
			goto out;
		}
	} else {
		virtio_mmio->device_type = VIRTIO_DEVICE_TYPE_INVALID;
	}

	if (virtio_mmio->me != NULL) {
		object_put_memextent(virtio_mmio->me);
	}

	virtio_mmio->me	     = object_get_memextent_additional(memextent);
	virtio_mmio->vqs_num = vqs_num;

out:
	return ret;
}

error_t
virtio_mmio_handle_object_activate_virtio_mmio(virtio_mmio_t *virtio_mmio)
{
	error_t ret = OK;

	assert(virtio_mmio != NULL);

	partition_t *partition = virtio_mmio->header.partition;

	if (virtio_mmio->me == NULL) {
		ret = ERROR_OBJECT_CONFIG;
		goto error_no_me;
	}

	virtio_mmio->frontend_device.type = VDEVICE_TYPE_VIRTIO_MMIO;
	ret = vdevice_attach_phys(&virtio_mmio->frontend_device,
				  virtio_mmio->me);
	if (ret != OK) {
		goto error_vdevice;
	}

	// Allocate banked registers based on how many virtual queues will be
	// used
	size_t alloc_size = virtio_mmio->vqs_num *
			    sizeof(virtio_mmio_banked_queue_registers_t);

	void_ptr_result_t alloc_ret =
		partition_alloc(partition, alloc_size, alignof(uint32_t *));
	if (alloc_ret.e != OK) {
		ret = ERROR_NOMEM;
		goto out;
	}
	(void)memset_s(alloc_ret.r, alloc_size, 0, alloc_size);

	virtio_mmio->banked_queue_regs =
		(virtio_mmio_banked_queue_registers_t *)alloc_ret.r;

	ret = trigger_virtio_mmio_device_config_activate_event(
		virtio_mmio->device_type, virtio_mmio);
	if (ret != OK) {
		goto out;
	}

	// Allocate virtio config page
	size_t size = virtio_mmio->me->size;
	if (size < sizeof(*virtio_mmio->regs)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	virt_range_result_t range = hyp_aspace_allocate(size);
	if (range.e != OK) {
		ret = range.e;
		goto out;
	}

	ret = memextent_attach(partition, virtio_mmio->me, range.r.base,
			       sizeof(*virtio_mmio->regs));
	if (ret != OK) {
		hyp_aspace_deallocate(partition, range.r);
		goto out;
	}

	virtio_mmio->regs = (virtio_mmio_regs_t *)range.r.base;
	virtio_mmio->size = range.r.size;

	// Flush cache before using the uncached mapping
	CACHE_CLEAN_OBJECT(*virtio_mmio->regs);

out:
	if (ret != OK) {
		vdevice_detach_phys(&virtio_mmio->frontend_device,
				    virtio_mmio->me);
	}
error_vdevice:
error_no_me:
	return ret;
}

void
virtio_mmio_handle_object_deactivate_virtio_mmio(virtio_mmio_t *virtio_mmio)
{
	assert(virtio_mmio != NULL);

	vic_unbind(&virtio_mmio->backend_source);
	vic_unbind(&virtio_mmio->frontend_source);

	vdevice_detach_phys(&virtio_mmio->frontend_device, virtio_mmio->me);
}

void
virtio_mmio_handle_object_cleanup_virtio_mmio(virtio_mmio_t *virtio_mmio)
{
	assert(virtio_mmio != NULL);

	partition_t *partition = virtio_mmio->header.partition;

	if (virtio_mmio->regs != NULL) {
		memextent_detach(partition, virtio_mmio->me);

		virt_range_t range = { .base = (uintptr_t)virtio_mmio->regs,
				       .size = virtio_mmio->size };

		hyp_aspace_deallocate(partition, range);

		virtio_mmio->regs = NULL;
		virtio_mmio->size = 0U;
	}

	if (virtio_mmio->banked_queue_regs != NULL) {
		size_t alloc_size =
			virtio_mmio->vqs_num *
			sizeof(virtio_mmio_banked_queue_registers_t);
		void *alloc_base = (void *)virtio_mmio->banked_queue_regs;

		error_t err = partition_free(partition, alloc_base, alloc_size);
		assert(err == OK);

		virtio_mmio->banked_queue_regs = NULL;
		virtio_mmio->vqs_num	       = 0U;
	}

	(void)trigger_virtio_mmio_device_config_cleanup_event(
		virtio_mmio->device_type, virtio_mmio);

	if (virtio_mmio->me != NULL) {
		object_put_memextent(virtio_mmio->me);
		virtio_mmio->me = NULL;
	}
}

void
virtio_mmio_unwind_object_activate_virtio_mmio(virtio_mmio_t *virtio_mmio)
{
	virtio_mmio_handle_object_deactivate_virtio_mmio(virtio_mmio);
	virtio_mmio_handle_object_cleanup_virtio_mmio(virtio_mmio);
}

error_t
virtio_mmio_backend_bind_virq(virtio_mmio_t *virtio_mmio, vic_t *vic,
			      virq_t virq)
{
	error_t ret = OK;

	assert(virtio_mmio != NULL);
	assert(vic != NULL);

	ret = vic_bind_shared(&virtio_mmio->backend_source, vic, virq,
			      VIRQ_TRIGGER_VIRTIO_MMIO_BACKEND);

	return ret;
}

void
virtio_mmio_backend_unbind_virq(virtio_mmio_t *virtio_mmio)
{
	assert(virtio_mmio != NULL);

	vic_unbind_sync(&virtio_mmio->backend_source);
}

error_t
virtio_mmio_frontend_bind_virq(virtio_mmio_t *virtio_mmio, vic_t *vic,
			       virq_t virq)
{
	error_t ret = OK;

	assert(virtio_mmio != NULL);
	assert(vic != NULL);

	ret = vic_bind_shared(&virtio_mmio->frontend_source, vic, virq,
			      VIRQ_TRIGGER_VIRTIO_MMIO_FRONTEND);

	return ret;
}

void
virtio_mmio_frontend_unbind_virq(virtio_mmio_t *virtio_mmio)
{
	assert(virtio_mmio != NULL);

	vic_unbind_sync(&virtio_mmio->frontend_source);
}

bool
virtio_mmio_frontend_handle_virq_check_pending(virq_source_t *source)
{
	assert(source != NULL);

	// Deassert backend's IRQ when get_notification has been called
	virtio_mmio_t *virtio_mmio =
		virtio_mmio_container_of_frontend_source(source);

	virtio_mmio_notify_reason_t reason =
		atomic_load_relaxed(&virtio_mmio->reason);
	return !virtio_mmio_notify_reason_is_equal(
		reason, virtio_mmio_notify_reason_default());
}

bool
virtio_mmio_backend_handle_virq_check_pending(virq_source_t *source)
{
	assert(source != NULL);

	// Deassert frontend's IRQ when interrupt_status is zero, meaning no
	// interrupts are pending to be handled
	virtio_mmio_t *virtio_mmio =
		virtio_mmio_container_of_backend_source(source);

	return (atomic_load_relaxed(&virtio_mmio->regs->interrupt_status) !=
		0U);
}

error_t
virtio_default_handle_object_activate(virtio_mmio_t *virtio_mmio)
{
	(void)virtio_mmio;
	return OK;
}

error_t
virtio_default_handle_object_cleanup(virtio_mmio_t *virtio_mmio)
{
	(void)virtio_mmio;
	return OK;
}
