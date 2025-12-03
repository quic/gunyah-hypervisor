// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <compiler.h>
#include <log.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <vcpu_run.h>

#include "event_handlers.h"
#include "pci_host.h"

vcpu_trap_result_t
pci_host_vdevice_access_cam(vdevice_t *vdevice, size_t offset,
			    size_t access_size, register_t *value,
			    bool is_write) REQUIRE_READ((rcu_read))
{
	(void)value;

	thread_t	  *current = thread_get_self();
	vcpu_trap_result_t ret	   = VCPU_TRAP_RESULT_UNHANDLED;
	pci_host_t     *pci_host   = pci_host_container_of_cam_vdevice(vdevice);
	pci_function_t *pci_function;

	spinlock_acquire(&pci_host->header.lock);

	switch (pci_host->state) {
	case PCI_HOST_LOCKDOWN_STATE_SCANNING:
		// Verify if the function is not passedthrough
		pci_function = pci_host_search_function_in_ecam(
			pci_host, offset, access_size);
		if ((pci_function != NULL) && (pci_function->passedthrough)) {
			// Function is already passedthrough, therefore,
			// the emulation of this function in pci_host's address
			// space should result in fault.
			ret = VCPU_TRAP_RESULT_FAULT;
			goto out_ref;
		} else {
			// If the function is NULL, then it is not created
			// yet but in this case, we still block vCPU. If
			// the function is not NULL, then it is not
			// passedthrough yet, therefore, block the vCPU
			// on the access.
		}

		// Block the thread until the state is exited
		if (is_write) {
			scheduler_lock_nopreempt(current);
			if (vcpu_run_is_enabled(current)) {
				scheduler_block(
					current,
					SCHEDULER_BLOCK_PCI_HOST_ECAM_FAULT);
				scheduler_trigger();
				ret = VCPU_TRAP_RESULT_RETRY;
			}
			scheduler_unlock_nopreempt(current);
		} else {
			// We should not receive any traps with
			// read access.
			TRACE_AND_LOG(
				DEBUG, WARN,
				"Unexpected ECAM read trap received @ {:x} in Scanning state",
				pci_host->cam_base + offset);
		}
		break;
	case PCI_HOST_LOCKDOWN_STATE_RESTRICTED:
		// Verify if the function has passthrough disabled and the
		// pci_host in Restricted Mode
		pci_function = pci_host_search_function_in_ecam(
			pci_host, offset, access_size);
		if ((pci_function == NULL) ||
		    ((pci_function != NULL) &&
		     (!pci_function_option_flags_get_passthrough(
			     &pci_function->option_flags)))) {
			// The function is NULL, or it does have the passthrough
			// option set, either way, it should result in a fault.
			ret = VCPU_TRAP_RESULT_FAULT;
			goto out_ref;
		} else {
			// The ECAM is partially read-only, i.e. capabilities
			// are emulated.
			// We should receive traps only for functions which are
			// not attached to a vpci bus.
			if (pci_function->fn0.type !=
			    VPCI_FUNCTION_TYPE_PHYSICAL) {
				ret = VCPU_TRAP_RESULT_FAULT;
				goto out_ref;
			}
			// TODO: Emulate capabilities
			ret = VCPU_TRAP_RESULT_EMULATED;
		}
		break;
	case PCI_HOST_LOCKDOWN_STATE_PERMISSIVE:
	default:
		// Unknown state of the PCI Host.
		TRACE_AND_LOG(DEBUG, WARN,
			      "ECAM trap received @ {:x} in Unknown state",
			      pci_host->cam_base + offset);
		break;
	}

out_ref:
	spinlock_release(&pci_host->header.lock);

	return ret;
}

vcpu_trap_result_t
pci_host_vdevice_access_mem_aperture(vdevice_t *vdevice, size_t offset,
				     size_t access_size, register_t *value,
				     bool is_write)
{
	(void)access_size;
	(void)value;
	(void)is_write;

	thread_t		 *current = thread_get_self();
	vcpu_trap_result_t	  ret	  = VCPU_TRAP_RESULT_UNHANDLED;
	pci_host_aperture_data_t *aperture_data =
		pci_host_aperture_data_container_of_mem_aperture_vdevice(
			vdevice);
	pci_host_t *pci_host = aperture_data->pci_host;

	spinlock_acquire(&pci_host->header.lock);

	switch (pci_host->state) {
	case PCI_HOST_LOCKDOWN_STATE_SCANNING:
		// Any access to Memory aperture should be blocked
		// in order to serialize it against BAR probes.
		// Note: Release of the blocked VCPUs shall be handled
		// by the lockdown hypercall handler whenever the state
		// is transitioned.
		scheduler_lock_nopreempt(current);
		if (vcpu_run_is_enabled(current)) {
			scheduler_block(
				current,
				SCHEDULER_BLOCK_PCI_HOST_MEM_APERTURE_FAULT);

			scheduler_trigger();
			ret = VCPU_TRAP_RESULT_RETRY;
		}
		scheduler_unlock_nopreempt(current);
		break;
	case PCI_HOST_LOCKDOWN_STATE_RESTRICTED:
		// We should not receive any traps
		// while in Permissive state.
		TRACE_AND_LOG(
			DEBUG, WARN,
			"Unexpected Memory Aperture trap received @ {:x} in Permissive state",
			aperture_data->vmaddr + offset);
		break;
	case PCI_HOST_LOCKDOWN_STATE_PERMISSIVE:
	default:
		// Unknown state of the PCI Host.
		TRACE_AND_LOG(
			DEBUG, WARN,
			"Memory Aperture trap received @ {:x} in Unknown state",
			aperture_data->vmaddr + offset);
		break;
	}

	spinlock_release(&pci_host->header.lock);

	return ret;
}
