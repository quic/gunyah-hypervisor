// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hyprights.h>

#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <object.h>
#include <partition.h>
#include <spinlock.h>
#include <util.h>
#include <virq.h>

#include <asm/nospec_checks.h>

#include "virtio_mmio.h"

error_t
hypercall_virtio_mmio_configure(cap_id_t virtio_mmio_cap,
				cap_id_t memextent_cap, count_t vqs_num,
				virtio_option_flags_t flags,
				virtio_device_type_t  device_type)
{
	error_t	      err;
	cspace_t     *cspace = cspace_get_self();
	object_type_t type;

	memextent_ptr_result_t m = cspace_lookup_memextent(
		cspace, memextent_cap, CAP_RIGHTS_MEMEXTENT_ATTACH);
	if (compiler_unexpected(m.e != OK)) {
		err = m.e;
		goto out;
	}

	memextent_t *memextent = m.r;

	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, virtio_mmio_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		err = o.e;
		goto out_memextent_release;
	}
	if (type != OBJECT_TYPE_VIRTIO_MMIO) {
		err = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		goto out_virtio_mmio_release;
	}

	virtio_mmio_t *virtio_mmio = o.r.virtio_mmio;

	spinlock_acquire(&virtio_mmio->header.lock);

	if (atomic_load_relaxed(&virtio_mmio->header.state) ==
	    OBJECT_STATE_INIT) {
		err = virtio_mmio_configure(virtio_mmio, memextent, vqs_num,
					    flags, device_type);
	} else {
		err = ERROR_OBJECT_STATE;
	}

	spinlock_release(&virtio_mmio->header.lock);
out_virtio_mmio_release:
	object_put(type, o.r);
out_memextent_release:
	object_put_memextent(memextent);
out:
	return err;
}

error_t
hypercall_virtio_mmio_backend_bind_virq(cap_id_t virtio_mmio_cap,
					cap_id_t vic_cap, virq_t virq)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap,
		CAP_RIGHTS_VIRTIO_MMIO_BIND_BACKEND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_virtio_mmio_release;
	}
	vic_t *vic = v.r;

	err = virtio_mmio_backend_bind_virq(virtio_mmio, vic, virq);

	object_put_vic(vic);
out_virtio_mmio_release:
	object_put_virtio_mmio(virtio_mmio);
out:
	return err;
}

error_t
hypercall_virtio_mmio_backend_unbind_virq(cap_id_t virtio_mmio_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap,
		CAP_RIGHTS_VIRTIO_MMIO_BIND_BACKEND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	virtio_mmio_backend_unbind_virq(virtio_mmio);

	object_put_virtio_mmio(virtio_mmio);
out:
	return err;
}

error_t
hypercall_virtio_mmio_backend_assert_virq(cap_id_t virtio_mmio_cap,
					  uint32_t interrupt_status)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_ASSERT_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	virtio_mmio_status_reg_t status =
		atomic_load_relaxed(&virtio_mmio->regs->status);

	if (virtio_mmio_status_reg_get_device_needs_reset(&status)) {
		err = ERROR_DENIED;
	} else {
#if defined(PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE) &&                          \
	PLATFORM_NO_DEVICE_ATTR_ATOMIC_UPDATE
		spinlock_acquire(&virtio_mmio->lock);
		uint32_t new_irq_status = atomic_load_relaxed(
			&virtio_mmio->regs->interrupt_status);
		new_irq_status |= interrupt_status;
		atomic_store_relaxed(&virtio_mmio->regs->interrupt_status,
				     new_irq_status);
		spinlock_release(&virtio_mmio->lock);
#else
		(void)atomic_fetch_or_explicit(
			&virtio_mmio->regs->interrupt_status, interrupt_status,
			memory_order_relaxed);
#endif
		atomic_thread_fence(memory_order_release);
		// Assert frontend's IRQ
		(void)virq_assert(&virtio_mmio->backend_source, false);
	}

	object_put_virtio_mmio(virtio_mmio);
out:
	return err;
}

error_t
hypercall_virtio_mmio_frontend_bind_virq(cap_id_t virtio_mmio_cap,
					 cap_id_t vic_cap, virq_t virq)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap,
		CAP_RIGHTS_VIRTIO_MMIO_BIND_FRONTEND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	vic_ptr_result_t v =
		cspace_lookup_vic(cspace, vic_cap, CAP_RIGHTS_VIC_BIND_SOURCE);
	if (compiler_unexpected(v.e != OK)) {
		err = v.e;
		goto out_virtio_mmio_release;
	}
	vic_t *vic = v.r;

	err = virtio_mmio_frontend_bind_virq(virtio_mmio, vic, virq);

	object_put_vic(vic);
out_virtio_mmio_release:
	object_put_virtio_mmio(virtio_mmio);
out:
	return err;
}

error_t
hypercall_virtio_mmio_frontend_unbind_virq(cap_id_t virtio_mmio_cap)
{
	error_t	  err	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap,
		CAP_RIGHTS_VIRTIO_MMIO_BIND_FRONTEND_VIRQ);
	if (compiler_unexpected(p.e != OK)) {
		err = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	virtio_mmio_frontend_unbind_virq(virtio_mmio);

	object_put_virtio_mmio(virtio_mmio);
out:
	return err;
}

error_t
hypercall_virtio_mmio_backend_set_dev_features(cap_id_t virtio_mmio_cap,
					       uint32_t sel, uint32_t dev_feat)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	index_result_t res = nospec_range_check(sel, VIRTIO_MMIO_DEV_FEAT_NUM);
	if (res.e != OK) {
		ret = res.e;
		goto set_failed;
	}

	// Check features enforced by the hypervisor
	if (res.r == 1U) {
		uint32_t allow =
			(uint32_t)util_bit((VIRTIO_F_VERSION_1 - 32U)) |
			(uint32_t)util_bit((VIRTIO_F_ACCESS_PLATFORM - 32U)) |
			dev_feat;
		uint32_t forbid = ~(uint32_t)util_bit(
					  (VIRTIO_F_NOTIFICATION_DATA - 32U)) &
				  dev_feat;

		if ((allow != dev_feat) || (forbid != dev_feat)) {
			ret = ERROR_DENIED;
			goto set_failed;
		}
	}

	virtio_mmio->banked_dev_feat[res.r] = dev_feat;

set_failed:
	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

error_t
hypercall_virtio_mmio_backend_set_queue_num_max(cap_id_t virtio_mmio_cap,
						uint32_t sel,
						uint32_t queue_num_max)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	index_result_t res = nospec_range_check(sel, virtio_mmio->vqs_num);
	if (res.e == OK) {
		virtio_mmio->banked_queue_regs[res.r].num_max = queue_num_max;
	} else {
		ret = res.e;
	}

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

hypercall_virtio_mmio_backend_get_drv_features_result_t
hypercall_virtio_mmio_backend_get_drv_features(cap_id_t virtio_mmio_cap,
					       uint32_t sel)
{
	hypercall_virtio_mmio_backend_get_drv_features_result_t ret = { 0 };
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	index_result_t res = nospec_range_check(sel, VIRTIO_MMIO_DRV_FEAT_NUM);
	if (res.e == OK) {
		ret.drv_feat = virtio_mmio->banked_drv_feat[res.r];
		ret.error    = OK;
	} else {
		ret.error = res.e;
	}

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

hypercall_virtio_mmio_backend_get_queue_info_result_t
hypercall_virtio_mmio_backend_get_queue_info(cap_id_t virtio_mmio_cap,
					     uint32_t sel)
{
	hypercall_virtio_mmio_backend_get_queue_info_result_t ret = { 0 };
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	index_result_t res = nospec_range_check(sel, virtio_mmio->vqs_num);
	if (res.e != OK) {
		object_put_virtio_mmio(virtio_mmio);
		ret.error = res.e;
		goto out;
	}

	virtio_mmio_banked_queue_registers_t *queue_regs =
		&virtio_mmio->banked_queue_regs[res.r];

	ret.queue_num	= queue_regs->num;
	ret.queue_ready = queue_regs->ready;

	ret.queue_desc = queue_regs->desc_high;
	ret.queue_desc = ret.queue_desc << 32;
	ret.queue_desc |= (register_t)queue_regs->desc_low;

	ret.queue_drv = queue_regs->drv_high;
	ret.queue_drv = ret.queue_drv << 32;
	ret.queue_drv |= (register_t)queue_regs->drv_low;

	ret.queue_dev = queue_regs->dev_high;
	ret.queue_dev = ret.queue_dev << 32;
	ret.queue_dev |= (register_t)queue_regs->dev_low;

	ret.error = OK;

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

hypercall_virtio_mmio_backend_get_notification_result_t
hypercall_virtio_mmio_backend_get_notification(cap_id_t virtio_mmio_cap)
{
	hypercall_virtio_mmio_backend_get_notification_result_t ret = { 0 };
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret.error = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	spinlock_acquire(&virtio_mmio->lock);
	ret.vqs_bitmap = atomic_exchange_explicit(&virtio_mmio->vqs_bitmap, 0U,
						  memory_order_relaxed);
	ret.reason     = atomic_load_relaxed(&virtio_mmio->reason);
	atomic_store_relaxed(&virtio_mmio->reason,
			     virtio_mmio_notify_reason_default());
	spinlock_release(&virtio_mmio->lock);

	ret.error = OK;

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

error_t
hypercall_virtio_mmio_backend_acknowledge_reset(cap_id_t virtio_mmio_cap)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	spinlock_acquire(&virtio_mmio->lock);
	atomic_store_relaxed(&virtio_mmio->regs->status,
			     virtio_mmio_status_reg_default());
	spinlock_release(&virtio_mmio->lock);

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}

error_t
hypercall_virtio_mmio_backend_update_status(cap_id_t virtio_mmio_cap,
					    uint32_t val)
{
	error_t	  ret	 = OK;
	cspace_t *cspace = cspace_get_self();

	virtio_mmio_ptr_result_t p = cspace_lookup_virtio_mmio(
		cspace, virtio_mmio_cap, CAP_RIGHTS_VIRTIO_MMIO_CONFIG);
	if (compiler_unexpected(p.e != OK)) {
		ret = p.e;
		goto out;
	}
	virtio_mmio_t *virtio_mmio = p.r;

	spinlock_acquire(&virtio_mmio->lock);
	uint32_t status = virtio_mmio_status_reg_raw(
		atomic_load_relaxed(&virtio_mmio->regs->status));
	status |= val;
	atomic_store_relaxed(&virtio_mmio->regs->status,
			     virtio_mmio_status_reg_cast(status));
	spinlock_release(&virtio_mmio->lock);

	object_put_virtio_mmio(virtio_mmio);
out:
	return ret;
}
