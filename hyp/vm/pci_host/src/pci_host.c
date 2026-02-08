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
#include <cspace.h>
#include <cspace_lookup.h>
#include <hyp_aspace.h>
#include <list.h>
#include <log.h>
#include <memdb.h>
#include <memextent.h>
#include <object.h>
#include <panic.h>
#include <partition.h>
#include <pgtable.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vcpu.h>
#include <vdevice.h>
#include <vpci.h>

#include <events/pci_host.h>

#include "event_handlers.h"
#include "pci_host.h"

bool
pci_host_search_function(pci_host_t		   *pci_host,
			 const pci_function_t	   *pci_function,
			 pci_host_function_data_t **data)
{
	bool found = false;

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_function_data_t,
				     &pci_host->function_list,
				     pci_host_function_data, list_node, fd)
		if (pci_responder_id_is_equal(fd->function->id,
					      pci_function->id)) {
			// Function already exists in the pci_host
			found = true;
			if (data != NULL) {
				*data = fd;
			}
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
	vdevice_init(&pci_host_create.pci_host->cam_vdevice);

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

static error_t
pci_host_memextent_update_access(memextent_t *memextent, addrspace_t *addrspace,
				 vmaddr_t	  vm_base,
				 pgtable_access_t pgtable_access)
{
	error_t			 err = OK;
	memextent_access_attrs_t access_attrs =
		memextent_access_attrs_default();

	memextent_access_attrs_set_user_access(&access_attrs, pgtable_access);
	memextent_access_attrs_set_kernel_access(&access_attrs, pgtable_access);

	err = memextent_update_access(memextent, addrspace, vm_base,
				      access_attrs,
				      addrspace_map_flags_default());
	if (err != OK) {
		goto out;
	}

	err = memextent_sync_all(memextent);
out:
	return err;
}

pci_function_t *
pci_host_search_function_in_ecam(pci_host_t *pci_host, size_t access_offset,
				 size_t access_size)
{
	pci_function_t *pci_function = NULL;

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_function_data_t,
				     &pci_host->function_list,
				     pci_host_function_data, list_node, fd)

		// Verify if the access is within the PCIe Function (function
		// size 4K page)
		if (((access_offset + pci_host->cam_base) >=
		     fd->function->base_addr) &&
		    ((access_offset + pci_host->cam_base + access_size) <
		     (fd->function->base_addr + 0x1000U))) {
			pci_function = fd->function;
			break;
		}
	LIST_FOREACH_CONTAINER_END

	return pci_function;
}

static void
pci_host_unblock_vcpus(pci_host_t *pci_host) REQUIRE_LOCK(preempt_disabled)
{
	partition_t *partition	   = pci_host->header.partition;
	list_t	    *thread_list   = &partition->thread_list;
	bool	     sched_trigger = false;

	spinlock_acquire_nopreempt(&partition->thread_list_lock);

	LIST_FOREACH_CONTAINER_CONSUME_BEGIN(thread_t, thread_list, thread,
					     thread_list_node, thread)
		if (!vcpu_is_vcpu(thread)) {
			continue;
		}

		// May happen for partially setup threads.
		if (thread->addrspace == NULL) {
			continue;
		}

		scheduler_lock_nopreempt(thread);
		if (scheduler_is_blocked(thread,
					 SCHEDULER_BLOCK_PCI_HOST_ECAM_FAULT)) {
			sched_trigger = scheduler_unblock(
				thread, SCHEDULER_BLOCK_PCI_HOST_ECAM_FAULT);
		}
		if (scheduler_is_blocked(
			    thread,
			    SCHEDULER_BLOCK_PCI_HOST_MEM_APERTURE_FAULT)) {
			sched_trigger = scheduler_unblock(
				thread,
				SCHEDULER_BLOCK_PCI_HOST_MEM_APERTURE_FAULT);
		}
		scheduler_unlock_nopreempt(thread);
	LIST_FOREACH_CONTAINER_CONSUME_END

	spinlock_release_nopreempt(&partition->thread_list_lock);

	if (sched_trigger) {
		scheduler_trigger();
	}
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
	// Attach the cam_vdevice
	err = vdevice_attach_vmaddr(VDEVICE_TYPE_PCI_HOST_CAM,
				    &pci_host->cam_vdevice, pci_host->addrspace,
				    pci_host->cam_base, pci_host->cam_size);
	if (err != OK) {
		goto out;
	}

	// Also update the access
	err = pci_host_memextent_update_access(pci_host->memextent,
					       pci_host->addrspace,
					       pci_host->cam_base,
					       PGTABLE_ACCESS_R);
	if (err != OK) {
		goto out;
	}

	// Any VCPU access to a memory aperture is trapped and
	// briefly blocked in order to serialize it against BAR
	// probes.
	// Note: Atleast one pci memory aperture should be present.
	assert(!list_is_empty(&pci_host->aperture_list));

	LIST_FOREACH_CONTAINER_BEGIN(pci_host_aperture_data_t,
				     &pci_host->aperture_list,
				     pci_host_aperture_data, list_node, fd)
		// At this point we assume all the memory
		// apertures are already mapped in the given
		// addrspace.
		size_result_t size_r = memextent_get_mapped_size(fd->memextent);
		if (size_r.e != OK) {
			err = size_r.e;
			goto out;
		}

		// Note: We emulate all the VPCU accesses to
		// memory apertures until the scanning state is
		// exited. The blocked VCPUs will be unblocked
		// when the state is changed to either
		// Restricted or Permissive.
		err = vdevice_attach_vmaddr(VDEVICE_TYPE_PCI_MEM_APERTURE,
					    &fd->mem_aperture_vdevice,
					    pci_host->addrspace, fd->vmaddr,
					    size_r.r);
		if (err != OK) {
			goto out;
		}

		err = pci_host_memextent_update_access(fd->memextent,
						       pci_host->addrspace,
						       fd->vmaddr,
						       PGTABLE_ACCESS_R);
		if (err != OK) {
			goto out;
		}
	LIST_FOREACH_CONTAINER_END
out:
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
	LIST_FOREACH_CONTAINER_BEGIN(pci_host_aperture_data_t,
				     &pci_host->aperture_list,
				     pci_host_aperture_data, list_node, fd)
		err = pci_host_memextent_update_access(fd->memextent,
						       pci_host->addrspace,
						       fd->vmaddr,
						       PGTABLE_ACCESS_RW);
		if (err != OK) {
			goto out;
		}

		// Note: The memory aperture emulator
		// must be detached now.
		vdevice_detach_vmaddr(&fd->mem_aperture_vdevice);

		spinlock_acquire_nopreempt(&fd->mem_aperture_vdevice.lock);
		fd->mem_aperture_vdevice.type = VDEVICE_TYPE_NONE;
		spinlock_release_nopreempt(&fd->mem_aperture_vdevice.lock);

		// Note: The ECAM remains mapped as
		// ReadOnly but partially, i.e.
		// capabilities are emulated accordingly
		// and not given direct access to the
		// guest.
	LIST_FOREACH_CONTAINER_END

	// Unblock the blocked VCPUs on ECAM & Memory
	// Apertures
	pci_host_unblock_vcpus(pci_host);

out:
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
	if (!memextent_is_mapped(pci_host->memextent, pci_host->addrspace,
				 true)) {
		err = pci_host_map_device_memextent(pci_host->memextent,
						    pci_host->addrspace,
						    pci_host->cam_base);
	}

	if (err != OK) {
		goto out;
	}

	// Detach the vdevice
	if (pci_host->addrspace != NULL) {
		if (pci_host->cam_vdevice.type != VDEVICE_TYPE_NONE) {
			vdevice_detach_vmaddr(&pci_host->cam_vdevice);
		}
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

	// If we are coming from the Scanning state,
	// then there might be VCPUs blocked which we
	// need to unblock.
	if (pci_host->state == PCI_HOST_LOCKDOWN_STATE_SCANNING) {
		pci_host_unblock_vcpus(pci_host);
	}

	// Note: The Restricted State does not block any
	// VCPUS.
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
		vdevice_init(&fd->mem_aperture_vdevice);
	LIST_FOREACH_CONTAINER_END

	err = pci_host_map_cam(pci_host);
out:

	return err;
}

error_t
pci_host_handle_vpci_attach(vpci_t *vpci, index_t *slot, cap_id_t device_cap)
{
	error_t		err;
	cspace_t       *cspace;
	pci_function_t *pci_function;

	cspace					 = cspace_get_self();
	pci_function_ptr_result_t pci_function_r = cspace_lookup_pci_function(
		cspace, device_cap, CAP_RIGHTS_PCI_FUNCTION_ATTACH);
	if (compiler_unexpected(pci_function_r.e != OK)) {
		err = pci_function_r.e;
		goto out;
	}
	pci_function = pci_function_r.r;

	if ((pci_function->fn0.type != VPCI_FUNCTION_TYPE_PASSTHROUGH) ||
	    (!pci_function->passedthrough)) {
		err = ERROR_OBJECT_CONFIG;
		goto out_ref;
	}

	index_result_t slot_r =
		vpci_bind_device(vpci, *slot, &pci_function->pci_vdevice);
	err = slot_r.e;
	if (err == OK) {
		*slot = slot_r.r;
	}

out_ref:
	object_put_pci_function(pci_function);
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
	pci_function_cleanup_bars(pci_function);
}

void
pci_host_handle_object_cleanup_pci_function(pci_function_t *pci_function)
{
	pci_host_t		 *pci_host;
	pci_host_function_data_t *function_data = NULL;

	pci_host = pci_function->pci_host;

	(void)pci_host_search_function(pci_host, pci_function, &function_data);

	assert(function_data != NULL);

	vpci_unbind_device(&pci_function->pci_vdevice);
	pci_function_destroy_capabilities(pci_function);
	pci_function_cleanup_bars(pci_function);
	object_put_pci_function(function_data->function);
	(void)list_delete_node(&pci_host->function_list,
			       &function_data->list_node);
	partition_free(pci_function->header.partition, function_data,
		       sizeof(pci_host_function_data_t));
	object_put_pci_host(pci_function->pci_host);
}
