// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <log.h>
#include <memextent.h>
#include <panic.h>
#include <trace.h>
#include <util.h>

#include "pci_host.h"

#define ECAM_MAX_PCIE_BUS  256U
#define ECAM_MAX_PCIE_DEV  32U
#define ECAM_MAX_PCIE_FUNC 8U

#define ECAM_PCIE_BUS_SHIFT  20
#define ECAM_PCIE_DEV_SHIFT  15
#define ECAM_PCIE_FUNC_SHIFT 12

// PCI BAR Space Encodings - BAR bit[0]
#define ECAM_PCIE_BAR_SPACE_BIT		    (1U << 0)
#define ECAM_PCIE_BAR_SPACE_IO		    1U
#define ECAM_PCIE_BAR_SPACE_MEM		    0U
#define ECAM_PCIE_BAR_IO_TYPE_ENCODING_MASK (util_mask(2))
#define ECAM_PCIE_IS_BAR_TYPE_IO_RES(x)	    ((((x) >> 1) & 1U) == 1U)
#define ECAM_PCIE_BAR_IO_INV_VAL	    0xFFFFFFFCU
#define ECAM_PCIE_IS_BAR_SPACE_IO(x)                                           \
	(((x) & ECAM_PCIE_BAR_SPACE_BIT) == ECAM_PCIE_BAR_SPACE_IO)
#define ECAM_PCIE_IS_BAR_SPACE_MEM(x)                                          \
	(((x) & ECAM_PCIE_BAR_SPACE_BIT) == ECAM_PCIE_BAR_SPACE_MEM)
// Memory Space Type BAR Encodings - BAR bit[2:1]
#define ECAM_PCIE_GET_BAR_MEM_TYPE(x)	     (((x) >> 1) & 3U)
#define ECAM_PCIE_BAR_MEM_TYPE_ENCODING_MASK (util_mask(4))
#define ECAM_PCIE_BAR_MEM_TYPE_32	     0U
#define ECAM_PCIE_BAR_MEM_TYPE_RES0	     1U
#define ECAM_PCIE_BAR_MEM_TYPE_64	     2U
#define ECAM_PCIE_BAR_MEM_TYPE_RES1	     3U
#define ECAM_PCIE_BAR_UNIMPL_VAL	     0U
#define ECAM_PCIE_BAR_MEM32_INV_VAL	     0xFFFFFFF0U
#define ECAM_PCIE_BAR_MEM64_INV_VAL	     0xFFFFFFFFU
#define ECAM_PCIE_BAR_MEM_PREF_BIT	     (1U << 3)
// Following 5 macros only to be used when ECAM_PCIE_IS_BAR_SPACE_MEM(x) == 1
#define ECAM_PCIE_IS_BAR_TYPE_MEM_PREF(x)                                      \
	(((x) & ECAM_PCIE_BAR_MEM_PREF_BIT) == ECAM_PCIE_BAR_MEM_PREF_BIT)
#define ECAM_PCIE_IS_BAR_TYPE_MEM_NPREF(x)                                     \
	(((x) & ECAM_PCIE_BAR_MEM_PREF_BIT) != ECAM_PCIE_BAR_MEM_PREF_BIT)
#define ECAM_PCIE_IS_BAR_TYPE_MEM32(x)                                         \
	(ECAM_PCIE_GET_BAR_MEM_TYPE(x) == ECAM_PCIE_BAR_MEM_TYPE_32)
#define ECAM_PCIE_IS_BAR_TYPE_MEM64(x)                                         \
	(ECAM_PCIE_GET_BAR_MEM_TYPE(x) == ECAM_PCIE_BAR_MEM_TYPE_64)
#define ECAM_PCIE_IS_BAR_TYPE_MEM_RES(x)                                       \
	((ECAM_PCIE_GET_BAR_MEM_TYPE(x) == ECAM_PCIE_BAR_MEM_TYPE_RES0) ||     \
	 (ECAM_PCIE_GET_BAR_MEM_TYPE(x) == ECAM_PCIE_BAR_MEM_TYPE_RES1))
#define ECAM_PCIE_IS_BAR_UNIMPLEMENTED(x) ((x) == ECAM_PCIE_BAR_UNIMPL_VAL)
#define ECAM_PCIE_GET_HEADER_TYPE(x)	  ((x) & 0x0000007FU)
#define ECAM_PCIE_HEADER_TYPE1		  1U
// PCI/PCIe Capabilities
#define ECAM_PCIE_GET_STD_CAP_ID(x)	  ((x) & 0x000000FFU)
#define ECAM_PCIE_GET_EXT_CAP_ID(x)	  ((x) & 0x0000FFFFU)
#define ECAM_PCIE_GET_EXT_NEXT_CAP_PTR(x) (((x) >> 20) & 0x00000FFFU)
#define ECAM_PCIE_GET_EXT_CAP_VERSION(x)  (((x) >> 16) & 0x0000000FU)

static uintptr_t
pci_function_get_ecam_offset(const pci_function_t *pci_function)
{
	return (uintptr_t)pci_responder_id_raw(pci_function->id)
	       << ECAM_PCIE_FUNC_SHIFT;
}

static error_t
pci_bar_me_create_map(const pci_function_t *pci_function, paddr_t addr,
		      vpci_bar_data_t *bar)
{
	error_t		       err;
	memextent_t	      *aperture_me;
	memextent_ptr_result_t mem_ptr_r;
	size_t		       bar_size;

	bar->memextent = NULL;
	bar_size       = (size_t)util_bit(bar->size_bits);

	aperture_me = pci_host_search_aperture(pci_function->pci_host, addr);

	if (aperture_me == NULL) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	size_result_t offset_r =
		memextent_get_offset_for_pa(aperture_me, addr, bar_size);
	if (offset_r.e != OK) {
		err = offset_r.e;
		goto out;
	}
	// The derived BAR memextent should already be mapped as the parent is
	// mapped.
	mem_ptr_r = memextent_derive(aperture_me, offset_r.r, bar_size,
				     MEMEXTENT_MEMTYPE_DEVICE,
				     PGTABLE_ACCESS_RW, MEMEXTENT_TYPE_BASIC);
	if (mem_ptr_r.e != OK) {
		err = mem_ptr_r.e;
		panic("Failed creation of derived BAR mem extent");
	}

	err	       = OK;
	bar->memextent = mem_ptr_r.r;
out:
	return err;
}

static bool
pci_function_enumerate_io_bar(pci_config_header_t *hdr, index_t bar_idx,
			      vpci_function_t *fn, uint32_t bar_val32,
			      paddr_t *bar_addr)
{
	bool		 is_valid = false;
	vpci_bar_data_t *bar	  = &fn->bars[bar_idx];

	assert(bar_idx < util_array_size(fn->bars));

	// IO BARs reserve bit[1] = 0b1;
	if (ECAM_PCIE_IS_BAR_TYPE_IO_RES(bar_val32)) {
		// BAR is reserved, skip it
		bar->size_bits = 0U;
	} else {
		atomic_store_relaxed(&hdr->bars[bar_idx],
				     (uint32_t)util_mask(32));
		uint32_t bar_temp_val =
			atomic_load_relaxed(&hdr->bars[bar_idx]);

		// Clear encoding bits[1:0]
		bar_temp_val &= ~(uint32_t)ECAM_PCIE_BAR_IO_TYPE_ENCODING_MASK;

		// Check for BAR invalid value (all hardwired to 1)
		if (bar_temp_val == ECAM_PCIE_BAR_IO_INV_VAL) {
			// BAR invalid, skip it
			bar->size_bits = 0U;
		} else {
			// Valid range 16 to 256 bytes only!
			bar->size_bits = (count_t)compiler_msb(
				((uint64_t)~bar_temp_val + 1U));

			// Mask out encoding bits and save the address
			*bar_addr = bar_val32 &
				    (~ECAM_PCIE_BAR_IO_TYPE_ENCODING_MASK);
			bar->address_type = VPCI_BAR_ADDRESS_TYPE_IO;
			bar->access_type  = VPCI_BAR_ACCESS_TYPE_TRAPPED;
			is_valid	  = true;

			// Restore the BAR address
			atomic_store_relaxed(&hdr->bars[bar_idx], bar_val32);
		}
	}

	return is_valid;
}

static bool
pci_function_enumerate_mem_bar(pci_config_header_t *hdr, index_t bar_idx,
			       vpci_function_t *fn, uint32_t bar_val32,
			       paddr_t *bar_addr, bool *is_64bit)
{
	bool		 is_valid  = false;
	vpci_bar_data_t *bar	   = &fn->bars[bar_idx];
	uint32_t	 bar_val64 = 0U;

	assert(bar_idx < util_array_size(fn->bars));

	*is_64bit      = false;
	bar->size_bits = 0U;

	// Memory BARs reserve bit[2:1] = 0b11 or 0b01
	// Or it can also be an unimplemented BAR
	if (ECAM_PCIE_IS_BAR_TYPE_MEM_RES(bar_val32) ||
	    ECAM_PCIE_IS_BAR_UNIMPLEMENTED(bar_val32)) {
		// BAR is reserved, skip it
	} else {
		atomic_store_relaxed(&hdr->bars[bar_idx],
				     (uint32_t)util_mask(32));
		uint32_t bar_temp_val =
			atomic_load_relaxed(&hdr->bars[bar_idx]);

		// Clear encoding bits[3:0]
		bar_temp_val &= ~(uint32_t)ECAM_PCIE_BAR_MEM_TYPE_ENCODING_MASK;

		if (ECAM_PCIE_IS_BAR_TYPE_MEM32(bar_val32)) {
			// 32-bit BAR

			// Check for BAR invalid value (all hardwired to 1)
			if (bar_temp_val == ECAM_PCIE_BAR_MEM32_INV_VAL) {
				// BAR invalid, skip it
			} else {
				// Valid range 128 bytes to 2 GB only!
				bar->size_bits = (count_t)compiler_msb(
					((uint64_t)~bar_temp_val + 1U));
				*bar_addr =
					bar_val32 &
					(~ECAM_PCIE_BAR_MEM_TYPE_ENCODING_MASK);

				// Pref:Non-Pref encoding bit[3] = 0b1:0b0
				if (ECAM_PCIE_IS_BAR_TYPE_MEM_PREF(bar_val32)) {
					bar->address_type =
						VPCI_BAR_ADDRESS_TYPE_MEM_32;
				} else {
					bar->address_type =
						VPCI_BAR_ADDRESS_TYPE_MEM_NP_32;
				}
				bar->access_type = VPCI_BAR_ACCESS_TYPE_TRAPPED;
				is_valid	 = true;
			}
		} else if (ECAM_PCIE_IS_BAR_TYPE_MEM64(bar_val32)) {
			// 64-bit BAR

			assert((bar_idx + 1U) <
			       (uint32_t)util_array_size(fn->bars));

			// Save the value of upper 32-bits of 64-bit BAR
			bar_val64 =
				atomic_load_relaxed(&hdr->bars[bar_idx + 1U]);

			atomic_store_relaxed(&hdr->bars[bar_idx + 1U],
					     (uint32_t)util_mask(32));
			uint32_t bar_temp_val64 =
				atomic_load_relaxed(&hdr->bars[bar_idx + 1U]);

			vpci_bar_data_t *bar64 = &fn->bars[bar_idx + 1U];

			// Check for BAR invalid value (all hardwired to 1)
			if ((bar_temp_val64 == ECAM_PCIE_BAR_MEM64_INV_VAL) &&
			    (bar_temp_val == ECAM_PCIE_BAR_MEM32_INV_VAL)) {
				// BAR invalid, skip it
				bar64->size_bits = 0U;
			} else {
				// Valid range 128 bytes to 2 GB only!
				bar->size_bits = (count_t)compiler_msb(
					((uint64_t)~bar_temp_val + 1U));
				bar64->size_bits = (count_t)compiler_msb(
					((uint64_t)~bar_temp_val64 + 1U));

				*bar_addr =
					bar_val32 &
					(~ECAM_PCIE_BAR_MEM_TYPE_ENCODING_MASK);
				*bar_addr |= (paddr_t)bar_val64 << 32;

				bar->address_type =
					VPCI_BAR_ADDRESS_TYPE_MEM_64;
				bar64->address_type =
					VPCI_BAR_ADDRESS_TYPE_MEM_64_HIGH;

				bar->access_type = VPCI_BAR_ACCESS_TYPE_TRAPPED;
				bar64->access_type =
					VPCI_BAR_ACCESS_TYPE_TRAPPED;
				is_valid  = true;
				*is_64bit = true;
			}
		} else {
			TRACE_AND_LOG(DEBUG, WARN,
				      "ECAM: Unrecognized BAR {:x}", bar_val32);
		}
	}

	if (is_valid) {
		// Restore the BAR address
		atomic_store_relaxed(&hdr->bars[bar_idx], bar_val32);
		if (*is_64bit) {
			atomic_store_relaxed(&hdr->bars[bar_idx + 1U],
					     bar_val64);
		}
	}

	return is_valid;
}

static error_t
pci_function_enumerate_bar(pci_config_header_t *hdr, index_t bar_idx,
			   const pci_function_t *pci_function, bool *is_64bit)
{
	error_t		 err	  = OK;
	bool		 is_valid = false;
	paddr_t		 bar_addr;
	vpci_function_t *fn	   = pci_function->pci_vdevice.functions[0];
	uint32_t	 bar_val32 = atomic_load_relaxed(&hdr->bars[bar_idx]);

	*is_64bit = false;

	// PCIe Exp. Base Specs 7.0 - Section: 7.5.1.2.1 Base Address
	// Registers
	if (ECAM_PCIE_IS_BAR_SPACE_IO(bar_val32)) {
		is_valid = pci_function_enumerate_io_bar(hdr, bar_idx, fn,
							 bar_val32, &bar_addr);
	} else {
		is_valid = pci_function_enumerate_mem_bar(
			hdr, bar_idx, fn, bar_val32, &bar_addr, is_64bit);
	}

	if (is_valid) {
		err = pci_bar_me_create_map(pci_function, bar_addr,
					    &fn->bars[bar_idx]);
	}

	return err;
}

static error_t
pci_function_enumerate_bars(pci_config_header_t	 *hdr,
			    const pci_function_t *pci_function)
{
	error_t		 err	  = OK;
	vpci_function_t *fn	  = pci_function->pci_vdevice.functions[0];
	bool		 is_64bit = false;
	index_t		 bar_idx;

	pci_function_command_t cmd	= atomic_load_relaxed(&hdr->command);
	pci_function_command_t cmd_stop = cmd;
	// Disable IO, Memory Space enable bits
	pci_function_command_set_io_space(&cmd_stop, false);
	pci_function_command_set_mem_space(&cmd_stop, false);
	atomic_store_relaxed(&hdr->command, cmd_stop);

	bar_idx = 0U;
	while (bar_idx < util_array_size(fn->bars)) {
		err = pci_function_enumerate_bar(hdr, bar_idx, pci_function,
						 &is_64bit);
		if (err != OK) {
			break;
		}

		bar_idx++;
		if (is_64bit) {
			bar_idx++;
		}
	}

	// Restore the original value of cmd
	atomic_store_relaxed(&hdr->command, cmd);

	return err;
}

error_t
pci_host_function_prepare(uintptr_t ecam_base, size_t ecam_size,
			  pci_function_t *pci_function)
{
	error_t	  err;
	uintptr_t function_base =
		ecam_base + pci_function_get_ecam_offset(pci_function);

	assert((ecam_base + ecam_size) > function_base);
	assert(util_is_baligned(function_base, sizeof(uint32_t)));

	pci_function->base_addr	 = function_base;
	pci_config_header_t *hdr = (pci_config_header_t *)function_base;
	uint8_t header_type	 = atomic_load_relaxed(&hdr->header_type);

	if (ECAM_PCIE_GET_HEADER_TYPE(header_type) == ECAM_PCIE_HEADER_TYPE1) {
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vpci_function_t *fn = &pci_function->fn0;
	*fn		    = (vpci_function_t){ 0 };

	// Only one function is implemented in each pci_function
	pci_function->pci_vdevice.functions[0] = fn;

	fn->type = VPCI_FUNCTION_TYPE_PHYSICAL;

	fn->product_id	      = atomic_load_relaxed(&hdr->product_id);
	fn->vendor_id	      = atomic_load_relaxed(&hdr->vendor_id);
	fn->device_revision   = atomic_load_relaxed(&hdr->device_revision);
	fn->subsys_product_id = atomic_load_relaxed(&hdr->subsys_id);
	fn->subsys_vendor_id  = atomic_load_relaxed(&hdr->subsys_vendor_id);
	fn->device_class      = atomic_load_relaxed(&hdr->device_class);
	fn->device_subclass   = atomic_load_relaxed(&hdr->device_subclass);
	fn->device_interface  = atomic_load_relaxed(&hdr->device_interface);
	fn->interrupt_pin     = atomic_load_relaxed(&hdr->interrupt_pin);
	fn->interrupt_line    = atomic_load_relaxed(&hdr->interrupt_line);
	fn->command	      = atomic_load_relaxed(&hdr->command);

	pci_function_status_t status = atomic_load_relaxed(&hdr->status);
	atomic_store_relaxed(&fn->status, status);

	fn->function_index = 0;
	fn->device	   = &pci_function->pci_vdevice;
	fn->capabilities   = NULL;

	err = pci_function_enumerate_bars(hdr, pci_function);
out:
	return err;
}

error_t
pci_function_passthrough(pci_function_t *pci_function)
{
	error_t err = OK;

	assert(pci_function != NULL);
	vpci_function_t *fn	   = pci_function->pci_vdevice.functions[0];
	addrspace_t	*addrspace = pci_function->pci_host->addrspace;

	for (index_t bar_idx = 0U; bar_idx < util_array_size(fn->bars);
	     bar_idx++) {
		vpci_bar_data_t *bar = &fn->bars[bar_idx];

		if (bar->size_bits == 0U) {
			// BAR is unimplmeneted, skip it
			continue;
		}

		// Unmap the BAR from the pci_host's addrspace
		err = memextent_unmap_whole_extent(
			bar->memextent, addrspace,
			addrspace_map_flags_default());
		if (err != OK) {
			goto out;
		}
	}

	// The function has been successfully unmapped
	//  and now can be attached to the vpci
	pci_function->passedthrough = true;
	fn->type		    = VPCI_FUNCTION_TYPE_PASSTHROUGH;

out:
	return err;
}

error_t
pci_function_add_capability(pci_function_t	    *pci_function,
			    pci_function_cap_data_t *cap_data)
{
	error_t err = OK;

	assert(pci_function != NULL);
	vpci_function_t	       *fn = pci_function->pci_vdevice.functions[0];
	vpci_capability_data_t *capability = &cap_data->base;
	_Atomic uint32_t       *cap_id_ptr =
		(_Atomic uint32_t *)(pci_function->base_addr +
				     cap_data->base.offset);
	uint32_t reg_val = atomic_load_relaxed(cap_id_ptr);

	if (cap_data->base.type == PCI_CAPABILITY_TYPE_STANDARD) {
		// Standard capability is 8 bits.
		capability->cap_id = (uint8_t)ECAM_PCIE_GET_STD_CAP_ID(reg_val);
	} else {
		// Extended capability is 16 bits.
		capability->cap_id =
			(uint16_t)ECAM_PCIE_GET_EXT_CAP_ID(reg_val);
		capability->version =
			(uint8_t)ECAM_PCIE_GET_EXT_CAP_VERSION(reg_val);
	}
	capability->next = NULL;
	if (fn->capabilities != NULL) {
		vpci_capability_data_t *node = fn->capabilities;
		while (node != NULL) {
			// Verify if the given capability does not
			// overlap with an existing one
			if ((capability->offset <
			     (node->offset + node->size)) &&
			    (node->offset <
			     (capability->offset + capability->size))) {
				err = ERROR_ARGUMENT_INVALID;
				goto out;
			}

			// check if we are on the last node
			if (node->next == NULL) {
				node->next = capability;
				break;
			}
			node = node->next;
		}
	} else {
		fn->capabilities = capability;
	}
out:
	return err;
}
