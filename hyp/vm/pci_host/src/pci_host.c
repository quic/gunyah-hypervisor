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
#include <hyp_aspace.h>
#include <list.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <trace.h>
#include <util.h>

#include <events/pci_host.h>

#include "event_handlers.h"
#include "pci_host.h"

error_t
pci_host_handle_object_create_pci_host(pci_host_create_t pci_host_create)
{
	// Initialize the sub objects in pci_host
	list_init(&pci_host_create.pci_host->aperture_list);
	return OK;
}

static error_t
pci_host_map_device_memextent(memextent_t *me, addrspace_t *addrspace,
			      vmaddr_t vmaddr)
{
	error_t err = OK;

	// NOTE: This function always maps the memextent
	// as Read-Write Device Memory.

	memextent_mapping_attrs_t map_attrs = memextent_mapping_attrs_default();
	memextent_mapping_attrs_set_user_access(&map_attrs, PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_kernel_access(&map_attrs,
						  PGTABLE_ACCESS_RW);
	memextent_mapping_attrs_set_memtype(&map_attrs,
					    PGTABLE_VM_MEMTYPE_DEVICE_NGNRE);

	err = memextent_map(me, addrspace, vmaddr, map_attrs,
			    addrspace_map_flags_default());

	return err;
}

static error_t
pci_host_map_cam(pci_host_t *pci_host)
{
	error_t		    err;
	partition_t	   *hyp_partition = partition_get_private();
	virt_range_result_t range = hyp_aspace_allocate(pci_host->cam_size);
	if (range.e != OK) {
		TRACE_AND_LOG(DEBUG, WARN, "ECAM: Address allocation failed.");
		err = range.e;
		goto out;
	}

	pgtable_hyp_start();

	pci_host->cam_vaddr = (uintptr_t)range.r.base;
	err = pgtable_hyp_map(hyp_partition, pci_host->cam_vaddr,
			      pci_host->cam_size, (paddr_t)pci_host->cam_base,
			      PGTABLE_HYP_MEMTYPE_NOSPEC_NOCOMBINE,
			      PGTABLE_ACCESS_RW,
			      VMSA_SHAREABILITY_NON_SHAREABLE);

	pgtable_hyp_commit();
	if (err != OK) {
		TRACE_AND_LOG(DEBUG, WARN,
			      "ECAM: Mapping of ECAM @ {:x} failed.",
			      pci_host->cam_vaddr);
		goto out;
	}

	// The cam_memextent may already be mapped in the addrspace
	// at the specified base address, if not, map it.
	if (!memextent_is_mapped(pci_host->memextent, pci_host->addrspace,
				 true)) {
		err = pci_host_map_device_memextent(pci_host->memextent,
						    pci_host->addrspace,
						    pci_host->cam_base);
	}

	if (err == OK) {
		pci_host->state = PCI_HOST_LOCKDOWN_STATE_PERMISSIVE;
	}

out:
	return err;
}

error_t
pci_host_handle_object_activate_pci_host(pci_host_t *pci_host)
{
	error_t err = OK;

	assert(pci_host != NULL);

	// Note: Atleast one pci aperture should be present.
	if (list_is_empty(&pci_host->aperture_list)) {
		err = ERROR_OBJECT_STATE;
		goto out;
	}

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_aperture_data_t,
				     &pci_host->aperture_list,
				     pci_host_aperture_data, list_node, fd)
		if (!memextent_is_mapped(fd->memextent, pci_host->addrspace,
					 true)) {
			err = pci_host_map_device_memextent(
				fd->memextent, pci_host->addrspace, fd->vmaddr);
		}
		if (err != OK) {
			goto out;
		}
	LIST_FOREACH_CONTAINER_END

	err = pci_host_map_cam(pci_host);
out:

	return err;
}

void
pci_host_handle_object_deactivate_pci_host(pci_host_t *pci_host)
{
	(void)pci_host;
}

void
pci_host_handle_object_cleanup_pci_host(pci_host_t *pci_host)
{
	(void)pci_host;
}
