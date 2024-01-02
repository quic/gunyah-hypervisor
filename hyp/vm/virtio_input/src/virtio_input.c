// © 2023 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypconstants.h>
#include <hypcontainers.h>

#include <atomic.h>
#include <partition.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <virq.h>

#include <events/virtio_mmio.h>

#include <asm/nospec_checks.h>

#include "event_handlers.h"
#include "useraccess.h"
#include "virtio_input.h"

error_t
virtio_input_handle_object_activate(virtio_mmio_t *virtio_mmio)
{
	error_t ret;

	partition_t *partition = virtio_mmio->header.partition;
	// Allocate memory for virtio input data struct if device type
	// virtio-input
	if (virtio_mmio->device_type == VIRTIO_DEVICE_TYPE_INPUT) {
		size_t		  alloc_size = sizeof(virtio_input_data_t);
		void_ptr_result_t alloc_ret  = partition_alloc(
			 partition, alloc_size, alignof(virtio_input_data_t));
		if (alloc_ret.e != OK) {
			ret = ERROR_NOMEM;
			goto out;
		}

		(void)memset_s(alloc_ret.r, alloc_size, 0, alloc_size);

		virtio_mmio->input_data = (virtio_input_data_t *)alloc_ret.r;
	}

	ret = OK;
out:
	return ret;
}

error_t
virtio_input_handle_object_cleanup(virtio_mmio_t *virtio_mmio)
{
	if (virtio_mmio->input_data != NULL) {
		partition_t *partition = virtio_mmio->header.partition;
		size_t	     alloc_size;
		void	    *alloc_base;
		/* first free memory for absinfo and evtypes if any */
		if (virtio_mmio->input_data->absinfo_count != 0U) {
			alloc_size = virtio_mmio->input_data->absinfo_count *
				     sizeof(virtio_input_absinfo_t);
			alloc_base = (void *)virtio_mmio->input_data->absinfo;

			error_t err = partition_free(partition, alloc_base,
						     alloc_size);
			assert(err == OK);

			virtio_mmio->input_data->absinfo       = NULL;
			virtio_mmio->input_data->absinfo_count = 0U;
		}

		if (virtio_mmio->input_data->ev_bits_count != 0U) {
			alloc_size = virtio_mmio->input_data->ev_bits_count *
				     sizeof(virtio_input_ev_bits_t);
			alloc_base = (void *)virtio_mmio->input_data->ev_bits;

			error_t err = partition_free(partition, alloc_base,
						     alloc_size);
			assert(err == OK);

			virtio_mmio->input_data->ev_bits       = NULL;
			virtio_mmio->input_data->ev_bits_count = 0U;
		}

		/* now safely free the virtio_input struct */
		alloc_size = sizeof(virtio_mmio->input_data);
		alloc_base = (void *)&virtio_mmio->input_data;

		error_t err = partition_free(partition, alloc_base, alloc_size);
		assert(err == OK);

		virtio_mmio->input_data = NULL;
	} else {
		// ignore
	}

	return OK;
}

error_t
set_data_sel_abs_info(const virtio_mmio_t *virtio_mmio, uint32_t subsel,
		      uint32_t size, vmaddr_t data)
{
	error_t ret;
	if (subsel < VIRTIO_INPUT_MAX_ABS_AXES) {
		// find free entry
		uint32_t entry;
		for (entry = 0; entry < virtio_mmio->input_data->absinfo_count;
		     entry++) {
			if (virtio_mmio->input_data->absinfo[entry].subsel ==
			    VIRTIO_INPUT_SUBSEL_INVALID) {
				// got the free entry
				break;
			} else {
				continue;
			}
		}

		if (entry == virtio_mmio->input_data->absinfo_count) {
			// no free entry
			ret = ERROR_NORESOURCES;
		} else {
			// copy data from guest va; size is checked by this API
			ret = useraccess_copy_from_guest_va(
				      &(virtio_mmio->input_data->absinfo[entry]
						.data),
				      VIRTIO_INPUT_MAX_ABSINFO_SIZE, data, size)
				      .e;
			if (ret == OK) {
				// successful copy, update subsel
				virtio_mmio->input_data->absinfo[entry].subsel =
					(uint8_t)subsel;
			} else {
				// ignore
			}
		}
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}
	return ret;
}

error_t
set_data_sel_ev_bits(const virtio_mmio_t *virtio_mmio, uint32_t subsel,
		     uint32_t size, vmaddr_t data)
{
	error_t ret;
	if (subsel < VIRTIO_INPUT_MAX_EV_TYPES) {
		// find free entry
		uint32_t entry;
		for (entry = 0; entry < virtio_mmio->input_data->ev_bits_count;
		     entry++) {
			if (virtio_mmio->input_data->ev_bits[entry].subsel ==
			    VIRTIO_INPUT_SUBSEL_INVALID) {
				// got the free entry
				break;
			} else {
				continue;
			}
		}

		if (entry == virtio_mmio->input_data->ev_bits_count) {
			// no free entry
			ret = ERROR_NORESOURCES;
		} else {
			// copy data from guest va; size is checked by this API
			ret = useraccess_copy_from_guest_va(
				      &(virtio_mmio->input_data->ev_bits[entry]
						.data),
				      VIRTIO_INPUT_MAX_BITMAP_SIZE, data, size)
				      .e;
			if (ret == OK) {
				/*successful copy, update the size info
				 * and subsel*/
				virtio_mmio->input_data->ev_bits[entry].size =
					(uint8_t)size;
				virtio_mmio->input_data->ev_bits[entry].subsel =
					(uint8_t)subsel;
			} else {
				// ignore
			}
		}
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}
	return ret;
}

static void
sel_cfg_abs_info_write(const virtio_mmio_t *virtio_mmio, uint8_t subsel)
{
	if (subsel < VIRTIO_INPUT_MAX_ABS_AXES) {
		// find the ev entry where this subsel entry is stored
		uint32_t entry;
		for (entry = 0; entry < virtio_mmio->input_data->absinfo_count;
		     entry++) {
			if (virtio_mmio->input_data->absinfo[entry].subsel ==
			    subsel) {
				// found the entry
				break;
			} else {
				continue;
			}
		}
		if (entry == virtio_mmio->input_data->absinfo_count) {
			// entry not found, invalid subsel set size 0
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			// valid subsel
			uint8_t size = (uint8_t)VIRTIO_INPUT_MAX_ABSINFO_SIZE;

			(void)memcpy(
				virtio_mmio->regs->device_config.input_config.u
					.abs,
				virtio_mmio->input_data->absinfo[entry].data,
				size);

			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     size);
		}
	} else {
		// invalid subsel set size 0
		atomic_store_relaxed(
			&virtio_mmio->regs->device_config.input_config.size,
			0U);
	}
}

static void
sel_cfg_ev_bits_write(const virtio_mmio_t *virtio_mmio, uint8_t subsel)
{
	if (subsel < VIRTIO_INPUT_MAX_EV_TYPES) {
		// find the ev entry where this subsel entry is stored
		uint32_t entry;
		for (entry = 0; entry < virtio_mmio->input_data->ev_bits_count;
		     entry++) {
			if (virtio_mmio->input_data->ev_bits[entry].subsel ==
			    subsel) {
				// found the entry
				break;
			} else {
				continue;
			}
		}
		if (entry == virtio_mmio->input_data->ev_bits_count) {
			// entry not found, invalid subsel set size 0
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			// valid subsel
			uint8_t size =
				virtio_mmio->input_data->ev_bits[entry].size;

			(void)memcpy(
				virtio_mmio->regs->device_config.input_config.u
					.bitmap,
				virtio_mmio->input_data->ev_bits[entry].data,
				size);

			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     size);
		}
	} else {
		// invalid subsel set size 0
		atomic_store_relaxed(
			&virtio_mmio->regs->device_config.input_config.size,
			0U);
	}
}

static void
virtio_input_config_u_write(const virtio_mmio_t *virtio_mmio, uint8_t sel,
			    uint8_t subsel)
{
	switch ((virtio_input_config_select_t)sel) {
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_NAME: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			size_t size = virtio_mmio->input_data->name_size;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.u.string[i],
					virtio_mmio->input_data->name[i]);
			}
			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     (uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_SERIAL: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			size_t size = virtio_mmio->input_data->serial_size;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.u.string[i],
					virtio_mmio->input_data->serial[i]);
			}
			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     (uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_DEVIDS: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			size_t size = sizeof(virtio_mmio->input_data->devids);
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.u.ids,
					     virtio_mmio->input_data->devids);
			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     (uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_PROP_BITS: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     0U);
		} else {
			size_t size =
				sizeof(virtio_mmio->input_data->prop_bits);
			uint8_t *prop_bits_addr =
				(uint8_t *)&virtio_mmio->input_data->prop_bits;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.u.bitmap[i],
					*prop_bits_addr);
				prop_bits_addr++;
			}
			// update the size
			atomic_store_relaxed(&virtio_mmio->regs->device_config
						      .input_config.size,
					     (uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_EV_BITS: {
		sel_cfg_ev_bits_write(virtio_mmio, subsel);
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ABS_INFO: {
		sel_cfg_abs_info_write(virtio_mmio, subsel);
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_UNSET:
	default:
		// No data; set size to 0
		atomic_store_relaxed(
			&virtio_mmio->regs->device_config.input_config.size,
			0U);
		break;
	}
}

vcpu_trap_result_t
virtio_input_config_write(const virtio_mmio_t *virtio_mmio, size_t write_offset,
			  register_t reg_val, size_t access_size)
{
	vcpu_trap_result_t ret;
	register_t	   val = reg_val;
	size_t		   offset;
	size_t		   access_size_remaining = access_size;

	if (write_offset >= (size_t)OFS_VIRTIO_MMIO_REGS_DEVICE_CONFIG) {
		ret    = VCPU_TRAP_RESULT_FAULT;
		offset = write_offset -
			 (size_t)OFS_VIRTIO_MMIO_REGS_DEVICE_CONFIG;
		while (access_size_remaining != 0U) {
			switch (offset) {
			case OFS_VIRTIO_INPUT_CONFIG_SELECT: {
				atomic_store_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.select,
					(uint8_t)val);

				uint8_t subsel = atomic_load_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.subsel);

				// write the appropriate data in u regs
				virtio_input_config_u_write(
					virtio_mmio, (uint8_t)val, subsel);
				// update remianing size
				access_size_remaining =
					access_size_remaining - 1U;
				offset += 1U; // update offset
				val >>= 8;    // update the value
				ret = VCPU_TRAP_RESULT_EMULATED;
				break;
			}
			case OFS_VIRTIO_INPUT_CONFIG_SUBSEL: {
				atomic_store_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.subsel,
					(uint8_t)val);

				uint8_t sel = atomic_load_relaxed(
					&virtio_mmio->regs->device_config
						 .input_config.select);

				// write the appropriate data in u regs
				virtio_input_config_u_write(virtio_mmio, sel,
							    (uint8_t)val);
				// update remianing size
				access_size_remaining =
					access_size_remaining - 1U;
				offset += 1U; // update offset
				val >>= 8;    // update the value
				ret = VCPU_TRAP_RESULT_EMULATED;
				break;
			}
			default:
				(void)access_size;
				// we will not handle offset after subsel
				access_size_remaining = 0U;
				ret		      = VCPU_TRAP_RESULT_FAULT;
				break;
			}
		}
	} else {
		ret = VCPU_TRAP_RESULT_FAULT;
	}

	return ret;
}
