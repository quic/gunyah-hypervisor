// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>
#include <string.h>

#include <hypcall_def.h>
#include <hypcontainers.h>
#include <hyprights.h>

#include <addrspace.h>
#include <atomic.h>
#include <compiler.h>
#include <cspace.h>
#include <cspace_lookup.h>
#include <list.h>
#include <memextent.h>
#include <object.h>
#include <partition.h>
#include <partition_alloc.h>
#include <spinlock.h>
#include <util.h>

#include "pci_host.h"

static size_result_t
pci_host_aperture_check(addrspace_t *aspace, vmaddr_t base, size_t size,
			size_t min_size, size_t max_size, index_t size_bits)
{
	size_result_t ret;

	if ((size < min_size) || (size > max_size)) {
		ret = size_result_error(ERROR_ARGUMENT_SIZE);
		goto out;
	}
	if (!util_is_p2aligned(base, size_bits)) {
		ret = size_result_error(ERROR_ARGUMENT_ALIGNMENT);
		goto out;
	}
	if (util_add_overflows(base, size)) {
		ret = size_result_error(ERROR_ADDR_OVERFLOW);
		goto out;
	}

	error_t err = addrspace_check_range(aspace, base, size);
	if (err != OK) {
		ret = size_result_error(err);
		goto out;
	}
	ret = size_result_ok(size);
out:
	return ret;
}

error_t
hypercall_pci_host_configure(cap_id_t pci_host_cap, cap_id_t aspace_cap,
			     cap_id_t cam_me_cap, vmaddr_t cam_base,
			     pci_host_option_flags_t options)
{
	error_t	      ret;
	cspace_t     *cspace;
	object_type_t type;

	cspace		      = cspace_get_self();
	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, pci_host_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		ret = o.e;
		goto out_pci_host_released;
	}
	if (type != OBJECT_TYPE_PCI_HOST) {
		ret = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		object_put(type, o.r);
		goto out_pci_host_released;
	}

	pci_host_t *pci_host = o.r.pci_host;
	spinlock_acquire(&pci_host->header.lock);

	if (atomic_load_relaxed(&pci_host->header.state) != OBJECT_STATE_INIT) {
		ret = ERROR_OBJECT_STATE;
		goto out_pci_host_ref;
	}

	if (!pci_host_option_flags_is_clean(options)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_host_ref;
	}
	bool pcie = pci_host_option_flags_get_pcie(&options);
#if PCI_HOST_PCIE_ONLY
	if (!pcie) {
		// Conventional PCI is not implemented yet
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_host_ref;
	}
#else
#error Conventional PCI not implemented
#endif /* PCI_HOST_PCIE_ONLY */

	addrspace_ptr_result_t aspace_r = cspace_lookup_addrspace(
		cspace, aspace_cap, CAP_RIGHTS_ADDRSPACE_MAP);
	if (compiler_unexpected(aspace_r.e != OK)) {
		ret = aspace_r.e;
		goto out_pci_host_ref;
	}

	memextent_ptr_result_t mem_r = cspace_lookup_memextent(
		cspace, cam_me_cap, CAP_RIGHTS_MEMEXTENT_MAP);

	if (compiler_unexpected(mem_r.e != OK)) {
		ret = mem_r.e;
		goto out_aspace_ref;
	}

	size_result_t mem_size_r = memextent_get_mapped_size(mem_r.r);
	if (mem_size_r.e != OK) {
		ret = mem_size_r.e;
		goto out_memextent_ref;
	}
	// Size must be power-of-2 between 2MB and 256MB
	size_result_t cam_size_r = pci_host_aperture_check(
		aspace_r.r, cam_base, mem_size_r.r, util_bit(pcie ? 21U : 17U),
		util_bit(pcie ? 28U : 24U), pcie ? 12U : 8U);
	if (cam_size_r.e != OK) {
		ret = cam_size_r.e;
		goto out_memextent_ref;
	}

	// Update the object pci_host
	if (pci_host->addrspace != NULL) {
		object_put_addrspace(pci_host->addrspace);
	}

	pci_host->addrspace = object_get_addrspace_additional(aspace_r.r);
	pci_host->lockdown  = pci_host_option_flags_get_lockdown(&options);
	pci_host->pcie	    = pci_host_option_flags_get_pcie(&options);
	pci_host->cam_base  = cam_base;
	pci_host->cam_size  = cam_size_r.r;
	pci_host->memextent = object_get_memextent_additional(mem_r.r);
	pci_host->state	    = PCI_HOST_LOCKDOWN_STATE_PERMISSIVE;

	ret = OK;
out_memextent_ref:
	if (mem_r.e == OK) {
		object_put_memextent(mem_r.r);
	}
out_aspace_ref:
	object_put_addrspace(aspace_r.r);
out_pci_host_ref:
	spinlock_release(&pci_host->header.lock);
	object_put_pci_host(pci_host);
out_pci_host_released:
	return ret;
}

error_t
hypercall_pci_host_add_aperture(cap_id_t pci_host_cap, cap_id_t mem_me_cap,
				vmaddr_t mem_base)
{
	error_t	    ret;
	cspace_t   *cspace;
	pci_host_t *pci_host;

	cspace				 = cspace_get_self();
	pci_host_ptr_result_t pci_host_r = cspace_lookup_pci_host_any(
		cspace, pci_host_cap, CAP_RIGHTS_PCI_HOST_OBJECT_ACTIVATE);
	if (compiler_unexpected(pci_host_r.e != OK)) {
		ret = pci_host_r.e;
		goto out_pci_host_released;
	}

	pci_host = pci_host_r.r;

	memextent_ptr_result_t mem_r = cspace_lookup_memextent(
		cspace, mem_me_cap, CAP_RIGHTS_MEMEXTENT_MAP);
	if (compiler_unexpected(mem_r.e != OK)) {
		ret = mem_r.e;
		goto out_pci_host_put;
	}

	size_result_t mem_size_r = memextent_get_mapped_size(mem_r.r);
	if (mem_size_r.e != OK) {
		ret = mem_size_r.e;
		goto out_pci_host_put;
	}

	size_result_t size_r = pci_host_aperture_check(
		pci_host->addrspace, mem_base, mem_size_r.r, 20U, SIZE_MAX,
		pci_host->pcie ? 12U : 8U);
	if (size_r.e != OK) {
		ret = size_r.e;
		goto out_pci_host_put;
	}

	partition_t *partition = pci_host->header.partition;
	size_t	     size      = sizeof(pci_host_aperture_data_t);

	void_ptr_result_t alloc_r = partition_alloc(
		partition, size, alignof(pci_host_aperture_data_t));
	if (alloc_r.e != OK) {
		ret = ERROR_NOMEM;
		goto out_pci_host_put;
	}

	pci_host_aperture_data_t *d = (pci_host_aperture_data_t *)alloc_r.r;
	(void)memset_s(d, sizeof(*d), 0, sizeof(*d));

	d->vmaddr    = mem_base;
	d->memextent = object_get_memextent_additional(mem_r.r);
	d->pci_host  = pci_host;

	spinlock_acquire(&pci_host->header.lock);

	// add aperture must be called before the object is active
	if (atomic_load_relaxed(&pci_host->header.state) != OBJECT_STATE_INIT) {
		ret = ERROR_OBJECT_STATE;
		partition_free(partition, d, sizeof(pci_host_aperture_data_t));
		goto out_pci_host_ref;
	}

	list_insert_at_tail(&pci_host->aperture_list, &d->list_node);
	ret = OK;
out_pci_host_ref:
	spinlock_release(&pci_host->header.lock);
out_pci_host_put:
	object_put_memextent(mem_r.r);
	object_put_pci_host(pci_host);
out_pci_host_released:
	return ret;
}

error_t
hypercall_pci_host_set_lockdown(cap_id_t		  pci_host_cap,
				pci_host_lockdown_state_t lockdown_state)
{
	error_t	    ret = OK;
	cspace_t   *cspace;
	pci_host_t *pci_host;

	cspace				 = cspace_get_self();
	pci_host_ptr_result_t pci_host_r = cspace_lookup_pci_host(
		cspace, pci_host_cap, CAP_RIGHTS_PCI_HOST_SET_LOCKDOWN);
	if (compiler_unexpected(pci_host_r.e != OK)) {
		ret = pci_host_r.e;
		goto out_pci_host_released;
	}

	pci_host = pci_host_r.r;
	spinlock_acquire(&pci_host->header.lock);

	if (atomic_load_relaxed(&pci_host->header.state) !=
	    OBJECT_STATE_ACTIVE) {
		ret = ERROR_OBJECT_STATE;
		goto out_pci_host_ref;
	}

	if (!pci_host->lockdown) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_pci_host_ref;
	}

	switch (lockdown_state) {
	case PCI_HOST_LOCKDOWN_STATE_SCANNING:
		if (pci_host->state != PCI_HOST_LOCKDOWN_STATE_PERMISSIVE) {
			// No other transition is allowed
			ret = ERROR_DENIED;
			goto out_pci_host_ref;
		}

		ret = pci_host_transition_scanning_state(pci_host);
		if (ret != OK) {
			goto out_pci_host_ref;
		}
		// State transition is successful
		pci_host->state = PCI_HOST_LOCKDOWN_STATE_SCANNING;
		break;
	case PCI_HOST_LOCKDOWN_STATE_RESTRICTED:
		if (pci_host->state != PCI_HOST_LOCKDOWN_STATE_SCANNING) {
			// No other transition is allowed
			ret = ERROR_DENIED;
			goto out_pci_host_ref;
		}

		// Scanning to Restricted is only allowed if
		// all the derived PCIe Functions are in ACTIVE
		// state.
		if (pci_host_search_inactive_function(pci_host)) {
			// Atleast one of the function is inactive
			ret = ERROR_BUSY;
			goto out_pci_host_ref;
		}

		ret = pci_host_transition_restricted_state(pci_host);
		if (ret != OK) {
			goto out_pci_host_ref;
		}
		// State transition is successful
		pci_host->state = PCI_HOST_LOCKDOWN_STATE_RESTRICTED;
		break;
	case PCI_HOST_LOCKDOWN_STATE_PERMISSIVE:
		if (pci_host->state == PCI_HOST_LOCKDOWN_STATE_PERMISSIVE) {
			ret = ERROR_DENIED;
			goto out_pci_host_ref;
		}

		// Scanning/Restricted to Permissive is only allowed if
		// no PCIe Function object exists
		if (!list_is_empty(&pci_host->function_list)) {
			ret = ERROR_BUSY;
			goto out_pci_host_ref;
		}

		ret = pci_host_transition_permissive_state(pci_host);
		if (ret != OK) {
			goto out_pci_host_ref;
		}
		// State transition is successful
		pci_host->state = PCI_HOST_LOCKDOWN_STATE_PERMISSIVE;
		break;
	default:
		ret = ERROR_ARGUMENT_INVALID;
		// assert
		break;
	}
out_pci_host_ref:
	spinlock_release(&pci_host->header.lock);
	object_put_pci_host(pci_host);
out_pci_host_released:
	return ret;
}

error_t
hypercall_pci_function_configure(cap_id_t		     pci_function_cap,
				 cap_id_t		     pci_host_cap,
				 pci_responder_id_t	     responder_id,
				 pci_function_option_flags_t options)
{
	error_t	      ret;
	cspace_t     *cspace;
	object_type_t type;

	cspace		      = cspace_get_self();
	object_ptr_result_t o = cspace_lookup_object_any(
		cspace, pci_function_cap, CAP_RIGHTS_GENERIC_OBJECT_ACTIVATE,
		&type);
	if (compiler_unexpected(o.e != OK)) {
		ret = o.e;
		goto out_pci_function_released;
	}
	if (type != OBJECT_TYPE_PCI_FUNCTION) {
		ret = ERROR_CSPACE_WRONG_OBJECT_TYPE;
		object_put(type, o.r);
		goto out_pci_function_released;
	}

	pci_function_t *pci_function = o.r.pci_function;
	spinlock_acquire(&pci_function->header.lock);

	if (atomic_load_relaxed(&pci_function->header.state) !=
	    OBJECT_STATE_INIT) {
		ret = ERROR_OBJECT_STATE;
		goto out_pci_function_ref;
	}

	partition_t *partition = pci_function->header.partition;
	size_t	     size      = sizeof(pci_host_function_data_t);

	void_ptr_result_t alloc_r = partition_alloc(
		partition, size, alignof(pci_host_function_data_t));
	if (alloc_r.e != OK) {
		ret = ERROR_NOMEM;
		goto out_pci_function_ref;
	}

	if (!pci_function_option_flags_is_clean(options)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_function_ref;
	}

	// For now we do not support SR-IOV
	if (pci_function_option_flags_get_sr_iov_vf(&options)) {
		ret = ERROR_UNIMPLEMENTED;
		goto out_pci_function_ref;
	}

	pci_function->option_flags = options;

	// Initialize function passthrough
	pci_function->passedthrough = false;

	if (!pci_responder_id_is_clean(responder_id)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_function_ref;
	}

	pci_function->id = responder_id;

	pci_host_function_data_t *d = (pci_host_function_data_t *)alloc_r.r;
	(void)memset_s(d, sizeof(*d), 0, sizeof(*d));

	d->function = object_get_pci_function_additional(pci_function);
	d->cap_id   = pci_function_cap;

	pci_host_t	     *pci_host;
	pci_host_ptr_result_t pci_host_r = cspace_lookup_pci_host(
		cspace, pci_host_cap, CAP_RIGHTS_PCI_HOST_ALL);
	if (compiler_unexpected(pci_host_r.e != OK)) {
		ret = pci_host_r.e;
		goto out_pci_function_ref;
	}
	pci_host = pci_host_r.r;

	spinlock_acquire_nopreempt(&pci_host->header.lock);

	if (atomic_load_relaxed(&pci_host->header.state) !=
	    OBJECT_STATE_ACTIVE) {
		ret = ERROR_OBJECT_STATE;
		goto out_pci_host_ref;
	}

	if ((pci_host->state == PCI_HOST_LOCKDOWN_STATE_SCANNING) ||
	    (!pci_host->lockdown)) {
		if (pci_host_search_function(pci_host, pci_function)) {
			ret = ERROR_BUSY;
			goto out_pci_host_ref;
		}
		pci_function->pci_host =
			object_get_pci_host_additional(pci_host);
	} else {
		ret = ERROR_DENIED;
		goto out_pci_host_ref;
	}

	list_insert_at_tail(&pci_host->function_list, &d->list_node);
	ret = OK;
out_pci_host_ref:
	spinlock_release_nopreempt(&pci_host->header.lock);
	object_put_pci_host(pci_host);
	if ((ret != OK) && (d != NULL)) {
		partition_free(partition, d, sizeof(pci_host_aperture_data_t));
	}
out_pci_function_ref:
	spinlock_release(&pci_function->header.lock);
	object_put_pci_function(pci_function);
out_pci_function_released:
	return ret;
}

error_t
hypercall_pci_function_add_capability(cap_id_t pci_function_cap, size_t offset,
				      size_t			    size,
				      pci_capability_access_flags_t flags)
{
	error_t		ret;
	cspace_t       *cspace;
	pci_function_t *pci_function;

	cspace					 = cspace_get_self();
	pci_function_ptr_result_t pci_function_r = cspace_lookup_pci_function(
		cspace, pci_function_cap, CAP_RIGHTS_PCI_FUNCTION_ALL);
	if (compiler_unexpected(pci_function_r.e != OK)) {
		ret = pci_function_r.e;
		goto out_pci_function_released;
	}

	pci_function = pci_function_r.r;
	spinlock_acquire(&pci_function->header.lock);

	pci_host_t *pci_host = pci_function->pci_host;

	// Capability can only be added if the function has not
	// been passed-through yet. Also, the attached pci_host must
	// have lockdown enabled, and must be in Scanning state.
	if ((pci_function->passedthrough) ||
	    ((pci_host->lockdown) &&
	     (pci_host->state != PCI_HOST_LOCKDOWN_STATE_SCANNING))) {
		ret = ERROR_OBJECT_STATE;
		goto out_pci_function_ref;
	}

	// Offset must be a multiple of 4
	if (!util_is_p2aligned(offset, 2)) {
		ret = ERROR_ARGUMENT_ALIGNMENT;
		goto out_pci_function_ref;
	}

	// Offset and size must be within the range 0x40-0xFF or 0x100-0xFFF
	if ((size == 0U) || (util_add_overflows(offset, size)) ||
	    (((offset < PCI_HOST_STD_CAP_OFFSET_START) ||
	      ((offset + size - 1U) > PCI_HOST_STD_CAP_OFFSET_END)) &&
	     ((offset < PCI_HOST_EXT_CAP_OFFSET_START) ||
	      ((offset + size - 1U) > PCI_HOST_EXT_CAP_OFFSET_END)))) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_function_ref;
	}

	if (!pci_capability_access_flags_is_clean(flags)) {
		ret = ERROR_ARGUMENT_INVALID;
		goto out_pci_function_ref;
	}

	// If passthrough visibile/writeable, pci_function passthrough must be
	// configured
	if ((pci_capability_access_flags_get_passthrough_visible(&flags) ||
	     pci_capability_access_flags_get_passthrough_writable(&flags)) &&
	    (!pci_function_option_flags_get_passthrough(
		    &pci_function->option_flags))) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_pci_function_ref;
	}

	// If lockdown visible/writeable, pci_host lockdown must be configured
	if ((pci_capability_access_flags_get_lockdown_visible(&flags) ||
	     pci_capability_access_flags_get_lockdown_writable(&flags)) &&
	    (!pci_host->lockdown)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_pci_function_ref;
	}

	partition_t	 *partition = pci_function->header.partition;
	void_ptr_result_t alloc_r =
		partition_alloc(partition, sizeof(pci_function_cap_data_t),
				alignof(pci_function_cap_data_t));
	if (alloc_r.e != OK) {
		ret = alloc_r.e;
		goto out_pci_function_ref;
	}

	pci_function_cap_data_t *cap_data =
		(pci_function_cap_data_t *)alloc_r.r;
	(void)memset_s(cap_data, sizeof(*cap_data), 0, sizeof(*cap_data));

	cap_data->flags	    = flags;
	cap_data->base.size = (uint8_t)size;
	// The offset may not be the same in the virtual ECAM as it is in
	// the physical ECAM. For now, all capabilities are mapped 1:1.
	// FIXME: QC Gunyah issue #293
	cap_data->base.offset = (uint8_t)offset;

	// Lifetime of a capability is same a function
	cap_data->function = pci_function;

	ret = pci_function_add_capability(pci_function, cap_data);
	if ((ret != OK) && (cap_data != NULL)) {
		partition_free(partition, cap_data,
			       sizeof(pci_function_cap_data_t));
	}
out_pci_function_ref:
	spinlock_release(&pci_function->header.lock);
	object_put_pci_function(pci_function);
out_pci_function_released:
	return ret;
}

error_t
hypercall_pci_function_set_passthrough(cap_id_t pci_function_cap, bool enable)
{
	error_t		ret = OK;
	cspace_t       *cspace;
	pci_function_t *pci_function;

	cspace					 = cspace_get_self();
	pci_function_ptr_result_t pci_function_r = cspace_lookup_pci_function(
		cspace, pci_function_cap, CAP_RIGHTS_PCI_FUNCTION_PASSTHROUGH);
	if (compiler_unexpected(pci_function_r.e != OK)) {
		ret = pci_function_r.e;
		goto out_pci_function_released;
	}

	pci_function = pci_function_r.r;
	spinlock_acquire(&pci_function->header.lock);

	// Passthrough is only possible when the given function
	// is configured to allow passthrough.
	if (!pci_function_option_flags_get_passthrough(
		    &pci_function->option_flags)) {
		ret = ERROR_OBJECT_CONFIG;
		goto out_pci_function_ref;
	}

	// Check if the function already passedthrough
	if (pci_function->passedthrough) {
		ret = ERROR_DENIED;
		goto out_pci_function_ref;
	}

	pci_host_t *pci_host = pci_function->pci_host;

	if (enable) {
		if (pci_host->lockdown &&
		    (pci_host->state == PCI_HOST_LOCKDOWN_STATE_RESTRICTED)) {
			// Unmap function's BAR ranges from pci_host->addrspace
			// and stop emulating trapped writes to the function's
			// ECAM range.
			ret = pci_function_passthrough(pci_function);
		}
	}

out_pci_function_ref:
	spinlock_release(&pci_function->header.lock);
	object_put_pci_function(pci_function);
out_pci_function_released:
	return ret;
}
