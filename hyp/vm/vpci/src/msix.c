// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <atomic.h>
#include <compiler.h>
#include <panic.h>
#include <partition.h>
#include <util.h>
#include <vpci.h>

error_t
vpci_msix_init(partition_t *owner, vpci_msix_data_t *msix_data)
{
	assert(msix_data->base.cap_id == (uint8_t)PCI_CAPABILITY_ID_MSIX);
	assert(msix_data->base.size == sizeof(pci_msix_capability_t));
	assert(msix_data->base.offset >= 0x40U);
	assert((msix_data->vector_count >= 1U) &&
	       (msix_data->vector_count <= util_bit(11U)));

	size_t alloc_size =
		sizeof(*msix_data->vectors) * msix_data->vector_count;
	void_ptr_result_t alloc_r = partition_alloc(
		owner, alloc_size, alignof(*msix_data->vectors));
	if (alloc_r.e == OK) {
		(void)memset_s(alloc_r.r, alloc_size, 0, alloc_size);
		msix_data->vectors = alloc_r.r;

		pci_msix_message_control_t control =
			pci_msix_message_control_default();
		// The number of table entries is the <value read> + 1
		pci_msix_message_control_set_table_size(
			&control, msix_data->vector_count - 1U);
		atomic_store_release(&msix_data->control, control);
	}

	return alloc_r.e;
}

error_t
vpci_msix_free(partition_t *owner, vpci_msix_data_t *msix_data)
{
	if (msix_data->vectors != NULL) {
		size_t alloc_size =
			sizeof(*msix_data->vectors) * msix_data->vector_count;
		partition_free(owner, msix_data->vectors, alloc_size);
		msix_data->vectors = NULL;
	}

	return OK;
}

static register_t
vpci_msix_config_read_one(vpci_msix_data_t *msix_data, size_t offset,
			  size_t *access_size)
{
	register_t val;

	switch (offset) {
	case util_offset_case_range(pci_msix_capability_t, msg_control):
		val = pci_msix_message_control_raw(
			atomic_load_relaxed(&msix_data->control));
		*access_size =
			util_sizeof_member(pci_msix_capability_t, msg_control);
		break;
	case util_offset_case_range(pci_msix_capability_t, table): {
		pci_msix_offset_t table = pci_msix_offset_default();
		pci_msix_offset_set_offset(&table, msix_data->table_offset);
		pci_msix_offset_set_bir(&table, msix_data->table_bar_index);
		val	     = pci_msix_offset_raw(table);
		*access_size = util_sizeof_member(pci_msix_capability_t, table);
		break;
	}
	case util_offset_case_range(pci_msix_capability_t, pba): {
		pci_msix_offset_t pba = pci_msix_offset_default();
		pci_msix_offset_set_offset(&pba, msix_data->pba_offset);
		pci_msix_offset_set_bir(&pba, msix_data->pba_bar_index);
		val	     = pci_msix_offset_raw(pba);
		*access_size = util_sizeof_member(pci_msix_capability_t, pba);
		break;
	}
	default:
		// Invalid offset
		val	     = 0U;
		*access_size = 1U;
		break;
	}

	return val;
}

register_result_t
vpci_msix_config_read(vpci_msix_data_t *msix_data, size_t offset,
		      size_t access_size)
{
	register_t read_val  = 0U;
	size_t	   read_size = 0U;

	assert(access_size <= sizeof(register_t));

	while (read_size < access_size) {
		size_t read_offset = offset + read_size;
		size_t accessed_size;

		size_t accessed_val = vpci_msix_config_read_one(
			msix_data, read_offset, &accessed_size);

		// Handle partial accesses (note all fields are size-aligned)
		size_t mask = accessed_size - 1U;
		if ((read_offset & mask) != 0U) {
			accessed_val >>=
				util_width(uint8_t) * (read_offset & mask);
			accessed_size = (0U - read_offset) & mask;
		}

		read_val |= accessed_val << (util_width(uint8_t) * read_size);
		read_size += accessed_size;
	}

	return register_result_ok(read_val);
}

static bool
vpci_msix_enabled(pci_msix_message_control_t control)
{
	return pci_msix_message_control_get_msix_enable(&control) &&
	       !pci_msix_message_control_get_function_mask(&control);
}

bool
vpci_msix_is_enabled(vpci_msix_data_t *msix_data)
{
	return vpci_msix_enabled(atomic_load_relaxed(&msix_data->control));
}

static void
vpci_msix_check_all(vpci_function_t *function, vpci_msix_data_t *msix_data)
{
	for (index_t i = 0U; i < msix_data->vector_count; i++) {
		vpci_msix_vector_t *vector = &msix_data->vectors[i];
		vpci_msix_status_t  status =
			atomic_load_acquire(&vector->status);
		if (!vpci_msix_status_get_pending(&status) ||
		    vpci_msix_status_get_mask(&status)) {
			continue;
		}
		if (atomic_compare_exchange_strong_explicit(
			    &vector->status, &status,
			    vpci_msix_status_default(), memory_order_acquire,
			    memory_order_acquire)) {
			vpci_dispatch_msi(function, vector->addr, vector->data);
		}
	}
}

error_t
vpci_msix_config_write(vpci_function_t *function, vpci_msix_data_t *msix_data,
		       size_t offset, size_t access_size, register_t data)
{
	// Only the msg_control field is writable. Note that this is the first
	// field in the capability after the header, so we'll never get a
	// partial write that starts before the field.
	if (!util_offset_in_range(offset, pci_msix_capability_t, msg_control)) {
		goto out;
	}

	size_t field_offset =
		offset - offsetof(pci_msix_capability_t, msg_control);
	// Only the second byte contains writable fields.
	if ((field_offset == 0U) && (access_size == 1U)) {
		goto out;
	}

	pci_msix_message_control_t new_control =
		pci_msix_message_control_cast((uint16_t)(data << field_offset));
	// Ensure the RO table size field has the right value
	pci_msix_message_control_set_table_size(&new_control,
						msix_data->vector_count - 1U);

	bool			   is_enabled  = vpci_msix_enabled(new_control);
	pci_msix_message_control_t old_control = atomic_exchange_explicit(
		&msix_data->control, new_control, memory_order_relaxed);
	bool was_enabled = vpci_msix_enabled(old_control);
	if (is_enabled && !was_enabled) {
		// This fence matches the fence after clearing a
		// vector's mask bit in vpci_msix_update_vector_mask
		// and the fence after setting a vector's pending bit in
		// vpci_msix_send, both of which are conditional on
		// MSI-X being disabled or masked at function level.
		//
		// The fences guarantee that if a vector enters pending
		// & unmasked state at the same time that this function
		// enables MSI-X delivery, we will deliver the MSI
		// either in vpci_msix_check_all() below, or in the
		// function operating on the vector.
		atomic_thread_fence(memory_order_seq_cst);

		vpci_msix_check_all(function, msix_data);
	}

out:
	return OK;
}

static register_t
vpci_msix_vector_read(vpci_msix_vector_t *vector, size_t offset,
		      size_t access_size)
{
	size_t	   read_size = 0U;
	register_t read_data = 0U;

	while (read_size < access_size) {
		size_t	   accessed_size;
		register_t accessed_data;

		switch (offset) {
		case offsetof(pci_msix_vector_t, addr):
			accessed_data = vector->addr;
			accessed_size = sizeof(vector->addr);
			break;
		case offsetof(pci_msix_vector_t, addr) + 4U:
			accessed_data = vector->addr >> 32U;
			accessed_size = sizeof(vector->addr) - 4U;
			break;
		case offsetof(pci_msix_vector_t, data):
			accessed_data = vector->data;
			accessed_size = sizeof(vector->data);
			break;
		case offsetof(pci_msix_vector_t, control): {
			pci_msix_vector_control_t control =
				pci_msix_vector_control_default();
			vpci_msix_status_t status =
				atomic_load_relaxed(&vector->status);
			pci_msix_vector_control_set_mask(
				&control, vpci_msix_status_get_mask(&status));
			accessed_data = pci_msix_vector_control_raw(control);
			accessed_size = sizeof(control);
			break;
		}
		default:
			accessed_data = 0U;
			accessed_size = 4U;
			break;
		}

		read_data |= accessed_data << (read_size * util_width(uint8_t));
		read_size += accessed_size;
	}

	return read_data;
}

register_result_t
vpci_msix_table_read(vpci_msix_data_t *msix_data, size_t offset,
		     size_t access_size)
{
	register_result_t ret;

	// Only DWORD and QWORD accesses need to be supported.
	if ((access_size != 4U) && (access_size != 8U)) {
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	// Only aligned accesses need to be supported.
	if ((offset % access_size) != 0U) {
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	index_t vector_index = (index_t)(offset / sizeof(pci_msix_vector_t));
	if (vector_index >= msix_data->vector_count) {
		ret = register_result_error(ERROR_UNIMPLEMENTED);
		goto out;
	}

	ret = register_result_ok(vpci_msix_vector_read(
		&msix_data->vectors[vector_index],
		offset % sizeof(pci_msix_vector_t), access_size));

out:
	return ret;
}

static void
vpci_msix_update_vector_mask(vpci_function_t	*function,
			     vpci_msix_data_t	*msix_data,
			     vpci_msix_vector_t *vector, bool mask)
{
	vpci_msix_status_t mask_bit = vpci_msix_status_default();
	vpci_msix_status_set_mask(&mask_bit, true);

	if (mask) {
		// No need to worry about delivering the MSI; just set the mask.
		(void)vpci_msix_status_atomic_union(&vector->status, mask_bit,
						    memory_order_relaxed);
	} else {
		// Clear the vector's mask bit before checking the function
		// level enables. See comment in vpci_msix_config_write.
		vpci_msix_status_t status = vpci_msix_status_atomic_difference(
			&vector->status, mask_bit, memory_order_relaxed);
		atomic_thread_fence(memory_order_seq_cst);
		pci_msix_message_control_t fn_control =
			atomic_load_relaxed(&msix_data->control);

		if (vpci_msix_enabled(fn_control) &&
		    vpci_msix_status_get_pending(&status) &&
		    atomic_compare_exchange_strong_explicit(
			    &vector->status, &status,
			    vpci_msix_status_default(), memory_order_acquire,
			    memory_order_acquire)) {
			vpci_dispatch_msi(function, vector->addr, vector->data);
		}
	}
}

static void
vpci_msix_vector_write(vpci_function_t *function, vpci_msix_data_t *msix_data,
		       vpci_msix_vector_t *vector, size_t offset,
		       size_t access_size, register_t data)
{
	size_t written_size = 0U;

	while (written_size < access_size) {
		register_t write_val  = data >>
					(util_width(uint8_t) * written_size);
		register_t write_size = access_size - written_size;

		switch (offset + written_size) {
		case offsetof(pci_msix_vector_t, addr):
			if (write_size == 4U) {
				vector->addr &= ~util_mask(32U);
				vector->addr |= write_val & util_mask(32U);
			} else {
				vector->addr = write_val;
			}
			written_size += write_size;
			break;
		case offsetof(pci_msix_vector_t, addr) + 4U:
			vector->addr &= util_mask(32U);
			vector->addr |= write_val << 32U;
			written_size += 4U;
			break;
		case offsetof(pci_msix_vector_t, data):
			vector->data = (uint32_t)write_val;
			written_size += sizeof(vector->data);
			break;
		case offsetof(pci_msix_vector_t, control): {
			pci_msix_vector_control_t control =
				pci_msix_vector_control_cast(
					(uint32_t)write_val);
			vpci_msix_update_vector_mask(
				function, msix_data, vector,
				pci_msix_vector_control_get_mask(&control));
			written_size += sizeof(control);
			break;
		}
		default:
			panic("Invalid offset");
		}
	}
}

error_t
vpci_msix_table_write(vpci_function_t *function, vpci_msix_data_t *msix_data,
		      size_t offset, size_t access_size, register_t data)
{
	error_t err;

	// Only DWORD and QWORD accesses need to be supported.
	//
	// Note that a QWORD write to the data and control words of a masked
	// vector must handle the data word first, in case the control write
	// unmasks the vector; fortunately the data word is at the lower address
	// so that happens naturally.
	if ((access_size != sizeof(uint32_t)) &&
	    (access_size != sizeof(uint64_t))) {
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	// Only aligned accesses need to be supported.
	if ((offset % access_size) != 0U) {
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	index_t vector_index = (index_t)(offset / sizeof(pci_msix_vector_t));
	if (vector_index >= msix_data->vector_count) {
		err = ERROR_UNIMPLEMENTED;
		goto out;
	}

	vpci_msix_vector_write(function, msix_data,
			       &msix_data->vectors[vector_index],
			       offset % sizeof(pci_msix_vector_t), access_size,
			       data);
	err = OK;

out:
	return err;
}

register_result_t
vpci_msix_pba_read(vpci_msix_data_t *msix_data, size_t offset,
		   size_t access_size)
{
	// This is only required to support aligned QWORD and DWORD accesses,
	// but the loop below will work for any size and alignment, so there is
	// no need to check.
	register_t pending_bits = 0U;
	index_t	   vector_index = (index_t)(offset * 8U);
	for (index_t i = 0U; (i < (access_size * 8U)) &&
			     ((i + vector_index) < msix_data->vector_count);
	     i++) {
		vpci_msix_status_t status = atomic_load_relaxed(
			&msix_data->vectors[i + vector_index].status);
		if (vpci_msix_status_get_pending(&status)) {
			pending_bits |= util_bit(i);
		}
	}

	return register_result_ok(pending_bits);
}

error_t
vpci_msix_send(vpci_function_t *function, vpci_msix_data_t *msix_data,
	       index_t vector_index)
{
	assert(function != NULL);
	assert(msix_data != NULL);
	error_t err;

	if (vector_index < msix_data->vector_count) {
		vpci_msix_vector_t *vector = &msix_data->vectors[vector_index];
		pci_msix_message_control_t fn_control =
			atomic_load_acquire(&msix_data->control);

		if (compiler_unexpected(!vpci_msix_enabled(fn_control))) {
			// Disabled or masked at the function level. Set the
			// vector's pending bit first, and then re-check the
			// function-level enables in case we are racing with
			// another CPU.
			vpci_msix_status_t pending_bit =
				vpci_msix_status_default();
			vpci_msix_status_set_pending(&pending_bit, true);
			(void)vpci_msix_status_atomic_union(
				&vector->status, pending_bit,
				memory_order_relaxed);
			// See comment in vpci_msix_config_write.
			atomic_thread_fence(memory_order_seq_cst);
			fn_control = atomic_load_relaxed(&msix_data->control);
		}

		if (vpci_msix_enabled(fn_control)) {
			vpci_msix_status_t status =
				atomic_load_acquire(&vector->status);
			if (!vpci_msix_status_get_mask(&status) &&
			    atomic_compare_exchange_strong_explicit(
				    &vector->status, &status,
				    vpci_msix_status_default(),
				    memory_order_acquire,
				    memory_order_acquire)) {
				vpci_dispatch_msi(function, vector->addr,
						  vector->data);
			}
		}

		err = OK;
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}

error_t
vpci_msix_cancel(vpci_function_t *function, vpci_msix_data_t *msix_data,
		 index_t vector_index)
{
	assert(function != NULL);
	assert(msix_data != NULL);
	error_t err;

	if (vector_index < msix_data->vector_count) {
		vpci_msix_vector_t *vector = &msix_data->vectors[vector_index];

		vpci_msix_status_t pending_bit = vpci_msix_status_default();
		vpci_msix_status_set_pending(&pending_bit, true);
		(void)vpci_msix_status_atomic_difference(
			&vector->status, pending_bit, memory_order_relaxed);

		err = OK;
	} else {
		err = ERROR_ARGUMENT_INVALID;
	}

	return err;
}
