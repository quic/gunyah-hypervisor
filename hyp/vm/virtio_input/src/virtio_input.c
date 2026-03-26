// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <partition.h>
#include <spinlock.h>
#include <thread.h>
#include <util.h>
#include <virq.h>

#include <events/virtio_backend.h>

#include <asm/nospec_checks.h>

#include "event_handlers.h"
#include "useraccess.h"
#include "virtio_input.h"

error_t
virtio_input_handle_object_activate(virtio_backend_t *virtio_backend)
{
	error_t ret;

	partition_t *partition = virtio_backend->header.partition;
	// Allocate memory for virtio input data struct if device type is
	// virtio-input
	if (virtio_backend->virtio.device_type == VIRTIO_DEVICE_TYPE_INPUT) {
		size_t		  alloc_size = sizeof(virtio_input_data_t);
		void_ptr_result_t alloc_ret  = partition_alloc(
			partition, alloc_size, alignof(virtio_input_data_t));
		if (alloc_ret.e != OK) {
			ret = ERROR_NOMEM;
			goto out;
		}

		(void)memset_s(alloc_ret.r, alloc_size, 0, alloc_size);

		virtio_backend->input_data = (virtio_input_data_t *)alloc_ret.r;
	}

	ret = OK;
out:
	return ret;
}

error_t
virtio_input_handle_object_cleanup(virtio_backend_t *virtio_backend)
{
	if (virtio_backend->virtio.device_type != VIRTIO_DEVICE_TYPE_INPUT) {
		assert(virtio_backend->input_data == NULL);
		goto out;
	}
	if (virtio_backend->input_data == NULL) {
		goto out;
	}

	partition_t *partition = virtio_backend->header.partition;
	size_t	     alloc_size;
	void	    *alloc_base;
	// first free memory for absinfo and evtypes if any
	count_t absinfo_count =
		atomic_load_relaxed(&virtio_backend->input_data->absinfo_count);
	if (absinfo_count != 0U) {
		alloc_size = absinfo_count * sizeof(virtio_input_absinfo_t);
		alloc_base = (void *)virtio_backend->input_data->absinfo;

		partition_free(partition, alloc_base, alloc_size);

		virtio_backend->input_data->absinfo = NULL;
		atomic_store_relaxed(&virtio_backend->input_data->absinfo_count,
				     0U);
	}

	count_t ev_bits_count =
		atomic_load_relaxed(&virtio_backend->input_data->ev_bits_count);
	if (ev_bits_count != 0U) {
		alloc_size = ev_bits_count * sizeof(virtio_input_ev_bits_t);
		alloc_base = (void *)virtio_backend->input_data->ev_bits;

		partition_free(partition, alloc_base, alloc_size);

		virtio_backend->input_data->ev_bits = NULL;
		atomic_store_relaxed(&virtio_backend->input_data->ev_bits_count,
				     0U);
	}

	// now safely free the virtio_input struct
	alloc_size = sizeof(*virtio_backend->input_data);
	alloc_base = (void *)virtio_backend->input_data;

	partition_free(partition, alloc_base, alloc_size);

	virtio_backend->input_data = NULL;

out:
	return OK;
}

error_t
virtio_input_set_data_sel_abs_info(count_t		   absinfo_count,
				   virtio_input_absinfo_t *absinfo,
				   uint32_t subsel, uint32_t size,
				   vmaddr_t data)
{
	error_t ret;

	assert(absinfo != NULL);

	if (subsel < VIRTIO_INPUT_MAX_ABS_AXES) {
		// find free entry
		uint32_t entry;
		for (entry = 0; entry < absinfo_count; entry++) {
			if (absinfo[entry].subsel ==
			    VIRTIO_INPUT_SUBSEL_INVALID) {
				// got the free entry
				break;
			} else {
				continue;
			}
		}

		if (entry == absinfo_count) {
			// no free entry
			ret = ERROR_NORESOURCES;
		} else {
			// copy data from guest va; size is checked by this API
			ret = useraccess_copy_from_guest_va(
				      &(absinfo[entry].data),
				      VIRTIO_INPUT_MAX_ABSINFO_SIZE, data, size)
				      .e;
			if (ret == OK) {
				// successful copy, update subsel
				absinfo[entry].subsel = (uint8_t)subsel;
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
virtio_input_set_data_sel_ev_bits(count_t		  ev_bits_count,
				  virtio_input_ev_bits_t *ev_bits,
				  uint32_t subsel, uint32_t size, vmaddr_t data)
{
	error_t ret;

	assert(ev_bits != NULL);

	if (subsel < VIRTIO_INPUT_MAX_EV_TYPES) {
		// find free entry
		uint32_t entry;
		for (entry = 0; entry < ev_bits_count; entry++) {
			if (ev_bits[entry].subsel ==
			    VIRTIO_INPUT_SUBSEL_INVALID) {
				// got the free entry
				break;
			} else {
				continue;
			}
		}

		if (entry == ev_bits_count) {
			// no free entry
			ret = ERROR_NORESOURCES;
		} else if (size != 0U) {
			// copy data from guest va; size is checked by this API
			ret = useraccess_copy_from_guest_va(
				      &(ev_bits[entry].data),
				      VIRTIO_INPUT_MAX_BITMAP_SIZE, data, size)
				      .e;
			if (ret == OK) {
				// successful copy, update the size info and
				// subsel
				ev_bits[entry].size   = (uint8_t)size;
				ev_bits[entry].subsel = (uint8_t)subsel;
			} else {
				// ignore
			}
		} else {
			// size 0 is allowed
			ev_bits[entry].size   = 0U;
			ev_bits[entry].subsel = (uint8_t)subsel;
			ret		      = OK;
		}
	} else {
		ret = ERROR_ARGUMENT_INVALID;
	}
	return ret;
}

static void
virtio_input_sel_cfg_abs_info_write(const virtio_backend_t *virtio_backend,
				    uint8_t		    subsel)
{
	if (subsel < VIRTIO_INPUT_MAX_ABS_AXES) {
		// find the ev entry where this subsel entry is stored
		uint32_t entry;
		count_t	 absinfo_count = atomic_load_acquire(
			&virtio_backend->input_data->absinfo_count);

		for (entry = 0U; entry < absinfo_count; entry++) {
			if (virtio_backend->input_data->absinfo[entry].subsel ==
			    subsel) {
				// found the entry
				break;
			} else {
				continue;
			}
		}
		if (entry == absinfo_count) {
			// entry not found, invalid subsel set size 0
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			// valid subsel
			uint8_t size = (uint8_t)VIRTIO_INPUT_MAX_ABSINFO_SIZE;

			(void)memscpy(
				virtio_backend->virtio.config_cache
					->input_config.u.abs,
				size,
				virtio_backend->input_data->absinfo[entry].data,
				sizeof(virtio_backend->input_data
					       ->absinfo[entry]
					       .data));

			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				size);
		}
	} else {
		// invalid subsel set size 0
		atomic_store_relaxed(
			&virtio_backend->virtio.config_cache->input_config.size,
			0U);
	}
}

static void
virtio_input_sel_cfg_ev_bits_write(const virtio_backend_t *virtio_backend,
				   uint8_t		   subsel)
{
	if (subsel < VIRTIO_INPUT_MAX_EV_TYPES) {
		// find the ev entry where this subsel entry is stored
		uint32_t entry;
		count_t	 ev_bits_count = atomic_load_acquire(
			&virtio_backend->input_data->ev_bits_count);

		for (entry = 0; entry < ev_bits_count; entry++) {
			if (virtio_backend->input_data->ev_bits[entry].subsel ==
			    subsel) {
				// found the entry
				break;
			} else {
				continue;
			}
		}
		if (entry == ev_bits_count) {
			// entry not found, invalid subsel set size 0
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			// valid subsel
			uint8_t size =
				virtio_backend->input_data->ev_bits[entry].size;

			(void)memscpy(
				virtio_backend->virtio.config_cache
					->input_config.u.bitmap,
				size,
				virtio_backend->input_data->ev_bits[entry].data,
				sizeof(virtio_backend->input_data
					       ->ev_bits[entry]
					       .data));

			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				size);
		}
	} else {
		// invalid subsel set size 0
		atomic_store_relaxed(
			&virtio_backend->virtio.config_cache->input_config.size,
			0U);
	}
}

static void
virtio_input_config_u_write(const virtio_backend_t *virtio_backend, uint8_t sel,
			    uint8_t subsel)
{
	switch ((virtio_input_config_select_t)sel) {
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_NAME: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			size_t size = virtio_backend->input_data->name_size;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_backend->virtio.config_cache
						 ->input_config.u.string[i],
					virtio_backend->input_data->name[i]);
			}
			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				(uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_SERIAL: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			size_t size = virtio_backend->input_data->serial_size;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_backend->virtio.config_cache
						 ->input_config.u.string[i],
					virtio_backend->input_data->serial[i]);
			}
			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				(uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ID_DEVIDS: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			size_t size =
				sizeof(virtio_backend->input_data->devids);
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.u.ids.bustype,
				(uint16_t)virtio_backend->input_data->devids);
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.u.ids.vendor,
				(uint16_t)(virtio_backend->input_data->devids >>
					   16U));
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.u.ids.product,
				(uint16_t)(virtio_backend->input_data->devids >>
					   32U));
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.u.ids.version,
				(uint16_t)(virtio_backend->input_data->devids >>
					   48U));
			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				(uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_PROP_BITS: {
		if (subsel != 0U) { // only subsel 0 is valid
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				0U);
		} else {
			size_t size =
				sizeof(virtio_backend->input_data->prop_bits);
			uint8_t *prop_bits_addr =
				(uint8_t *)&virtio_backend->input_data
					->prop_bits;
			for (index_t i = 0U; i < size; i++) {
				atomic_store_relaxed(
					&virtio_backend->virtio.config_cache
						 ->input_config.u.bitmap[i],
					*prop_bits_addr);
				prop_bits_addr++;
			}
			// update the size
			atomic_store_relaxed(
				&virtio_backend->virtio.config_cache
					 ->input_config.size,
				(uint8_t)size);
		}
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_EV_BITS: {
		virtio_input_sel_cfg_ev_bits_write(virtio_backend, subsel);
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_ABS_INFO: {
		virtio_input_sel_cfg_abs_info_write(virtio_backend, subsel);
		break;
	}
	case VIRTIO_INPUT_CONFIG_SELECT_CFG_UNSET:
	default:
		// No data; set size to 0
		atomic_store_relaxed(
			&virtio_backend->virtio.config_cache->input_config.size,
			0U);
		break;
	}
}

error_t
virtio_input_config_write(const virtio_backend_t *virtio_backend, size_t offset,
			  register_t value, size_t access_size)
{
	error_t ret;

	if ((access_size == 0U) || (access_size > 2U)) {
		ret = ERROR_ARGUMENT_SIZE;
		goto out;
	}

	size_t	   access_size_remaining = access_size;
	size_t	   next_offset		 = offset;
	register_t next_value		 = value;

	if (next_offset ==
	    offsetof(virtio_config_space_t, input_config.select)) {
		atomic_store_relaxed(&virtio_backend->virtio.config_cache
					      ->input_config.select,
				     (uint8_t)next_value);

		uint8_t subsel = atomic_load_relaxed(
			&virtio_backend->virtio.config_cache->input_config
				 .subsel);

		// write the appropriate data in u regs
		virtio_input_config_u_write(virtio_backend, (uint8_t)next_value,
					    subsel);

		// handle 2-byte accesses to both fields
		access_size_remaining -= 1U;
		next_offset += 1U;
		next_value >>= 8;
	}

	if ((access_size_remaining == 1U) &&
	    (next_offset ==
	     offsetof(virtio_config_space_t, input_config.subsel))) {
		atomic_store_relaxed(&virtio_backend->virtio.config_cache
					      ->input_config.subsel,
				     (uint8_t)next_value);

		uint8_t sel = atomic_load_relaxed(
			&virtio_backend->virtio.config_cache->input_config
				 .select);

		// write the appropriate data in u regs
		virtio_input_config_u_write(virtio_backend, sel,
					    (uint8_t)next_value);

		access_size_remaining = 0U;
	}

	ret = (access_size_remaining == 0U) ? OK : ERROR_ARGUMENT_INVALID;

out:
	return ret;
}
