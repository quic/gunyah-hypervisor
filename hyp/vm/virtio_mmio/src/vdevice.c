// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypconstants.h>
#include <hypcontainers.h>

#include <atomic.h>
#include <partition.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <virq.h>

#include <asm/nospec_checks.h>

#include "event_handlers.h"
#include "virtio_mmio.h"

static bool
virtio_mmio_access_allowed(size_t size, size_t offset)
{
	bool ret;

	// First check if the access is size-aligned
	if ((offset & (size - 1U)) != 0UL) {
		ret = false;
	} else if (size == sizeof(uint32_t)) {
		// Word accesses, always allowed
		ret = true;
	} else if (size == sizeof(uint8_t)) {
		// Byte accesses only allowed for config
		ret = ((offset >= OFS_VIRTIO_MMIO_REGS_CONFIG(0U)) &&
		       (offset <= OFS_VIRTIO_MMIO_REGS_CONFIG((
					  VIRTIO_MMIO_REG_CONFIG_BYTES - 1U))));
	} else {
		// Invalid access size
		ret = false;
	}

	return ret;
}

static bool
virtio_mmio_default_write(const virtio_mmio_t *virtio_mmio, size_t offset,
			  size_t access_size, uint32_t val)
{
	bool ret = true;

	if ((offset >= OFS_VIRTIO_MMIO_REGS_CONFIG(0U)) &&
	    (offset <= OFS_VIRTIO_MMIO_REGS_CONFIG(
			       (VIRTIO_MMIO_REG_CONFIG_BYTES - 1U)))) {
		index_t n = (index_t)(offset - OFS_VIRTIO_MMIO_REGS_CONFIG(0U));
		// Loop through every byte
		uint32_t shifted_val = val;
		for (index_t i = 0U; i < access_size; i++) {
			atomic_store_relaxed(&virtio_mmio->regs->config[n + i],
					     (uint8_t)shifted_val);
			shifted_val >>= 8U;
		}
	} else {
		ret = false;
	}

	return ret;
}

static bool
virtio_mmio_write_queue_sel(virtio_mmio_t *virtio_mmio, uint32_t val)
{
	bool	       ret = true;
	index_result_t res = nospec_range_check(val, virtio_mmio->vqs_num);
	if (res.e == OK) {
		virtio_mmio->queue_sel = res.r;

		// Update corresponding banked registers with read
		// permission
		spinlock_acquire(&virtio_mmio->lock);
		atomic_store_relaxed(
			&virtio_mmio->regs->queue_num_max,
			virtio_mmio->banked_queue_regs[res.r].num_max);
		atomic_store_relaxed(
			&virtio_mmio->regs->queue_ready,
			virtio_mmio->banked_queue_regs[res.r].ready);
		spinlock_release(&virtio_mmio->lock);
	} else {
		ret = false;
	}

	return ret;
}

static bool
virtio_mmio_write_status_reg(virtio_mmio_t *virtio_mmio, uint32_t val)
{
	bool			    ret = true;
	virtio_mmio_notify_reason_t reason;

	if (val != 0U) {
		bool assert_virq = false;

		spinlock_acquire(&virtio_mmio->lock);

		virtio_mmio_status_reg_t old_val =
			atomic_load_relaxed(&virtio_mmio->regs->status);
		virtio_mmio_status_reg_t new_val =
			virtio_mmio_status_reg_cast(val);
		atomic_store_relaxed(&virtio_mmio->regs->status, new_val);

		reason = atomic_load_relaxed(&virtio_mmio->reason);

		if (!virtio_mmio_status_reg_get_driver_ok(&old_val) &&
		    virtio_mmio_status_reg_get_driver_ok(&new_val)) {
			virtio_mmio_notify_reason_set_driver_ok(&reason, true);
			assert_virq = true;

		} else if (!virtio_mmio_status_reg_get_failed(&old_val) &&
			   virtio_mmio_status_reg_get_failed(&new_val)) {
			virtio_mmio_notify_reason_set_failed(&reason, true);
			assert_virq = true;
		} else {
			// Nothing to do
		}

		atomic_store_relaxed(&virtio_mmio->reason, reason);

		spinlock_release(&virtio_mmio->lock);

		if (assert_virq) {
			atomic_thread_fence(memory_order_release);
			(void)virq_assert(&virtio_mmio->frontend_source, false);
		}
	} else if (virtio_mmio_status_reg_raw(atomic_load_relaxed(
			   &virtio_mmio->regs->status)) == 0U) {
		// We do not request a reset the first time the frontend
		// tries to write a zero to the status register
	} else {
		// Assert backend's IRQ to let the
		// backend know that a device reset has been requested.
		spinlock_acquire(&virtio_mmio->lock);
		virtio_mmio_status_reg_t status =
			atomic_load_relaxed(&virtio_mmio->regs->status);
		virtio_mmio_status_reg_set_device_needs_reset(&status, true);
		atomic_store_relaxed(&virtio_mmio->regs->status, status);

		reason = atomic_load_relaxed(&virtio_mmio->reason);
		virtio_mmio_notify_reason_set_reset_rqst(&reason, true);
		atomic_store_relaxed(&virtio_mmio->reason, reason);
		spinlock_release(&virtio_mmio->lock);

		// Clear all bits QueueReady for all queues in the
		// device.
		for (index_t i = 0; i < virtio_mmio->vqs_num; i++) {
			virtio_mmio->banked_queue_regs[i].ready = 0U;
		}
		atomic_store_relaxed(&virtio_mmio->regs->queue_ready, 0U);

		atomic_thread_fence(memory_order_release);
		ret = virq_assert(&virtio_mmio->frontend_source, false).r;
	}

	return ret;
}

static bool
virtio_mmio_write_dev_feat_sel(const virtio_mmio_t *virtio_mmio, uint32_t val)
{
	bool	       ret = true;
	index_result_t res = nospec_range_check(val, VIRTIO_MMIO_DEV_FEAT_NUM);
	if (res.e == OK) {
		// Update corresponding banked register
		atomic_store_relaxed(&virtio_mmio->regs->dev_feat,
				     virtio_mmio->banked_dev_feat[res.r]);
	} else {
		ret = false;
	}

	return ret;
}

static bool
virtio_mmio_write_drv_feat_sel(virtio_mmio_t *virtio_mmio, uint32_t val)
{
	bool	       ret = true;
	index_result_t res = nospec_range_check(val, VIRTIO_MMIO_DRV_FEAT_NUM);
	if (res.e == OK) {
		virtio_mmio->drv_feat_sel = res.r;
	} else {
		ret = false;
	}

	return ret;
}

static void
virtio_mmio_write_queue_notify(virtio_mmio_t *virtio_mmio, uint32_t val)
{
	virtio_mmio_notify_reason_t reason;

	spinlock_acquire(&virtio_mmio->lock);

	// Update bitmap of virtual queues to be notified
	(void)atomic_fetch_or_explicit(&virtio_mmio->vqs_bitmap, util_bit(val),
				       memory_order_relaxed);

	reason = atomic_load_relaxed(&virtio_mmio->reason);
	virtio_mmio_notify_reason_set_new_buffer(&reason, true);
	atomic_store_relaxed(&virtio_mmio->reason, reason);

	spinlock_release(&virtio_mmio->lock);

	// Assert backend's IRQ to notify the backend that there are new
	// buffers to process
	atomic_thread_fence(memory_order_release);
	(void)virq_assert(&virtio_mmio->frontend_source, false);
}

static void
virtio_mmio_write_interrupt_ack(virtio_mmio_t *virtio_mmio, uint32_t val)
{
	virtio_mmio_notify_reason_t reason;

	atomic_store_relaxed(&virtio_mmio->regs->interrupt_ack, val);

	spinlock_acquire(&virtio_mmio->lock);

	uint32_t interrupt_status =
		atomic_load_relaxed(&virtio_mmio->regs->interrupt_status);
	interrupt_status &= ~val;
	atomic_store_relaxed(&virtio_mmio->regs->interrupt_status,
			     interrupt_status);

	// Assert backend's IRQ so that the backend can continue raising
	// interrupts
	reason = atomic_load_relaxed(&virtio_mmio->reason);
	virtio_mmio_notify_reason_set_interrupt_ack(&reason, true);
	atomic_store_relaxed(&virtio_mmio->reason, reason);

	spinlock_release(&virtio_mmio->lock);

	atomic_thread_fence(memory_order_release);
	(void)virq_assert(&virtio_mmio->frontend_source, false);
}

static bool
virtio_mmio_vdevice_write(virtio_mmio_t *virtio_mmio, size_t offset,
			  uint32_t val, size_t access_size)
{
	bool ret = true;

	switch (offset) {
	case OFS_VIRTIO_MMIO_REGS_DEV_FEAT_SEL:
		ret = virtio_mmio_write_dev_feat_sel(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_DRV_FEAT:
		virtio_mmio->banked_drv_feat[virtio_mmio->drv_feat_sel] = val;
		break;

	case OFS_VIRTIO_MMIO_REGS_DRV_FEAT_SEL:
		ret = virtio_mmio_write_drv_feat_sel(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_SEL:
		ret = virtio_mmio_write_queue_sel(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_NUM:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].num =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_READY:
		atomic_store_relaxed(&virtio_mmio->regs->queue_ready, val);

		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].ready =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_NOTIFY:
		virtio_mmio_write_queue_notify(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_INTERRUPT_ACK:
		virtio_mmio_write_interrupt_ack(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_STATUS:
		// We should not allow the frontend to write 0 to the device
		// status since a 0 status means that the device reset is
		// complete
		ret = virtio_mmio_write_status_reg(virtio_mmio, val);
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DESC_LOW:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].desc_low =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DESC_HIGH:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel]
			.desc_high = val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DRV_LOW:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].drv_low =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DRV_HIGH:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].drv_high =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DEV_LOW:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].dev_low =
			val;
		break;

	case OFS_VIRTIO_MMIO_REGS_QUEUE_DEV_HIGH:
		virtio_mmio->banked_queue_regs[virtio_mmio->queue_sel].dev_high =
			val;
		break;

	default:
		ret = virtio_mmio_default_write(virtio_mmio, offset,
						access_size, val);
		break;
	}

	return ret;
}

vcpu_trap_result_t
virtio_mmio_handle_vdevice_access(vdevice_t *vdevice, size_t offset,
				  size_t access_size, register_t *value,
				  bool is_write)
{
	vcpu_trap_result_t ret;

	// Trap only writes from virtio's frontend
	if (!is_write) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	assert((vdevice != NULL) &&
	       (vdevice->type == VDEVICE_TYPE_VIRTIO_MMIO));
	virtio_mmio_t *virtio_mmio =
		virtio_mmio_container_of_frontend_device(vdevice);
	if (virtio_mmio == NULL) {
		ret = VCPU_TRAP_RESULT_UNHANDLED;
		goto out;
	}

	if (!virtio_mmio_access_allowed(access_size, offset)) {
		ret = VCPU_TRAP_RESULT_FAULT;
		goto out;
	}

	ret = virtio_mmio_vdevice_write(virtio_mmio, offset, (uint32_t)*value,
					access_size)
		      ? VCPU_TRAP_RESULT_EMULATED
		      : VCPU_TRAP_RESULT_FAULT;

out:
	return ret;
}
