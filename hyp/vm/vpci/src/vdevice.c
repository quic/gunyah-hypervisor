// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <list.h>
#include <object.h>
#include <panic.h>
#include <rcu.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vpci.h>

#include <events/vpci.h>

#include "event_handlers.h"
#include "host_bridge.h"
#include "mapping.h"

static vpci_capability_data_t *
vpci_config_find_capability(vpci_function_t *function, size_t offset)
{
	vpci_capability_data_t *cap;

	for (cap = function->capabilities; cap != NULL; cap = cap->next) {
		if ((offset >= cap->offset) &&
		    (offset < (cap->offset + (size_t)cap->size))) {
			break;
		}
	}

	return cap;
}

static bool
vpci_config_capability_header_read(vpci_capability_data_t *capability,
				   size_t		   reg_offset,
				   register_t		  *accessed_value,
				   size_t		  *accessed_size)
{
	bool handled = true;

	if ((capability->type == PCI_CAPABILITY_TYPE_STANDARD) &&
	    ((reg_offset - capability->offset) ==
	     offsetof(pci_capability_t, cap_id))) {
		// Capability ID byte
		*accessed_value = capability->cap_id;
		*accessed_size	= 1U;
	} else if ((capability->type == PCI_CAPABILITY_TYPE_EXTENDED) &&
		   ((reg_offset - capability->offset) ==
		    offsetof(pcie_ext_capability_t, cap_id))) {
		// Capability ID 2 bytes
		*accessed_value = capability->cap_id;
		*accessed_size	= 2U;
	} else if ((capability->type == PCI_CAPABILITY_TYPE_STANDARD) &&
		   ((reg_offset - capability->offset) ==
		    offsetof(pci_capability_t, next))) {
		// Capability next pointer
		*accessed_value = (capability->next != NULL)
					  ? capability->next->offset
					  : 0U;
		*accessed_size	= 1U;
	} else if ((capability->type == PCI_CAPABILITY_TYPE_EXTENDED) &&
		   ((reg_offset - capability->offset) ==
		    offsetof(pcie_ext_capability_t, version_next))) {
		pcie_ext_version_next_t val = pcie_ext_version_next_default();
		pcie_ext_version_next_set_version(&val, capability->version);
		pcie_ext_version_next_set_next(
			&val, (capability->next != NULL)
				      ? capability->next->offset
				      : 0U);
		// Capability version and next pointer
		*accessed_value = pcie_ext_version_next_raw(val);
		*accessed_size	= 2U;
	} else if ((capability->cap_id == (uint8_t)PCI_CAPABILITY_ID_VENDOR) &&
		   (capability->type == PCI_CAPABILITY_TYPE_STANDARD) &&
		   ((reg_offset - capability->offset) ==
		    offsetof(pci_vendor_capability_t, size))) {
		// Capability size byte (vendor capabilitites only)
		*accessed_value = capability->size;
		*accessed_size	= 1U;
	} else {
		// Access is in capability structure content
		handled = false;
	}

	return handled;
}

static bool
vpci_config_capability_header_write(vpci_capability_data_t *capability,
				    size_t reg_offset, size_t *accessed_size)
{
	bool handled = true;

	// The capability headers are mostly WI
	if ((capability->type == PCI_CAPABILITY_TYPE_STANDARD) &&
	    ((reg_offset - capability->offset) < 2U)) {
		// Standard Capability header; WI
		*accessed_size = 1U;
	} else if ((capability->type == PCI_CAPABILITY_TYPE_STANDARD) &&
		   (capability->cap_id == (uint8_t)PCI_CAPABILITY_ID_VENDOR) &&
		   ((reg_offset - capability->offset) == 2U)) {
		// Capability size byte; WI (vendor capabilities)
		*accessed_size = 1U;
	} else if ((capability->type == PCI_CAPABILITY_TYPE_EXTENDED) &&
		   ((reg_offset - capability->offset) < 4U)) {
		// Extended Capability header; WI
		*accessed_size = 1U;
	} else {
		handled = false;
	}

	return handled;
}

static uint32_t
vpci_config_bar_read(vpci_function_t *function, index_t bar_index)
{
	uint32_t ret;

	assert(function != NULL);
	assert(bar_index < util_array_size(function->bars));
	vpci_bar_data_t *bar = &function->bars[bar_index];

	if ((bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH) &&
	    (bar_index > 0U)) {
		bar = &function->bars[bar_index - 1U];
		assert(bar->size_bits != 0U);
		ret = (uint32_t)((bar->pci_addr & ~util_mask(bar->size_bits)) >>
				 32U);
	} else if (bar->size_bits == 0U) {
		ret = 0U;
	} else {
		ret = (uint32_t)(bar->pci_addr & ~util_mask(bar->size_bits));

		// Set the type bits. It isn't really possible to do this with
		// the type system's bitfields, because bits 3:2 are address
		// bits for I/O BARs, but are type bits for memory BARs.
		switch (bar->address_type) {
		case VPCI_BAR_ADDRESS_TYPE_IO:
			ret |= 0x1U;
			break;
		case VPCI_BAR_ADDRESS_TYPE_MEM_NP_32:
			// All type bits are 0
			break;
		case VPCI_BAR_ADDRESS_TYPE_MEM_32:
			ret |= 0x8U;
			break;
		case VPCI_BAR_ADDRESS_TYPE_MEM_64:
			ret |= 0xcU;
			break;
		case VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH:
		default:
			panic("unreachable");
		}
	}

	return ret;
}

static void
vpci_config_bar_update(vpci_device_t *device, vpci_function_t *function,
		       vpci_bar_data_t *bar, index_t bar_index,
		       uint64_t new_value)
	RELEASE_SPINLOCK(device -> mapping_lock)
{
	vmaddr_t new_pci_addr = util_p2align_down(new_value, bar->size_bits);

	// Calculate the IPA corresponding to the PCI IO or Memory address.
	vmaddr_t new_ipa_addr;
	if (bar->address_type == VPCI_BAR_ADDRESS_TYPE_IO) {
		// Any PCI I/O address is relative to the IPA base of the I/O
		// aperture.
		new_ipa_addr = new_pci_addr + device->bus->io_base;
	} else if ((new_pci_addr < util_bit(32U)) &&
		   (device->bus->npmem_base >= util_bit(32U)) &&
		   !vpci_map_bar_check_range(device->bus, bar, new_pci_addr)) {
		// If a PCI Memory range is below 4GiB and can't be 1:1 mapped
		// in an aperture, and the NP Memory aperture starts above 4GiB,
		// then extend the base address with the high bits of the NP
		// Memory aperture base. This effectively aliases the NP Memory
		// aperture in the low 4GiB of IPA space. Note that it might
		// also alias part of the P Memory aperture; that aperture will
		// take precedence for P Memory BARs.
		new_ipa_addr = new_pci_addr |
			       (device->bus->npmem_base & ~util_mask(32U));
	} else {
		// Otherwise, PCI Memory space is 1:1 mapped to IPA space.
		new_ipa_addr = new_pci_addr;
	}

	if (vpci_map_allowed_for_bar(device, function, bar)) {
		vpci_unmap_bar(device->bus, device, bar);
		bar->pci_addr = new_pci_addr;
		bar->ipa_addr = new_ipa_addr;
		spinlock_release(&device->mapping_lock);

		error_t map_err;
		do {
			map_err = vpci_try_map_bar(device->bus, device, bar,
						   bar_index);
			if (map_err == ERROR_RETRY) {
				rcu_sync();
			}
		} while (map_err == ERROR_RETRY);
	} else {
		bar->pci_addr = new_pci_addr;
		bar->ipa_addr = new_ipa_addr;
		spinlock_release(&device->mapping_lock);
	}
}

static size_result_t
vpci_config_bar_write(vpci_function_t *function, index_t bar_index,
		      uint64_t value, size_t access_size)
{
	error_t ret;

	assert(function != NULL);
	assert(bar_index < util_array_size(function->bars));
	vpci_bar_data_t *bar = &function->bars[bar_index];

	vpci_device_t *device = function->device;
	assert(device != NULL);

	spinlock_acquire(&device->mapping_lock);
	if ((access_size == 8U) &&
	    (bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64) &&
	    (bar_index < (util_array_size(function->bars) - 1U))) {
		// Updating a whole 64-bit address
		vpci_config_bar_update(device, function, bar, bar_index, value);
		ret = OK;
	} else if (access_size != 4U) {
		// Invalid access size
		ret = ERROR_UNIMPLEMENTED;
		spinlock_release(&device->mapping_lock);
	} else if (bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64) {
		// Updating low 32 bits of 64-bit address
		uint64_t merged_value = (bar->pci_addr & ~util_mask(32U)) |
					(value & util_mask(32U));
		vpci_config_bar_update(device, function, bar, bar_index,
				       merged_value);
		ret = OK;
	} else if ((bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH) &&
		   (bar_index > 0U)) {
		// Updating high 32 bits of 64-bit address
		bar = &function->bars[bar_index - 1U];

		assert(bar->address_type == VPCI_BAR_ADDRESS_TYPE_MEM_64);
		uint64_t merged_value = (bar->pci_addr & util_mask(32U)) |
					(value << 32U);
		vpci_config_bar_update(device, function, bar, bar_index,
				       merged_value);
		ret = OK;
	} else if (bar->address_type != VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH) {
		// Updating a whole 32-bit address
		vpci_config_bar_update(device, function, bar, bar_index,
				       value & util_mask(32U));
		ret = OK;
	} else {
		panic("unreachable");
	}

	return (ret == OK) ? size_result_ok(access_size)
			   : size_result_error(ret);
}

static void
vpci_function_header_read(vpci_function_t *function, size_t offset,
			  size_t *access_size, register_t *value)
	REQUIRE_RCU_READ
{
	// Function header (type 0) access
	switch (offset) {
	case util_offset_case_range(pci_config_header_t, vendor_id):
		*value = function->vendor_id;
		*access_size =
			util_sizeof_member(pci_config_header_t, vendor_id);
		break;
	case util_offset_case_range(pci_config_header_t, product_id):
		*value = function->product_id;
		*access_size =
			util_sizeof_member(pci_config_header_t, product_id);
		break;
	case util_offset_case_range(pci_config_header_t, command):
		*value	     = pci_function_command_raw(function->command);
		*access_size = util_sizeof_member(pci_config_header_t, command);
		break;
	case util_offset_case_range(pci_config_header_t, status): {
		pci_function_status_t status =
			atomic_load_relaxed(&function->status);
		pci_function_status_set_int_state(
			&status, trigger_vpci_irq_check_pending_event(
					 function->type, function));
		pci_function_status_set_capabilities(
			&status, function->capabilities != NULL);
		*value	     = pci_function_status_raw(status);
		*access_size = util_sizeof_member(pci_config_header_t, status);
		break;
	}
	case util_offset_case_range(pci_config_header_t, device_revision):
		*value	     = function->device_revision;
		*access_size = util_sizeof_member(pci_config_header_t,
						  device_revision);
		break;
	case util_offset_case_range(pci_config_header_t, device_interface):
		*value	     = function->device_interface;
		*access_size = util_sizeof_member(pci_config_header_t,
						  device_interface);
		break;
	case util_offset_case_range(pci_config_header_t, device_subclass):
		*value	     = function->device_subclass;
		*access_size = util_sizeof_member(pci_config_header_t,
						  device_subclass);
		break;
	case util_offset_case_range(pci_config_header_t, device_class):
		*value = function->device_class;
		*access_size =
			util_sizeof_member(pci_config_header_t, device_class);
		break;
	case util_offset_case_range(pci_config_header_t, header_type):
#if VPCI_PCIE_ONLY
		*value = 0U;
		*access_size =
			util_sizeof_member(pci_config_header_t, header_type);
#else
#error Type 1 config headers not implemented
#endif
		break;

	case util_offset_case_range(pci_config_header_t, bars): {
		size_t bars_offset =
			offset - offsetof(pci_config_header_t, bars);
		*access_size = util_sizeof_member(pci_config_header_t, bars[0]);
		index_t bar_index = (index_t)(bars_offset / *access_size);
		*value		  = vpci_config_bar_read(function, bar_index);
		break;
	}
	case util_offset_case_range(pci_config_header_t, subsys_vendor_id):
		*value	     = function->subsys_vendor_id;
		*access_size = util_sizeof_member(pci_config_header_t,
						  subsys_vendor_id);
		break;
	case util_offset_case_range(pci_config_header_t, subsys_id):
		*value = function->subsys_product_id;
		*access_size =
			util_sizeof_member(pci_config_header_t, subsys_id);
		break;
	case util_offset_case_range(pci_config_header_t, capability_ptr):
		*value = (function->capabilities != NULL)
				 ? function->capabilities->offset
				 : 0U;
		*access_size =
			util_sizeof_member(pci_config_header_t, capability_ptr);
		break;
	case util_offset_case_range(pci_config_header_t, interrupt_line):
		*value = function->interrupt_line;
		*access_size =
			util_sizeof_member(pci_config_header_t, interrupt_line);
		break;
	case util_offset_case_range(pci_config_header_t, interrupt_pin):
		*value = function->interrupt_pin;
		*access_size =
			util_sizeof_member(pci_config_header_t, interrupt_pin);
		break;
	default:
		// All other registers are unimplemented and
		// RAZ/WI
		*value	     = 0U;
		*access_size = 1U;
		break;
	}

	// Handle partial accesses (note all fields are size-aligned)
	size_t mask = *access_size - 1U;
	if ((offset & mask) != 0U) {
		*value >>= util_width(uint8_t) * (offset & mask);
		*access_size = (0U - offset) & mask;
	}
}

static register_result_t
vpci_config_read(vpci_function_t *function, size_t offset, size_t access_size)
	RELEASE_RCU_READ
{
	register_result_t ret;

	size_t	   reg_offset = offset;
	size_t	   read_size  = 0U;
	register_t read_value = 0U;

	while (access_size > read_size) {
		size_t	   accessed_size;
		register_t accessed_value;

		if (reg_offset >= sizeof(pci_config_header_t)) {
			// PCI device-specific config space
			vpci_capability_data_t *capability =
				vpci_config_find_capability(function,
							    reg_offset);
			if (capability == NULL) {
				// Not in any capability structure
				register_result_t access_r =
					trigger_vpci_config_read_event(
						function->type, function, NULL,
						reg_offset,
						access_size - read_size);
				// This should be released by the event.
				// FIXME: QC Gunyah issue #252
				rcu_read_finish();
				if (access_r.e != OK) {
					ret = access_r;
					goto failed;
				}
				read_value |=
					access_r.r
					<< (read_size * util_width(uint8_t));
				goto done;
			} else if (vpci_config_capability_header_read(
					   capability, reg_offset,
					   &accessed_value, &accessed_size)) {
				// Nothing to be done here
			} else {
				// Capability structure contents
				register_result_t access_r =
					trigger_vpci_config_read_event(
						function->type, function,
						capability,
						reg_offset - capability->offset,
						access_size);
				// This should be released by the event.
				// FIXME: QC Gunyah issue #252
				rcu_read_finish();
				if (access_r.e != OK) {
					ret = access_r;
					goto failed;
				}
				read_value |=
					access_r.r
					<< (read_size * util_width(uint8_t));
				goto done;
			}
		} else {
			accessed_size = 0U;
			vpci_function_header_read(function, reg_offset,
						  &accessed_size,
						  &accessed_value);
			assert(accessed_size > 0U);
		}

		read_value |= accessed_value
			      << (read_size * util_width(uint8_t));
		read_size += accessed_size;
		reg_offset += accessed_size;
	}
	rcu_read_finish();
done:
	ret = register_result_ok(read_value);
failed:
	return ret;
}

static size_result_t
vpci_function_header_write(vpci_function_t *function, size_t offset,
			   size_t access_size, register_t value)
{
	size_result_t ret;

	// Function header (type 0) access
	switch (offset) {
	case offsetof(pci_config_header_t, command): {
		size_t reg_size =
			util_sizeof_member(pci_config_header_t, command);
		if (access_size < reg_size) {
			ret = size_result_error(ERROR_UNIMPLEMENTED);
			goto out;
		}

		const pci_function_command_t command_val =
			pci_function_command_cast((uint16_t)value);
		pci_function_command_t new_command =
			pci_function_command_default();
		vpci_device_t *device = function->device;
		vpci_t	      *bus    = device->bus;

		// Copy the RW bits out of the write value
		pci_function_command_copy_io_space(&new_command, &command_val);
		pci_function_command_copy_mem_space(&new_command, &command_val);
		pci_function_command_copy_bus_master(&new_command,
						     &command_val);
		pci_function_command_copy_parity_errors(&new_command,
							&command_val);
		pci_function_command_copy_serr_enabled(&new_command,
						       &command_val);
		pci_function_command_copy_int_disabled(&new_command,
						       &command_val);
		if (!vpci_is_pcie(bus)) {
			pci_function_command_copy_special_cycles(&new_command,
								 &command_val);
		}

		// Update the memory and I/O mappings, if
		// necessary
		spinlock_acquire(&device->mapping_lock);

		bool need_map = false;

		bool map_io = pci_function_command_get_io_space(&new_command);
		if (pci_function_command_get_io_space(&function->command) !=
		    map_io) {
			if (map_io) {
				need_map = true;
			} else {
				vpci_unmap_function(bus, device, function, true,
						    false);
			}
		}

		bool map_mem = pci_function_command_get_mem_space(&new_command);
		if (pci_function_command_get_mem_space(&function->command) !=
		    map_mem) {
			if (map_mem) {
				need_map = true;
			} else {
				vpci_unmap_function(bus, device, function,
						    false, true);
			}
		}

		if (pci_function_command_get_int_disabled(&function->command) &&
		    !pci_function_command_get_int_disabled(&new_command) &&
		    trigger_vpci_irq_check_pending_event(function->type,
							 function)) {
			vpci_irq_assert(function);
		}

		function->command = new_command;
		spinlock_release(&device->mapping_lock);

		while (need_map) {
			error_t map_err = vpci_try_map_function(
				bus, device, function, map_io, map_mem);
			if (map_err == OK) {
				// Mappings were all completed
				break;
			}
			if (map_err != ERROR_RETRY) {
				ret = size_result_error(map_err);
				goto out;
			}

			// Wait for unmaps to complete before retrying
			rcu_sync();
		}

		ret = size_result_ok(reg_size);
		break;
	}
	case offsetof(pci_config_header_t, status): {
		size_t reg_size =
			util_sizeof_member(pci_config_header_t, status);
		if (access_size < reg_size) {
			ret = size_result_error(ERROR_UNIMPLEMENTED);
			goto out;
		}

		const pci_function_status_t status_val =
			pci_function_status_cast((uint16_t)value);
		pci_function_status_t status_w1c =
			pci_function_status_default();

		// Copy the W1C bits out of the write value
		pci_function_status_copy_master_data_parity_err(&status_w1c,
								&status_val);
		pci_function_status_copy_signalled_target_abort(&status_w1c,
								&status_val);
		pci_function_status_copy_received_target_abort(&status_w1c,
							       &status_val);
		pci_function_status_copy_received_master_abort(&status_w1c,
							       &status_val);
		pci_function_status_copy_serr_asserted(&status_w1c,
						       &status_val);
		pci_function_status_copy_parity_error(&status_w1c, &status_val);

		// Clear the W1C bits in the function state
		(void)pci_function_status_atomic_difference(
			&function->status, status_w1c, memory_order_relaxed);
		ret = size_result_ok(reg_size);
		break;
	}
	case offsetof(pci_config_header_t, bars[0]):
		ret = vpci_config_bar_write(function, 0, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, bars[1]):
		ret = vpci_config_bar_write(function, 1, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, bars[2]):
		ret = vpci_config_bar_write(function, 2, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, bars[3]):
		ret = vpci_config_bar_write(function, 3, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, bars[4]):
		ret = vpci_config_bar_write(function, 4, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, bars[5]):
		ret = vpci_config_bar_write(function, 5, (uint32_t)value,
					    access_size);
		break;
	case offsetof(pci_config_header_t, interrupt_line):
		function->interrupt_line = (uint8_t)value;
		ret = size_result_ok(util_sizeof_member(pci_config_header_t,
							interrupt_line));
		break;
	case offsetof(pci_config_header_t, bist):
	case offsetof(pci_config_header_t, cacheline_size):
	case offsetof(pci_config_header_t, latency_timer):
		// Single-byte WI registers
		ret = size_result_ok(1U);
		break;
	default:
		// Writes not allowed; trigger an abort
		ret = size_result_error(ERROR_UNIMPLEMENTED);
		break;
	}

out:
	return ret;
}

static error_t
vpci_config_write(vpci_function_t *function, size_t offset, size_t access_size,
		  register_t access_value) RELEASE_RCU_READ
{
	error_t ret;

	size_t	   reg_offset	  = offset;
	size_t	   remaining_size = access_size;
	register_t value	  = access_value;
	while (remaining_size > 0U) {
		size_t accessed_size;

		if (reg_offset >= sizeof(pci_config_header_t)) {
			// PCI device-specific config space
			vpci_capability_data_t *capability =
				vpci_config_find_capability(function,
							    reg_offset);
			if (capability == NULL) {
				// Not in any capability structure
				ret = trigger_vpci_config_write_event(
					function->type, function, NULL,
					reg_offset, remaining_size, value);
				// This should be released by the event.
				// FIXME: QC Gunyah issue #252
				rcu_read_finish();
				goto done;
			} else if (vpci_config_capability_header_write(
					   capability, reg_offset,
					   &accessed_size)) {
				// Nothing to be done here
			} else {
				// Capability structure contents
				ret = trigger_vpci_config_write_event(
					function->type, function, capability,
					reg_offset - capability->offset,
					remaining_size, value);
				// This should be released by the event.
				// FIXME: QC Gunyah issue #252
				rcu_read_finish();
				goto done;
			}
		} else {
			size_result_t size_r = vpci_function_header_write(
				function, reg_offset, remaining_size, value);
			if (size_r.e != OK) {
				ret = size_r.e;
				goto failed;
			}
			accessed_size = size_r.r;
		}

		assert(remaining_size <= accessed_size);

		remaining_size -= accessed_size;
		reg_offset += accessed_size;
		value >>= accessed_size * util_width(uint8_t);
	}
	ret = OK;
failed:
	rcu_read_finish();
done:
	return ret;
}

vcpu_trap_result_t
vpci_vdevice_access_cam(vdevice_t *vdevice, size_t offset, size_t access_size,
			register_t *value, bool is_write)
	REQUIRE_READ((rcu_read))
{
	vpci_t		  *bus	      = vpci_container_of_vdevice(vdevice);
	bool		   pcie	      = vpci_is_pcie(bus);
	count_t		   reg_bits   = pcie ? 12U : 8U;
	size_t		   reg_offset = offset & util_mask(reg_bits);
	pci_responder_id_t rid =
		pci_responder_id_cast((uint16_t)(offset >> reg_bits));

	// Failed read accesses are RAO.
	if (!is_write) {
		*value = ~(register_t)0U;
	}

	// Configuration space only supports accesses within one aligned 64-bit
	// word; we're allowed to reject anything else. This means we don't have
	// to handle accesses that cross a function boundary.
	if ((offset / sizeof(uint64_t)) !=
	    ((offset + access_size - 1U) / sizeof(uint64_t))) {
		vpci_bus_error(bus);
		goto out;
	}

	if (pci_responder_id_get_bus(&rid) != 0U) {
		// Bridges are not supported, so all devices are on bus 0
		vpci_bus_error(bus);
		goto out;
	}

	vpci_device_t *device = atomic_load_consume(
		&bus->slots[pci_responder_id_get_slot(&rid)]);
	if (device == NULL) {
		// No device in the slot
		TRACE(DEBUG, INFO, "vpci {:#x}: CAM absent device, RID {:#x}",
		      (uintptr_t)bus, pci_responder_id_raw(rid));
		vpci_bus_error(bus);
		goto out;
	}

	vpci_function_t *function =
		device->functions[pci_responder_id_get_function(&rid)];
	if (function == NULL) {
		// Device does not have this function
		TRACE(DEBUG, INFO, "vpci {:#x}: CAM absent function, RID {:#x}",
		      (uintptr_t)bus, pci_responder_id_raw(rid));
		vpci_bus_error(bus);
		goto out;
	}

#if !ARCH_ENDIAN_LITTLE
#error Unimplemented
#endif

	if (is_write) {
		TRACE(DEBUG, INFO,
		      "vpci {:#x}: CAM write RID {:#x}: {:#x}@{:#x}/{:d}",
		      (uintptr_t)bus, pci_responder_id_raw(rid), *value,
		      reg_offset, access_size);
		error_t err = vpci_config_write(function, reg_offset,
						access_size, *value);
		// The config access might have blocked, and has therefore
		// dropped the RCU critical section. Re-acquire it.
		// FIXME: QC Gunyah issue #252
		rcu_read_start();
		if (err != OK) {
			TRACE(DEBUG, WARN, "vpci {:#x}: CAM write failed: {:d}",
			      (uintptr_t)bus, (register_t)err);
			goto out;
		}
	} else {
		size_t		  byte_offset = 0U;
		register_result_t val_r	      = vpci_config_read(
			      function, reg_offset + byte_offset, access_size);
		// The config access might have blocked, and has therefore
		// dropped the RCU critical section. Re-acquire it.
		// FIXME: QC Gunyah issue #252
		rcu_read_start();
		if (val_r.e != OK) {
			TRACE(DEBUG, WARN,
			      "vpci {:#x}: CAM read failed {:d}, RID {:#x}: {:#x}/{:d}",
			      (uintptr_t)bus, (register_t)val_r.e,
			      pci_responder_id_raw(rid), reg_offset,
			      access_size);
			goto out;
		}
		*value = val_r.r;
		TRACE(DEBUG, INFO,
		      "vpci {:#x}: CAM read RID {:#x}: {:#x}@{:#x}/{:d}",
		      (uintptr_t)bus, pci_responder_id_raw(rid), *value,
		      reg_offset, access_size);
	}
out:
	return VCPU_TRAP_RESULT_EMULATED;
}

static void
vpci_handle_bar_access(vpci_function_t *function, index_t bar_index,
		       size_t offset, size_t access_size, register_t *value,
		       bool is_write) RELEASE_RCU_READ
{
#if !ARCH_ENDIAN_LITTLE
#error Unimplemented
#endif
	if (is_write) {
		TRACE(DEBUG, INFO,
		      "vpci function {:#x}: BAR {:d} write: {:#x}@{:#x}/{:d}",
		      (uintptr_t)function, bar_index, *value, offset,
		      access_size);
		error_t err = trigger_vpci_bar_write_event(function->type,
							   function, bar_index,
							   offset, access_size,
							   *value);
		if (err != OK) {
			TRACE(DEBUG, WARN,
			      "vpci function {:#x}: BAR write failed: {:d}",
			      (uintptr_t)function, (register_t)err);
			goto out;
		}
	} else {
		register_result_t read_r = trigger_vpci_bar_read_event(
			function->type, function, bar_index, offset,
			access_size);
		if (read_r.e != OK) {
			TRACE(DEBUG, WARN,
			      "vpci function {:#x}: BAR {:d} read failed {:d}: {:#x}/{:d}",
			      (uintptr_t)function, bar_index,
			      (register_t)read_r.e, offset, access_size);
		} else {
			TRACE(DEBUG, INFO,
			      "vpci function {:#x}: BAR {:d} read: {:#x}@{:#x}/{:d}",
			      (uintptr_t)function, bar_index, read_r.r, offset,
			      access_size);
			*value = read_r.r;
		}
	}

out:
	// This should be released by the events.
	// FIXME: QC Gunyah issue #252
	rcu_read_finish();
}

vcpu_trap_result_t
vpci_vdevice_access_bar(vdevice_type_t type, vdevice_t *vdevice, size_t offset,
			size_t access_size, register_t *value, bool is_write)
{
	(void)type;

	vcpu_trap_result_t ret;

	vpci_bar_data_t *bar = vpci_bar_data_container_of_vdevice(vdevice);
	assert(bar != NULL);
	assert(bar->index < util_member_array_size(vpci_function_t, bars));

	vpci_function_t *function =
		vpci_function_container_of_bars_element(bar, bar->index);
	assert(function != NULL);

	assert(bar->size_bits != 0U);
	assert(((bar->access_type == VPCI_BAR_ACCESS_TYPE_TRAPPED_W) &&
		is_write) ||
	       (bar->access_type == VPCI_BAR_ACCESS_TYPE_TRAPPED));

	vpci_handle_bar_access(function, bar->index, offset, access_size, value,
			       is_write);
	ret = VCPU_TRAP_RESULT_EMULATED;

	// The BAR access might have blocked, and has therefore dropped the RCU
	// critical section. Re-acquire it.
	// FIXME: QC Gunyah issue #252
	rcu_read_start();

	return ret;
}
