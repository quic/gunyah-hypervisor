// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
// All Rights Reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <assert.h>
#include <hyptypes.h>

#include <hypcontainers.h>

#include <atomic.h>
#include <compiler.h>
#include <log.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <thread.h>
#include <trace.h>
#include <util.h>
#include <vcpu_run.h>
#include <vpci.h>

#include "event_handlers.h"
#include "pci_host.h"

vcpu_trap_result_t
pci_host_vdevice_access_cam(vdevice_t *vdevice, size_t offset,
			    size_t access_size, register_t *value,
			    bool is_write) REQUIRE_READ((rcu_read))
{
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

			// Configuration space only supports accesses within one
			// aligned 64-bit word; we're allowed to reject anything
			// else. This means we don't have to handle accesses
			// that cross a function boundary.
			if ((offset / sizeof(uint64_t)) !=
			    ((offset + access_size - 1U) / sizeof(uint64_t))) {
				ret = VCPU_TRAP_RESULT_FAULT;
				goto out_ref;
			}

			if (is_write) {
				TRACE(DEBUG, INFO,
				      "pci_host {:#x}: CAM write RID {:#x}: {:#x}@{:#x}/{:d}",
				      (uintptr_t)pci_host,
				      pci_responder_id_raw(pci_function->id),
				      *value, offset, access_size);
				error_t err = vpci_config_write(
					&pci_function->fn0, offset, access_size,
					*value);
				// The config access might have blocked, and has
				// therefore dropped the RCU critical section.
				// Re-acquire it.
				// FIXME: QC Gunyah issue #252
				rcu_read_start();
				if (err != OK) {
					TRACE(DEBUG, WARN,
					      "pci_host {:#x}: CAM write failed: {:d}",
					      (uintptr_t)pci_host,
					      (register_t)err);
				}
			} else {
				TRACE(DEBUG, INFO,
				      "pci_host {:#x}: CAM read RID {:#x}: {:#x}@{:#x}/{:d}",
				      (uintptr_t)pci_host,
				      pci_responder_id_raw(pci_function->id),
				      *value, offset, access_size);
				register_result_t val_r =
					vpci_config_read(&pci_function->fn0,
							 offset, access_size);
				// The config access might have blocked, and has
				// therefore dropped the RCU critical section.
				// Re-acquire it.
				// FIXME: QC Gunyah issue #252
				rcu_read_start();
				if (val_r.e != OK) {
					TRACE(DEBUG, WARN,
					      "pci_host {:#x}: CAM read failed {:d}",
					      (uintptr_t)pci_host,
					      (register_t)val_r.e);
					// Failed read accesses are RAO
					*value = ~(register_t)0U;
				} else {
					*value = val_r.r;
				}
			}
			ret = VCPU_TRAP_RESULT_EMULATED;
		}
		break;
	case PCI_HOST_LOCKDOWN_STATE_PERMISSIVE:
		// We should not receive any traps
		// while in Permissive state.
		TRACE_AND_LOG(
			DEBUG, WARN,
			"Unexpected ECAM trap received @ {:x} in Permissive state",
			pci_host->cam_base + offset);
		break;
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
	case PCI_HOST_LOCKDOWN_STATE_PERMISSIVE:
		// We should not receive any traps
		// while in Restricted/Permissive state.
		TRACE_AND_LOG(
			DEBUG, WARN,
			"Unexpected Memory Aperture trap received @ {:x} in Restricted/Permissive state",
			aperture_data->vmaddr + offset);
		break;
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

static uint64_t
pci_host_config_read(uintptr_t addr, size_t offset, size_t access_size)
	REQUIRE_RCU_READ
{
	uintptr_t mem_addr  = addr + offset;
	uint64_t  read_val  = 0U;
	size_t	  read_size = 0U;

	while (read_size < access_size) {
		uint8_t byte = atomic_load_relaxed((_Atomic uint8_t *)mem_addr);
		read_val |= (uint64_t)byte << (util_width(uint8_t) * read_size);
		read_size++;
		mem_addr++;
	}

	return read_val;
}

register_result_t
pci_host_passthrough_config_read(vpci_function_t	*function,
				 vpci_capability_data_t *capability,
				 size_t offset, size_t access_size)
{
	register_result_t ret;
	uint64_t	  read_val;

	if (access_size > sizeof(ret.r)) {
		// Access larger than one register
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else if (capability == NULL) {
		// Access outside of any capability
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else {
		pci_function_cap_data_t *cap_data =
			pci_function_cap_data_container_of_base(capability);

		if (pci_capability_access_flags_get_passthrough_visible(
			    &cap_data->flags)) {
			pci_function_t *pci_function =
				pci_function_container_of_fn0(function);
			read_val = pci_host_config_read(
				pci_function->base_addr + cap_data->base.offset,
				offset, access_size);
		} else {
			// Emulate as NULL capability data
			read_val = 0U;
		}

		ret = register_result_ok(read_val);
	}

	return ret;
}

static void
pci_host_config_write(uintptr_t addr, size_t offset, size_t access_size,
		      register_t data)
{
	// Write the value directly to the memory
	switch (access_size) {
	case 1U:
		atomic_store_relaxed((_Atomic uint8_t *)(addr + offset),
				     (uint8_t)data);
		break;
	case 2U:
		atomic_store_relaxed((_Atomic uint16_t *)(addr + offset),
				     (uint16_t)data);
		break;
	case 4U:
		atomic_store_relaxed((_Atomic uint32_t *)(addr + offset),
				     (uint32_t)data);
		break;
	case 8U:
		atomic_store_relaxed((_Atomic uint64_t *)(addr + offset), data);
		break;
	default:
		TRACE_AND_LOG(DEBUG, WARN,
			      "ECAM: Undefined write access size {:d}",
			      access_size);
		break;
	}
}

error_t
pci_host_passthrough_config_write(vpci_function_t	 *function,
				  vpci_capability_data_t *capability,
				  size_t offset, size_t access_size,
				  register_t data)
{
	error_t err = OK;

	if (access_size > sizeof(data)) {
		// Access larger than one register
		err = ERROR_UNIMPLEMENTED;
	} else if (capability == NULL) {
		// Access outside of capability
		err = ERROR_UNIMPLEMENTED;
	} else {
		pci_function_cap_data_t *cap_data =
			pci_function_cap_data_container_of_base(capability);

		if (pci_capability_access_flags_get_passthrough_writable(
			    &cap_data->flags)) {
			pci_function_t *pci_function =
				pci_function_container_of_fn0(function);
			pci_host_config_write(pci_function->base_addr +
						      cap_data->base.offset,
					      offset, access_size, data);
		} else {
			// Emulate as WI;
		}
	}

	return err;
}

register_result_t
pci_host_physical_config_read(vpci_function_t	     *function,
			      vpci_capability_data_t *capability, size_t offset,
			      size_t access_size)
{
	register_result_t ret;
	uint64_t	  read_val;

	if (access_size > sizeof(ret.r)) {
		// Access larger than one register
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else if (capability == NULL) {
		// Access outside of capability
		ret = register_result_error(ERROR_UNIMPLEMENTED);
	} else {
		pci_function_cap_data_t *cap_data =
			pci_function_cap_data_container_of_base(capability);

		if (pci_capability_access_flags_get_lockdown_visible(
			    &cap_data->flags)) {
			pci_function_t *pci_function =
				pci_function_container_of_fn0(function);
			read_val = pci_host_config_read(
				pci_function->base_addr + cap_data->base.offset,
				offset, access_size);
		} else {
			// Emulate as NULL capability data
			read_val = 0U;
		}

		ret = register_result_ok(read_val);
	}

	return ret;
}

error_t
pci_host_physical_config_write(vpci_function_t	      *function,
			       vpci_capability_data_t *capability,
			       size_t offset, size_t access_size,
			       register_t data)
{
	error_t err = OK;

	if (access_size > sizeof(data)) {
		// Access larger than one register
		err = ERROR_UNIMPLEMENTED;
	} else if (capability == NULL) {
		// Access outside of capability
		err = ERROR_UNIMPLEMENTED;
	} else {
		pci_function_cap_data_t *cap_data =
			pci_function_cap_data_container_of_base(capability);

		if (pci_capability_access_flags_get_lockdown_writable(
			    &cap_data->flags)) {
			pci_function_t *pci_function =
				pci_function_container_of_fn0(function);
			pci_host_config_write(pci_function->base_addr +
						      cap_data->base.offset,
					      offset, access_size, data);
		} else {
			// Emulate as write ignored
		}
	}

	return err;
}
