// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <atomic.h>
#include <compiler.h>
#include <util.h>
#include <vpci.h>

error_t
vpci_pcie_init(partition_t *owner, vpci_function_t *function,
	       vpci_pcie_data_t *pcie_data)
{
	assert(pcie_data->base.cap_id == (uint8_t)PCI_CAPABILITY_ID_PCIE);
	assert(pcie_data->base.size == sizeof(pci_express_capability_t));
	assert(pcie_data->base.offset >= 0x40U);
	assert(function->pcie_data == NULL);

	(void)owner;

	function->pcie_data = pcie_data;

	return OK;
}

error_t
vpci_pcie_free(partition_t *owner, vpci_function_t *function,
	       vpci_pcie_data_t *pcie_data)
{
	assert(function->pcie_data == pcie_data);
	function->pcie_data = NULL;

	(void)owner;
	(void)pcie_data;

	return OK;
}

static register_t
vpci_pcie_config_read_one(vpci_pcie_data_t *pcie_data, size_t offset,
			  size_t *access_size)
{
	register_t val;

	switch (offset) {
	case util_offset_case_range(pci_express_capability_t, pcie_caps): {
		// Currently all PCIe devices are RCIEs. This will need to
		// change if we implement bridges or advanced error handling.
		pci_express_capabilities_t pcie_caps =
			pci_express_capabilities_default();
		pci_express_capabilities_set_type(&pcie_caps,
						  PCI_EXPRESS_DEVICE_TYPE_RCIE);
		val = pci_express_capabilities_raw(pcie_caps);
		*access_size =
			util_sizeof_member(pci_express_capability_t, pcie_caps);
		break;
	}
	case util_offset_case_range(pci_express_capability_t, device_caps):
		val = pci_express_device_capabilities_raw(
			pci_express_device_capabilities_default());
		*access_size = util_sizeof_member(pci_express_capability_t,
						  device_caps);
		break;
	case util_offset_case_range(pci_express_capability_t, device_control):
		val = pci_express_device_control_raw(pcie_data->pcie_control);
		*access_size = util_sizeof_member(pci_express_capability_t,
						  device_control);
		break;
	case util_offset_case_range(pci_express_capability_t, device_status):
		val = pci_express_device_status_raw(
			atomic_load_relaxed(&pcie_data->pcie_status));
		*access_size = util_sizeof_member(pci_express_capability_t,
						  device_status);
		break;
	default:
		// All other registers are not used for an RCIE and are RAZ/WI.
		val	     = 0U;
		*access_size = 1U;
		break;
	}

	// Handle partial accesses (note all fields are size-aligned)
	size_t mask = *access_size - 1U;
	if ((offset & mask) != 0U) {
		val >>= util_width(uint8_t) * (offset & mask);
		*access_size = (0U - offset) & mask;
	}

	return val;
}

register_result_t
vpci_pcie_config_read(vpci_pcie_data_t *pcie_data, size_t offset,
		      size_t access_size)
{
	register_t read_val  = 0U;
	size_t	   read_size = 0U;

	while (read_size < access_size) {
		size_t read_offset = offset + read_size;
		size_t accessed_size;

		register_t accessed_val = vpci_pcie_config_read_one(
			pcie_data, read_offset, &accessed_size);

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

static error_t
vpci_pcie_config_write_one(vpci_pcie_data_t *pcie_data, size_t offset,
			   size_t *access_size, register_t data)
{
	error_t err = OK;

	switch (offset) {
	case offsetof(pci_express_capability_t, device_control): {
		size_t reg_size = util_sizeof_member(pci_express_capability_t,
						     device_control);
		if (*access_size < reg_size) {
			err = ERROR_UNIMPLEMENTED;
			goto out;
		}
		*access_size = reg_size;

		pci_express_device_control_t new_control =
			pci_express_device_control_cast((uint16_t)data);
		// Zero the bits that are not currently supported, or are
		// meaningless for virtual devices.
		pci_express_device_control_set_extended_tag(&new_control,
							    false);
		pci_express_device_control_set_phantom_functions(&new_control,
								 false);
		pci_express_device_control_set_aux_power(&new_control, false);
		pci_express_device_control_set_function_reset(&new_control,
							      false);

		// Error reporting is currently not supported; we only implement
		// RCiEPs without Root Complex Event Collectors. So, don't allow
		// any of the error reporting bits to be set. Note that this is
		// permitted by the spec for RCiEPs.
		pci_express_device_control_set_correctable_errors(&new_control,
								  false);
		pci_express_device_control_set_nonfatal_errors(&new_control,
							       false);
		pci_express_device_control_set_fatal_errors(&new_control,
							    false);
		pci_express_device_control_set_unsupported_requests(
			&new_control, false);

		pcie_data->pcie_control = new_control;
		break;
	}
	case offsetof(pci_express_capability_t, device_status): {
		size_t reg_size = util_sizeof_member(pci_express_capability_t,
						     device_status);
		if (*access_size < reg_size) {
			err = ERROR_UNIMPLEMENTED;
			goto out;
		}
		*access_size = reg_size;

		const pci_express_device_status_t status_val =
			pci_express_device_status_cast((uint16_t)data);
		pci_express_device_status_t status_w1c =
			pci_express_device_status_default();

		// Copy the W1C bits out of the write value
		pci_express_device_status_copy_correctable_error(&status_w1c,
								 &status_val);
		pci_express_device_status_copy_nonfatal_error(&status_w1c,
							      &status_val);
		pci_express_device_status_copy_fatal_error(&status_w1c,
							   &status_val);
		pci_express_device_status_copy_unsupported_request(&status_w1c,
								   &status_val);

		// Clear the W1C bits in the function state
		(void)pci_express_device_status_atomic_difference(
			&pcie_data->pcie_status, status_w1c,
			memory_order_relaxed);

		break;
	}
	default:
		err = ERROR_UNIMPLEMENTED;
		break;
	}

out:
	return err;
}

error_t
vpci_pcie_config_write(vpci_pcie_data_t *pcie_data, size_t offset,
		       size_t access_size, register_t data)
{
	error_t ret;

	size_t	   reg_offset	  = offset;
	size_t	   remaining_size = access_size;
	register_t value	  = data;
	while (remaining_size > 0U) {
		size_t accessed_size = remaining_size;

		ret = vpci_pcie_config_write_one(pcie_data, reg_offset,
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

register_result_t
vpci_pcie_ext_config_read(vpci_pcie_data_t *pcie_data, size_t offset,
			  size_t *access_size)
{
#if defined(VPCI_PCIE_CAPABILITIES) && VPCI_PCIE_CAPABILITIES
#error Unimplemented
#else
	(void)pcie_data;
	(void)offset;
	(void)access_size;
	return register_result_error(ERROR_UNIMPLEMENTED);
#endif
}

error_t
vpci_pcie_ext_config_write(vpci_pcie_data_t *pcie_data, size_t offset,
			   size_t *access_size, register_t data)
{
#if defined(VPCI_PCIE_CAPABILITIES) && VPCI_PCIE_CAPABILITIES
#error Unimplemented
#else
	(void)pcie_data;
	(void)offset;
	(void)access_size;
	(void)data;
	return ERROR_UNIMPLEMENTED;
#endif
}

void
vpci_pcie_error(vpci_function_t *reporter, error_t error)
{
	vpci_pcie_data_t *pcie_data = reporter->pcie_data;
	if (pcie_data == NULL) {
		goto out;
	}

	pci_express_device_status_t new_status =
		pci_express_device_status_default();

	if (compiler_expected(error == OK)) {
		goto out;
	} else if ((error == ERROR_UNIMPLEMENTED) ||
		   (error == ERROR_ADDR_INVALID)) {
		// PCIe Unsupported Request
		pci_express_device_status_set_unsupported_request(&new_status,
								  true);
	} else if (error == ERROR_RETRY) {
		// PCIe Corrected Error
		pci_express_device_status_set_correctable_error(&new_status,
								true);
	} else if (error == ERROR_FAILURE) {
		// PCIe Uncorrectable (Fatal) Error
		pci_express_device_status_set_fatal_error(&new_status, true);
	} else {
		// PCIe Completer Abort
	}

	(void)pci_express_device_status_atomic_union(
		&pcie_data->pcie_status, new_status, memory_order_relaxed);

out:
	return;
}

static register_t
vpci_pm_config_read_one(size_t offset, size_t *access_size)
{
	register_t val;

	// The vcpi power state it hardcoded. Just return the defaults

	switch (offset) {
	case util_offset_case_range(pci_pm_capability_t, pmc): {
		val	     = pci_pm_pmc_raw(pci_pm_pmc_default());
		*access_size = util_sizeof_member(pci_pm_capability_t, pmc);
		break;
	}
	case util_offset_case_range(pci_pm_capability_t, pmcsr):
		val	     = pci_pm_pmcsr_raw(pci_pm_pmcsr_default());
		*access_size = util_sizeof_member(pci_pm_capability_t, pmcsr);
		break;
	default:
		// All other registers are not used and are RAZ/WI.
		val	     = 0U;
		*access_size = 1U;
		break;
	}

	// Handle partial accesses (note all fields are size-aligned)
	size_t mask = *access_size - 1U;
	if ((offset & mask) != 0U) {
		val >>= util_width(uint8_t) * (offset & mask);
		*access_size = (0U - offset) & mask;
	}

	return val;
}

register_result_t
vpci_pm_config_read(size_t offset, size_t access_size)
{
	register_t read_val  = 0U;
	size_t	   read_size = 0U;

	while (read_size < access_size) {
		size_t read_offset = offset + read_size;
		size_t accessed_size;

		// The vcpi power state it hardcoded. Just return the defaults
		register_t accessed_val =
			vpci_pm_config_read_one(read_offset, &accessed_size);

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
