// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <compiler.h>
#include <trace.h>
#include <util.h>

#include "gicv3_its.h"
#include "panic.h"
#include "useraccess.h"
#include "vgic_its_base.h"

void
vgic_its_copy_in_ctes(vgic_its_t *vgic_its)
{
	error_t err	  = ERROR_ADDR_INVALID;
	size_t	page_size = gicv3_its_page_size(
		GITS_BASER_get_Page_Size(&vgic_its->collection_table_base));
	size_t size =
		((size_t)GITS_BASER_get_Size(&vgic_its->collection_table_base) +
		 1U) *
		page_size;

	size = GITS_BASER_get_Valid(&vgic_its->collection_table_base)
		       ? util_min(size, sizeof(vgic_its->collection_table))
		       : 0U;

	if (size > 0U) {
		err = useraccess_copy_from_guest_ipa(
			      vgic_its->addrspace, vgic_its->collection_table,
			      sizeof(vgic_its->collection_table),
			      GITS_BASER_get_Physical_Address(
				      &vgic_its->collection_table_base),
			      size, false, false)
			      .e;

		if (err != OK) {
			// Failed to read the collection table; zero the cache
			// FIXME: QC Gunyah issue #143
			errno_t err_mem = memset_s(vgic_its->collection_table,
						   size, 0, size);
			if (err_mem != 0) {
				panic("Error in memset_s operation!");
			}
		}
	}

	if (size < sizeof(vgic_its->collection_table)) {
		// Zero the remainder of the cached collection table.
		errno_t err_mem =
			memset_s((uint8_t *)vgic_its->collection_table + size,
				 sizeof(vgic_its->collection_table) - size, 0,
				 sizeof(vgic_its->collection_table) - size);
		if (err_mem != 0) {
			panic("Error in memset_s operation!");
		}
	}

	// Initially we mark all the collections as empty. This bit will be
	// cleared as soon as any event is mapped to the collection.
	//
	// This allows us to optimise for the common (Linux driver) case that
	// MAPC is executed once at startup before any events are mapped and is
	// never used again, so it doesn't need to search any tables.
	for (count_t i = 0U; i < sizeof(vgic_its->collection_table); i++) {
		vgic_its_cte_set_known_empty(&vgic_its->collection_table[i],
					     true);
	}
}

void
vgic_its_copy_out_ctes(vgic_its_t *vgic_its)
{
	error_t err	  = ERROR_ADDR_INVALID;
	size_t	page_size = gicv3_its_page_size(
		GITS_BASER_get_Page_Size(&vgic_its->collection_table_base));
	size_t size =
		((size_t)GITS_BASER_get_Size(&vgic_its->collection_table_base) +
		 1U) *
		page_size;

	size = GITS_BASER_get_Valid(&vgic_its->collection_table_base) ? size
								      : 0U;

	if (size > 0U) {
		err = useraccess_copy_to_guest_ipa(
			      vgic_its->addrspace,
			      GITS_BASER_get_Physical_Address(
				      &vgic_its->collection_table_base),
			      size, vgic_its->collection_table,
			      sizeof(vgic_its->collection_table), false, false)
			      .e;
	}

	if (err != OK) {
		// FIXME: QC Gunyah issue #143
	}
}

static vmaddr_result_t
vgic_its_lookup_l2_table(vgic_its_t *vgic_its, vmaddr_t base, size_t size,
			 index_t index)
{
	vmaddr_result_t		    ret = { 0 };
	GITS_BASER_Indirect_entry_t entry;

	size_t offset = index * sizeof(entry);
	if ((size < sizeof(entry)) || (offset > (size - sizeof(entry)))) {
		// Index is beyond the end of the table
		ret = vmaddr_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	ret.e = useraccess_copy_from_guest_ipa(
			vgic_its->addrspace, &entry, sizeof(entry),
			base + offset, sizeof(GITS_BASER_Indirect_entry_t),
			false, false)
			.e;

	if (ret.e != OK) {
		goto out;
	}

	if (!GITS_BASER_Indirect_entry_get_Valid(&entry)) {
		// No L2 table is present
		ret = vmaddr_result_error(ERROR_ADDR_INVALID);
	} else {
		ret = vmaddr_result_ok(
			GITS_BASER_Indirect_entry_get_Physical_Address(&entry));
	}

out:
	return ret;
}

static vmaddr_result_t
vgic_its_lookup_table_entry(vgic_its_t *vgic_its, GITS_BASER_t baser,
			    index_t index)
{
	vmaddr_result_t ret;

	size_t page_size =
		gicv3_its_page_size(GITS_BASER_get_Page_Size(&baser));
	size_t	 size = ((size_t)GITS_BASER_get_Size(&baser) + 1U) * page_size;
	size_t	 entry_size = GITS_BASER_get_Entry_Size(&baser) + 1U;
	vmaddr_t base	    = GITS_BASER_get_Physical_Address(&baser);

	if (GITS_BASER_get_Indirect(&baser)) {
		count_t l2_table_entries = (count_t)(page_size / entry_size);

		ret = vgic_its_lookup_l2_table(vgic_its, base, size,
					       index / l2_table_entries);
		if (ret.e == OK) {
			size_t offset =
				((size_t)index % (size_t)l2_table_entries) *
				entry_size;
			ret = vmaddr_result_ok(ret.r + offset);
		}
	} else {
		size_t offset = index * entry_size;
		if (offset >= size) {
			// Index is beyond the end of the table
			ret = vmaddr_result_error(ERROR_ARGUMENT_INVALID);
		} else {
			ret = vmaddr_result_ok(base + offset);
		}
	}

	return ret;
}

vgic_its_dte_result_t
vgic_its_copy_in_dte(vgic_its_t *vgic_its, platform_msi_device_id_t device_id)
{
	vgic_its_dte_result_t ret = { 0 };

	vmaddr_result_t vmaddr_r = vgic_its_lookup_table_entry(
		vgic_its, vgic_its->device_table_base, (index_t)device_id);

	if (vmaddr_r.e == OK) {
		ret.e = useraccess_copy_from_guest_ipa(vgic_its->addrspace,
						       &ret.r, sizeof(ret.r),
						       vmaddr_r.r,
						       sizeof(vgic_its_dte_t),
						       false, false)
				.e;
	} else {
		ret = vgic_its_dte_result_error(vmaddr_r.e);
	}

	return ret;
}

error_t
vgic_its_copy_out_dte(vgic_its_t *vgic_its, platform_msi_device_id_t device_id,
		      vgic_its_dte_t dte)
{
	error_t err;

	vmaddr_result_t vmaddr_r = vgic_its_lookup_table_entry(
		vgic_its, vgic_its->device_table_base, (index_t)device_id);

	if (vmaddr_r.e == OK) {
		err = useraccess_copy_to_guest_ipa(vgic_its->addrspace,
						   vmaddr_r.r,
						   sizeof(vgic_its_dte_t), &dte,
						   sizeof(dte), false, false)
			      .e;
	} else {
		err = vmaddr_r.e;
	}

	return err;
}

vgic_its_itte_result_t
vgic_its_copy_in_itte(vgic_its_t *vgic_its, vgic_its_dte_t dte,
		      platform_msi_event_id_t event_id)
{
	vgic_its_itte_result_t ret = { 0 };

	assert(vgic_its_dte_get_valid(&dte));

	count_t table_bits = vgic_its_dte_get_itt_size(&dte) + 1U;
	if (table_bits > VGIC_ITS_EVENT_BITS) {
		// Table is larger than it should be, probably because the VM
		// has been messing with the DTE
		ret = vgic_its_itte_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	count_t table_entries = (count_t)util_bit(table_bits);
	if (event_id >= table_entries) {
		// Out-of-range event number
		ret = vgic_its_itte_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	vmaddr_t base	= vgic_its_dte_get_itt_addr(&dte);
	size_t	 offset = event_id * sizeof(ret.r);

	ret.e = useraccess_copy_from_guest_ipa(vgic_its->addrspace, &ret.r,
					       sizeof(ret.r), base + offset,
					       sizeof(vgic_its_itte_t), false,
					       false)
			.e;

out:
	return ret;
}

error_t
vgic_its_copy_out_itte(vgic_its_t *vgic_its, vgic_its_dte_t dte,
		       platform_msi_event_id_t event_id, vgic_its_itte_t itte)
{
	error_t err;

	assert(vgic_its_dte_get_valid(&dte));

	count_t table_bits = vgic_its_dte_get_itt_size(&dte) + 1U;
	if (table_bits > VGIC_ITS_EVENT_BITS) {
		// Table is larger than it should be, probably because the VM
		// has been messing with the DTE
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	count_t table_entries = (count_t)util_bit(table_bits);
	if (event_id >= table_entries) {
		// Out-of-range event number
		err = ERROR_ARGUMENT_INVALID;
		goto out;
	}

	vmaddr_t base	= vgic_its_dte_get_itt_addr(&dte);
	size_t	 offset = event_id * sizeof(itte);

	err = useraccess_copy_to_guest_ipa(vgic_its->addrspace, base + offset,
					   sizeof(vgic_its_itte_t), &itte,
					   sizeof(itte), false, false)
		      .e;

out:
	return err;
}

vgic_its_dte_result_t
vgic_its_lookup_dte(vgic_its_t *vgic_its, platform_msi_device_id_t device)
{
	vgic_its_dte_result_t dte_r;

	assert(vgic_its != NULL);

	dte_r = vgic_its_copy_in_dte(vgic_its, device);

	if (dte_r.e != OK) {
		// Device is out of range or table access faulted
		TRACE(VGIC, WARN,
		      "VITS {:#x}: DTE access fault for device {:d}",
		      (uintptr_t)vgic_its, device);
		goto out;
	}
	if (!vgic_its_dte_get_valid(&dte_r.r)) {
		// Device is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: DTE invalid for device {:d} ({:#x})",
		      (uintptr_t)vgic_its, device, dte_r.r.bf[0]);
		dte_r = vgic_its_dte_result_error(ERROR_ARGUMENT_INVALID);
	}

out:
	return dte_r;
}

vgic_its_itte_result_t
vgic_its_lookup_itte(vgic_its_t *vgic_its, platform_msi_device_id_t device,
		     vgic_its_dte_t dte, platform_msi_event_id_t event,
		     bool valid_only)
{
	vgic_its_itte_result_t itte_r;

	assert(vgic_its != NULL);

	itte_r = vgic_its_copy_in_itte(vgic_its, dte, event);
	if (itte_r.e != OK) {
		// Event is out of range or table access faulted
		TRACE(VGIC, WARN,
		      "VITS {:#x}: ITTE access fault for event {:d}/{:d}",
		      (uintptr_t)vgic_its, device, event);
		goto out;
	}

	if (valid_only && !vgic_its_itte_get_valid(&itte_r.r)) {
		// Event is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: ITTE invalid for event {:d}/{:d} ({:#x})",
		      (uintptr_t)vgic_its, device, event, itte_r.r.bf[0]);
		itte_r = vgic_its_itte_result_error(ERROR_ARGUMENT_INVALID);
	}

out:
	return itte_r;
}

vgic_its_cte_result_t
vgic_its_lookup_cte(vgic_its_t *vgic_its, vgic_its_itte_t itte,
		    platform_msi_device_id_t device,
		    platform_msi_event_id_t  event)
{
	vgic_its_ic_id_t      icid;
	vgic_its_cte_t	      cte;
	vgic_its_cte_result_t ret;

	if (!vgic_its_itte_get_valid(&itte)) {
		ret = vgic_its_cte_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	icid = vgic_its_itte_get_icid(&itte);

	if (icid >= util_array_size(vgic_its->collection_table)) {
		// Collection is out of range
		ret = vgic_its_cte_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	cte = vgic_its->collection_table[icid];
	if (!vgic_its_cte_get_valid(&cte)) {
		// Collection is not mapped
		TRACE(VGIC, WARN,
		      "VITS {:#x}: IC {:d} invalid for event {:d}/{:d} ({:#x})",
		      (uintptr_t)vgic_its, icid, device, event, cte.bf[0]);
		ret = vgic_its_cte_result_error(ERROR_ARGUMENT_INVALID);
		goto out;
	}

	ret = vgic_its_cte_result_ok(cte);
out:

	return ret;
}
