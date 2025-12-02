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
#include <rcu.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>

#include <events/pci_host.h>

#include "event_handlers.h"
#include "pci_host.h"

bool
pci_host_search_function(pci_host_t	      *pci_host,
			 const pci_function_t *pci_function)
{
	bool found = false;

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_function_data_t,
				     &pci_host->function_list,
				     pci_host_function_data, list_node, fd)
		if (pci_responder_id_is_equal(fd->function->id,
					      pci_function->id)) {
			// Function already exists in the pci_host
			found = true;
			break;
		}
	LIST_FOREACH_CONTAINER_END

	return found;
}

bool
pci_host_search_inactive_function(pci_host_t *pci_host)
{
	bool found = false;

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_function_data_t,
				     &pci_host->function_list,
				     pci_host_function_data, list_node, fd)
		spinlock_acquire_nopreempt(&fd->function->header.lock);
		if (atomic_load_relaxed(&fd->function->header.state) !=
		    OBJECT_STATE_ACTIVE) {
			found = true;
			spinlock_release_nopreempt(&fd->function->header.lock);
			break;
		}
		spinlock_release_nopreempt(&fd->function->header.lock);
	LIST_FOREACH_CONTAINER_END

	return found;
}

memextent_t *
pci_host_search_aperture(pci_host_t *pci_host, paddr_t addr)
{
	memextent_t *me = NULL;

	rcu_read_start();
	memdb_obj_type_result_t res = memdb_lookup(addr);
	if ((res.e != OK) || (res.r.type != MEMDB_TYPE_EXTENT)) {
		goto out;
	}

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_aperture_data_t,
				     &pci_host->aperture_list,
				     pci_host_aperture_data, list_node, fd)
		if (fd->memextent == (memextent_t *)res.r.object) {
			me = fd->memextent;
			break;
		}
	LIST_FOREACH_CONTAINER_END
out:
	rcu_read_finish();
	return me;
}

error_t
pci_host_handle_object_create_pci_host(pci_host_create_t pci_host_create)
{
	// Initialize the sub objects in pci_host
	list_init(&pci_host_create.pci_host->aperture_list);
	list_init(&pci_host_create.pci_host->function_list);
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
pci_host_transition_scanning_state(pci_host_t *pci_host)
{
	error_t err = OK;

	// NOTE: This function assumes that the pci_host's
	// spinlock is held.

	assert(pci_host != NULL);

	// The ECAM aperture must be read-only to the owner VM.
	// Any VCPU access to ECAM should be blocked until the
	// scanning state is exited.
	// Also, the VPCU accesses to memory apertures
	// may trapped and briefly blocked in order to serialize
	// it against BAR probes.
	// PCIe function objects are allowed to be created in
	// this state but cannot be pass-throughed.

	return err;
}

error_t
pci_host_transition_restricted_state(pci_host_t *pci_host)
{
	error_t err = OK;

	// NOTE: This function assumes that the pci_host's
	// spinlock is held.

	assert(pci_host != NULL);

	// We need to unblock all the VPCUs blocked on
	// to the access to Memory apertures and also
	// map the memory aperture access attributes to
	// RW and detach the emulator for these
	// apertures.

	return err;
}

error_t
pci_host_transition_permissive_state(pci_host_t *pci_host)
{
	error_t err = OK;

	// NOTE: This function assumes that the pci_host's
	// spinlock is held.

	assert(pci_host != NULL);

	// The ECAM and Memory apertures are fully
	// accessible to the owner VM.

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

error_t
pci_host_handle_object_activate_pci_function(pci_function_t *pci_function)
{
	error_t err;

	assert(pci_function != NULL);
	assert(pci_function->pci_host != NULL);

	err = pci_host_function_prepare(pci_function->pci_host->cam_vaddr,
					pci_function->pci_host->cam_size,
					pci_function);

	return err;
}

void
pci_host_handle_object_deactivate_pci_function(pci_function_t *pci_function)
{
	(void)pci_function;
}

void
pci_host_handle_object_cleanup_pci_function(pci_function_t *pci_function)
{
	(void)pci_function;
}
