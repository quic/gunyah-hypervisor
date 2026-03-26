// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <object.h>
#include <rcu.h>
#include <util.h>
#include <virtio.h>
#include <vpci.h>

#include <asm/nospec_checks.h>

#include "event_handlers.h"

static virtio_t *
virtio_from_virtio_pci(virtio_pci_t *virtio_pci)
{
	return virtio_container_of_frontend_data(
		virtio_frontend_container_of_pci(virtio_pci));
}

static register_result_t
virtio_pci_handle_vpci_config_read_bar_access(vpci_function_t	    *function,
					      virtio_pci_cap_data_t *cap_data,
					      size_t *access_size)
	REQUIRE_RCU_READ
{
	index_t bar_index = cap_data->bar_index;
	size_t	offset	  = cap_data->bar_offset;
	size_t	size	  = cap_data->bar_length;

	register_result_t access_r = virtio_pci_handle_vpci_bar_read(
		function, bar_index, offset, size);

	return (access_r.e == OK)
		       ? register_result_ok(
				 access_r.r &
				 util_mask(util_width(uint8_t) * *access_size))
		       : register_result_error(access_r.e);
}

static error_t
virtio_pci_handle_vpci_config_write_bar_access(vpci_function_t	     *function,
					       virtio_pci_cap_data_t *cap_data,
					       size_t	 *access_size,
					       register_t data) REQUIRE_RCU_READ
{
	index_t bar_index = cap_data->bar_index;
	size_t	offset	  = cap_data->bar_offset;
	size_t	size	  = cap_data->bar_length;

	return virtio_pci_handle_vpci_bar_write(
		function, bar_index, offset, size,
		data & util_mask(util_width(uint8_t) * *access_size));
}

static register_result_t
virtio_pci_cap_read_one(vpci_function_t	      *function,
			virtio_pci_cap_data_t *cap_data, size_t offset,
			size_t *access_size) REQUIRE_RCU_READ
{
	register_result_t ret;

	if (offset == offsetof(virtio_pci_capability_t, type)) {
		ret = register_result_ok((register_t)cap_data->type);
		*access_size =
			util_sizeof_member(virtio_pci_capability_t, type);
	} else if (offset == offsetof(virtio_pci_capability_t, bar_index)) {
		ret = register_result_ok(cap_data->bar_index);
		*access_size =
			util_sizeof_member(virtio_pci_capability_t, bar_index);
	} else if (offset == offsetof(virtio_pci_capability_t, bar_offset)) {
		ret = register_result_ok(cap_data->bar_offset);
		*access_size =
			util_sizeof_member(virtio_pci_capability_t, bar_offset);
	} else if (offset == offsetof(virtio_pci_capability_t, bar_length)) {
		ret = register_result_ok(cap_data->bar_length);
		*access_size =
			util_sizeof_member(virtio_pci_capability_t, bar_length);
	} else if (offset < sizeof(virtio_pci_capability_t)) {
		// handle undefined bytes
		ret	     = register_result_ok(0U);
		*access_size = 1U;
	} else if ((cap_data->type == VIRTIO_PCI_CAP_TYPE_NOTIFY_CFG) &&
		   (offset == offsetof(virtio_pci_notify_capability_t,
				       notify_off_multiplier))) {
		// All queues share one notify register
		ret	     = register_result_ok(0U);
		*access_size = util_sizeof_member(
			virtio_pci_notify_capability_t, notify_off_multiplier);
	} else if ((cap_data->type == VIRTIO_PCI_CAP_TYPE_PCI_CFG) &&
		   (offset ==
		    offsetof(virtio_pci_access_capability_t, pci_cfg_data))) {
		ret = virtio_pci_handle_vpci_config_read_bar_access(
			function, cap_data, access_size);
	} else {
		// Unknown vendor cap ID
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	}

	return ret;
}

static register_result_t
virtio_pci_cap_read(vpci_function_t *function, virtio_pci_cap_data_t *cap_data,
		    size_t offset, size_t access_size) REQUIRE_RCU_READ
{
	register_result_t ret;

	register_t read_val  = 0U;
	size_t	   read_size = 0U;

	while (read_size < access_size) {
		size_t read_offset   = offset + read_size;
		size_t accessed_size = access_size - read_size;

		register_result_t access_r = virtio_pci_cap_read_one(
			function, cap_data, read_offset, &accessed_size);
		if (access_r.e != OK) {
			ret = access_r;
			goto out;
		}

		// Handle partial accesses (note all fields are size-aligned)
		size_t mask = accessed_size - 1U;
		if ((read_offset & mask) != 0U) {
			access_r.r >>=
				util_width(uint8_t) * (read_offset & mask);
			accessed_size = (0U - read_offset) & mask;
		}

		read_val |= access_r.r << (util_width(uint8_t) * read_size);
		read_size += accessed_size;
	}

	ret = register_result_ok(read_val);
out:
	return ret;
}

register_result_t
virtio_pci_handle_vpci_config_read(vpci_function_t	  *function,
				   vpci_capability_data_t *capability,
				   size_t offset, size_t access_size)
{
	register_result_t ret;

	if (access_size > sizeof(ret.r)) {
		// Access larger than one register
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else if (capability == NULL) {
		// Access outside of any capability
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else if (capability->type != PCI_CAPABILITY_TYPE_STANDARD) {
		// Only standard capabilities are supported
		// FIXME: QC Gunyah issue #291
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_MSIX) {
		ret = vpci_msix_config_read(
			vpci_msix_data_container_of_base(capability), offset,
			access_size);
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_PCIE) {
		ret = vpci_pcie_config_read(
			vpci_pcie_data_container_of_base(capability), offset,
			access_size);
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_PM) {
		// Power Management
		// The vcpi power state is hardcoded. Just return the defaults
		ret = vpci_pm_config_read(offset, access_size);
	} else if (capability->cap_id != (uint16_t)PCI_CAPABILITY_ID_VENDOR) {
		// Unknown capability
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else {
		virtio_pci_cap_data_t *cap_data =
			virtio_pci_cap_data_container_of_base(capability);
		ret = virtio_pci_cap_read(function, cap_data, offset,
					  access_size);
	}

	return ret;
}

static error_t
virtio_pci_cap_write_one(vpci_function_t       *function,
			 virtio_pci_cap_data_t *cap_data, size_t offset,
			 size_t *access_size, register_t data) REQUIRE_RCU_READ
{
	error_t err;

	if (cap_data->type != VIRTIO_PCI_CAP_TYPE_PCI_CFG) {
		// Not writable
		err = ERROR_UNIMPLEMENTED;
	} else if (util_range_is_member(offset, *access_size,
					virtio_pci_capability_t, bar_index)) {
		cap_data->bar_index = (index_t)data;
		err		    = OK;
	} else if (util_range_is_member(offset, *access_size,
					virtio_pci_capability_t, bar_offset)) {
		cap_data->bar_offset = (uint32_t)data;
		err		     = OK;
	} else if (util_range_is_member(offset, *access_size,
					virtio_pci_capability_t, bar_length)) {
		cap_data->bar_length = (uint32_t)data;
		err		     = OK;
	} else if (util_range_at_member(offset, *access_size,
					virtio_pci_access_capability_t,
					pci_cfg_data)) {
		err = virtio_pci_handle_vpci_config_write_bar_access(
			function, cap_data, access_size, data);
	} else {
		// Unknown or non-writable offset
		err = ERROR_UNIMPLEMENTED;
	}
	return err;
}

static error_t
virtio_pci_cap_write(vpci_function_t *function, virtio_pci_cap_data_t *cap_data,
		     size_t offset, size_t access_size, register_t data)
	REQUIRE_RCU_READ
{
	error_t ret;

	size_t	   reg_offset	  = offset;
	size_t	   remaining_size = access_size;
	register_t value	  = data;
	while (remaining_size > 0U) {
		size_t accessed_size = remaining_size;

		ret = virtio_pci_cap_write_one(function, cap_data, reg_offset,
					       &accessed_size, value);
		if (ret != OK) {
			goto failed;
		}

		assert(remaining_size <= accessed_size);

		remaining_size -= accessed_size;
		reg_offset += accessed_size;
		value >>= accessed_size * util_width(uint8_t);
	}
	ret = OK;
failed:
	return ret;
}

error_t
virtio_pci_handle_vpci_config_write(vpci_function_t	   *function,
				    vpci_capability_data_t *capability,
				    size_t offset, size_t access_size,
				    register_t data)
{
	error_t err;

	if (access_size > sizeof(data)) {
		// Access larger than one register
		err = ERROR_UNIMPLEMENTED;
	} else if (capability == NULL) {
		// Access outside of any capability
		err = ERROR_UNIMPLEMENTED;
	} else if (capability->type != PCI_CAPABILITY_TYPE_STANDARD) {
		// Only standard capabilities are supported
		// FIXME: QC Gunyah issue #291
		err = ERROR_UNIMPLEMENTED;
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_MSIX) {
		err = vpci_msix_config_write(
			function, vpci_msix_data_container_of_base(capability),
			offset, access_size, data);
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_PCIE) {
		err = vpci_pcie_config_write(
			vpci_pcie_data_container_of_base(capability), offset,
			access_size, data);
	} else if (capability->cap_id == (uint16_t)PCI_CAPABILITY_ID_PM) {
		// Ignore all writes
		err = OK;
	} else if (capability->cap_id != (uint16_t)PCI_CAPABILITY_ID_VENDOR) {
		// Unknown capability
		err = ERROR_UNIMPLEMENTED;
	} else {
		virtio_pci_cap_data_t *cap_data =
			virtio_pci_cap_data_container_of_base(capability);
		err = virtio_pci_cap_write(function, cap_data, offset,
					   access_size, data);
	}

	return err;
}

static register_result_t
virtio_pci_read_queue(virtio_pci_t *pci, size_t offset)
{
	virtio_t	 *virtio = virtio_from_virtio_pci(pci);
	register_result_t ret;

	virtio_status_lock(virtio);
	virtio_queue_info_result_t queue_info_r = virtio_get_queue_info(
		virtio, atomic_load_relaxed(&pci->queue_sel), false);
	if (queue_info_r.e != OK) {
		ret = register_result_ok(0U);
		goto out;
	}
	virtio_queue_info_t queue_info = queue_info_r.r;

	switch (offset) {
	case offsetof(virtio_pci_bar0_regs_t, queue_size):
		ret = register_result_ok(queue_info.size);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_enable):
		ret = register_result_ok(queue_info.ready ? 1U : 0U);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_low):
		ret = register_result_ok((uint32_t)queue_info.desc);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_high):
		ret = register_result_ok((uint32_t)(queue_info.desc >> 32U));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_low):
		ret = register_result_ok((uint32_t)queue_info.drv);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_high):
		ret = register_result_ok((uint32_t)(queue_info.drv >> 32U));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_low):
		ret = register_result_ok((uint32_t)queue_info.dev);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_high):
		ret = register_result_ok((uint32_t)(queue_info.dev >> 32U));
		break;
	default:
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

out:
	virtio_status_unlock(virtio);
	return ret;
}

static register_result_t
virtio_pci_handle_common_read(virtio_pci_t *pci, size_t offset) REQUIRE_RCU_READ
{
	virtio_t	 *virtio = virtio_from_virtio_pci(pci);
	register_result_t ret;

	switch (offset) {
	case offsetof(virtio_pci_bar0_regs_t, dev_feat_sel):
		ret = register_result_ok(
			atomic_load_relaxed(&pci->dev_feat_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, dev_feat): {
		uint32_result_t feat_r = virtio_get_dev_features(
			virtio, atomic_load_relaxed(&pci->dev_feat_sel));
		ret = register_result_ok((feat_r.e == OK) ? feat_r.r : 0U);
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, drv_feat_sel):
		ret = register_result_ok(
			atomic_load_relaxed(&pci->drv_feat_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, drv_feat): {
		virtio_status_lock(virtio);
		uint32_result_t feat_r = virtio_get_drv_features(
			virtio, atomic_load_relaxed(&pci->drv_feat_sel), false);
		virtio_status_unlock(virtio);
		ret = register_result_ok((feat_r.e == OK) ? feat_r.r : 0U);
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, config_msix_vector):
		ret = register_result_ok(
			atomic_load_relaxed(&pci->config_msix_vector));
		break;
	case offsetof(virtio_pci_bar0_regs_t, num_queues):
		ret = register_result_ok(virtio->vqs);
		break;
	case offsetof(virtio_pci_bar0_regs_t, status): {
		ret = register_result_ok(
			virtio_status_raw(virtio_read_status(virtio)));
		// Reacquire the RCU read lock, which was dropped by
		// virtio_read_status(), because the handler as a whole
		// currently can't drop the lock.
		// FIXME: QC Gunyah issue #252
		rcu_read_start();
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, config_gen): {
		uint8_result_t gen_r = virtio_get_generation(virtio);
		ret		     = (register_result_t){
					 .r = (register_t)gen_r.r,
					 .e = gen_r.e,
		};
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, queue_sel):
		ret = register_result_ok(atomic_load_relaxed(&pci->queue_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_msix_vector): {
		index_result_t queue_sel_r = nospec_range_check(
			atomic_load_relaxed(&pci->queue_sel), virtio->vqs);
		if (queue_sel_r.e == OK) {
			ret = register_result_ok(atomic_load_relaxed(
				&pci->queue_msix_vector[queue_sel_r.r]));
		} else {
			ret = register_result_ok(0U);
		}
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, queue_notify_offset):
		// All queues share one notify register
		ret = register_result_ok(0U);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_size):
	case offsetof(virtio_pci_bar0_regs_t, queue_enable):
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_high):
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_high):
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_high): {
		ret = virtio_pci_read_queue(pci, offset);
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, queue_notify):
		// Write-only register
		ret = register_result_ok(0U);
		break;
	case offsetof(virtio_pci_bar0_regs_t, interrupt_status):
		// Reading the interrupt status clears it.
		ret = register_result_ok(virtio_interrupt_raw(
			atomic_exchange_explicit(&pci->interrupt_status,
						 virtio_interrupt_default(),
						 memory_order_relaxed)));
		break;
	default:
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

	return ret;
}

register_result_t
virtio_pci_handle_vpci_bar_read(vpci_function_t *function, index_t bar_index,
				size_t offset, size_t access_size)
{
	register_result_t ret;

	assert(function != NULL);
	virtio_pci_t *pci = virtio_pci_container_of_fn0(function);

	virtio_pci_bar_result_t bar_r = virtio_pci_bar_cast_safe(bar_index);
	if (bar_r.e != OK) {
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	switch (bar_r.r) {
	case VIRTIO_PCI_BAR_COMMON:
		ret = virtio_pci_handle_common_read(pci, offset);
		break;
	case VIRTIO_PCI_BAR_DEVCFG: {
		virtio_t       *virtio = virtio_from_virtio_pci(pci);
		uint32_result_t config_r =
			virtio_device_config_read(virtio, offset, access_size);
		ret = (register_result_t){
			.r = (register_t)config_r.r,
			.e = config_r.e,
		};
		// Reacquire the RCU read lock, which was dropped by
		// virtio_device_config_read(), because the handler as a whole
		// currently can't drop the lock.
		// FIXME: QC Gunyah issue #252
		rcu_read_start();
		break;
	}
	case VIRTIO_PCI_BAR_MSIX_TABLE:
		ret = vpci_msix_table_read(&pci->msix_data, offset,
					   access_size);
		break;
	case VIRTIO_PCI_BAR_MSIX_PBA:
		ret = vpci_msix_pba_read(&pci->msix_data, offset, access_size);
		break;
	default:
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

out:
	return ret;
}

static size_result_t
virtio_pci_write_queue(virtio_pci_t *pci, size_t offset, uint32_t val)
	REQUIRE_RCU_READ
{
	virtio_t     *virtio = virtio_from_virtio_pci(pci);
	size_result_t ret;

	virtio_status_lock(virtio);

	count_t queue_sel = atomic_load_relaxed(&pci->queue_sel);
	virtio_queue_info_ptr_result_t queue_info_r =
		virtio_get_queue_info_ptr(virtio, queue_sel);
	if (queue_info_r.e != OK) {
		ret = size_result_ok(sizeof(uint32_t));
		goto out;
	}
	virtio_queue_info_t *queue_info = queue_info_r.r;

	switch (offset) {
	case offsetof(virtio_pci_bar0_regs_t, queue_size): {
		count_result_t max_r =
			virtio_get_queue_size_max(virtio, queue_sel);
		if (max_r.e != OK) {
			ret = size_result_error(max_r.e);
		} else {
			queue_info->size = util_min(val, max_r.r);
			ret		 = size_result_ok(util_sizeof_member(
				virtio_pci_bar0_regs_t, queue_size));
		}
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, queue_enable):
		if (val != 0U) {
			queue_info->ready = true;
		}
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_enable));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_low):
		queue_info->desc &= ~util_mask(32);
		queue_info->desc |= (paddr_t)val;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_desc_low));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_high):
		queue_info->desc &= util_mask(32);
		queue_info->desc |= (paddr_t)val << 32;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_desc_high));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_low):
		queue_info->drv &= ~util_mask(32);
		queue_info->drv |= (paddr_t)val;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_drv_low));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_high):
		queue_info->drv &= util_mask(32);
		queue_info->drv |= (paddr_t)val << 32;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_drv_high));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_low):
		queue_info->dev &= ~util_mask(32);
		queue_info->dev |= (paddr_t)val;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_dev_low));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_high):
		queue_info->dev &= util_mask(32);
		queue_info->dev |= (paddr_t)val << 32;
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_dev_high));
		break;
	default:
		ret = size_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

out:
	virtio_status_unlock(virtio);
	return ret;
}

static size_result_t
virtio_pci_handle_common_write_one(virtio_pci_t *pci, size_t offset,
				   register_t data) REQUIRE_RCU_READ
{
	virtio_t *virtio = virtio_from_virtio_pci(pci);

	size_result_t ret;
	error_t	      err;

	switch (offset) {
	case offsetof(virtio_pci_bar0_regs_t, dev_feat_sel):
		atomic_store_relaxed(&pci->dev_feat_sel, (uint32_t)data);
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							dev_feat_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, drv_feat_sel):
		atomic_store_relaxed(&pci->drv_feat_sel, (uint32_t)data);
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							dev_feat_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, drv_feat):
		err = virtio_set_drv_features(
			virtio, atomic_load_relaxed(&pci->drv_feat_sel),
			(uint32_t)data);
		if (err != OK) {
			ret = size_result_error(err);
		} else {
			ret = size_result_ok(util_sizeof_member(
				virtio_pci_bar0_regs_t, drv_feat));
		}
		break;
	case offsetof(virtio_pci_bar0_regs_t, config_msix_vector):
		atomic_store_relaxed(&pci->config_msix_vector, (uint16_t)data);
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							config_msix_vector));
		break;
	case offsetof(virtio_pci_bar0_regs_t, status):
		err = virtio_write_status(virtio,
					  virtio_status_cast((uint8_t)data));
		if (err != OK) {
			ret = size_result_error(err);
		} else {
			ret = size_result_ok(util_sizeof_member(
				virtio_pci_bar0_regs_t, status));
		}
#if defined(INTERFACE_VIRTIO_MMIO)
		// Workaround for legacy backends that assume an MMIO frontend:
		// if the config cache is present and has an offset that matches
		// the one used by an MMIO backend, write the current status
		// where the MMIO status register would be.
		if ((err == OK) &&
		    (virtio->config_range.size >= sizeof(virtio_mmio_regs_t)) &&
		    (virtio->config_offset ==
		     offsetof(virtio_mmio_regs_t, device_config))) {
			virtio_mmio_regs_t *regs =
				(virtio_mmio_regs_t *)virtio->config_range.base;
			atomic_store_relaxed(&regs->status,
					     virtio_read_status(virtio));
			rcu_read_start();
		}
#endif
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_sel):
		atomic_store_relaxed(&pci->queue_sel, (uint32_t)data);
		ret = size_result_ok(
			util_sizeof_member(virtio_pci_bar0_regs_t, queue_sel));
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_msix_vector): {
		index_result_t queue_sel_r = nospec_range_check(
			atomic_load_relaxed(&pci->queue_sel), virtio->vqs);
		if (queue_sel_r.e == OK) {
			atomic_store_relaxed(
				&pci->queue_msix_vector[queue_sel_r.r],
				(uint16_t)data);
		}
		ret = size_result_ok(util_sizeof_member(virtio_pci_bar0_regs_t,
							queue_msix_vector));
		break;
	}
	case offsetof(virtio_pci_bar0_regs_t, queue_size):
	case offsetof(virtio_pci_bar0_regs_t, queue_enable):
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_desc_high):
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_drv_high):
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_low):
	case offsetof(virtio_pci_bar0_regs_t, queue_dev_high):
		ret = virtio_pci_write_queue(pci, offset, (uint32_t)data);
		break;
	case offsetof(virtio_pci_bar0_regs_t, queue_notify):
		err = virtio_queue_notify(virtio, (uint32_t)data);
		if (err != OK) {
			ret = size_result_error(err);
		} else {
			ret = size_result_ok(util_sizeof_member(
				virtio_pci_bar0_regs_t, queue_notify));
		}
		break;
	case offsetof(virtio_pci_bar0_regs_t, num_queues):
	case offsetof(virtio_pci_bar0_regs_t, dev_feat):
	case offsetof(virtio_pci_bar0_regs_t, config_gen):
	case offsetof(virtio_pci_bar0_regs_t, queue_notify_offset):
	case offsetof(virtio_pci_bar0_regs_t, interrupt_status):
		// Read-only register
		ret = size_result_error(ERROR_DENIED);
		break;
	default:
		// No such register
		ret = size_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

	return ret;
}

static error_t
virtio_pci_handle_common_write(virtio_pci_t *pci, size_t offset,
			       size_t access_size, register_t data)
	REQUIRE_RCU_READ
{
	size_t	written_size = 0U;
	error_t ret;

	while (written_size < access_size) {
		register_t write_val  = data >> (8U * written_size);
		register_t write_size = access_size - written_size;

		size_result_t write_r = virtio_pci_handle_common_write_one(
			pci, offset + written_size, write_val);

		if (write_r.e != OK) {
			ret = write_r.e;
			goto out;
		}

		written_size += util_min(write_r.r, write_size);
	}

	ret = OK;
out:
	return ret;
}

error_t
virtio_pci_handle_vpci_bar_write(vpci_function_t *function, index_t bar_index,
				 size_t offset, size_t access_size,
				 register_t data)
{
	error_t ret;

	assert(function != NULL);
	virtio_pci_t *pci    = virtio_pci_container_of_fn0(function);
	virtio_t     *virtio = virtio_from_virtio_pci(pci);

	virtio_pci_bar_result_t bar_r = virtio_pci_bar_cast_safe(bar_index);
	if (bar_r.e != OK) {
		ret = ERROR_UNIMPLEMENTED;
		goto out;
	}

	switch (bar_r.r) {
	case VIRTIO_PCI_BAR_COMMON:
		ret = virtio_pci_handle_common_write(pci, offset, access_size,
						     data);
		break;
	case VIRTIO_PCI_BAR_DEVCFG:
		ret = virtio_device_config_write(virtio, offset, access_size,
						 (uint32_t)data);
		// Reacquire the RCU read lock, which was dropped by
		// virtio_device_config_write(), because the handler as a whole
		// currently can't drop the lock.
		// FIXME: QC Gunyah issue #252
		rcu_read_start();
		break;
	case VIRTIO_PCI_BAR_MSIX_PBA:
		// Read only; ignore writes
		ret = OK;
		break;
	case VIRTIO_PCI_BAR_MSIX_TABLE:
		ret = vpci_msix_table_write(&pci->fn0, &pci->msix_data, offset,
					    access_size, data);
		break;
	default:
		ret = ERROR_UNIMPLEMENTED;
		break;
	}

out:
	return ret;
}
