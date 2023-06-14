// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// This function attaches an address space to a thread.
//
// An address space can only be attached to a thread before it starts running
// and the address space should already be in an activate state.
//
// The function returns an error if the thread is not vcpu kind, if it belongs
// to a HLOS VM and if it the address space has not been activated.
error_t
addrspace_attach_thread(addrspace_t *addrspace, thread_t *thread);

// Get a pointer to the thread's addrspace.
addrspace_t *
addrspace_get_self(void);

// Configure the address space.
//
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
addrspace_configure(addrspace_t *addrspace, vmid_t vmid);

// Configure the address space information area.
//
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
addrspace_configure_info_area(addrspace_t *addrspace, memextent_t *info_area_me,
			      vmaddr_t ipa);

// Nominate an address range as being handled by an unprivileged VMM.
//
// This can return ERROR_NORESOURCES if the implementation has reached a limit
// of nominated address ranges, ERROR_ARGUMENT_INVALID if the specified range
// overlaps an existing VMMIO range, or ERROR_UNIMPLEMENTED if there is no way
// to forward faults to an unprivileged VMM.
error_t
addrspace_add_vmmio_range(addrspace_t *addrspace, vmaddr_t base, size_t size);

// Remove an address range from the ranges handled by an unprivileged VMM.
//
// The range must match one that was previously added to the address space by
// calling addrspace_add_vmmio_range().
error_t
addrspace_remove_vmmio_range(addrspace_t *addrspace, vmaddr_t base,
			     size_t size);

// Translate a VA to PA in the current guest address space.
//
// Returns ERROR_DENIED if the lookup faults in stage 2 (including during the
// stage 1 page table walk), or ERROR_ADDR_INVALID if it faults in stage 1,
// regardless of the cause of the fault.
//
// Requires the RCU read lock, because the returned physical address might be
// unmapped concurrently and then reused after an RCU grace period.
paddr_result_t
addrspace_va_to_pa_read(gvaddr_t addr) REQUIRE_RCU_READ;

// Translate a VA to IPA in the current guest address space.
//
// Returns ERROR_DENIED if the lookup faults in stage 2 (during the stage 1 page
// table walk), or ERROR_ADDR_INVALID if it faults in stage 1, regardless of the
// cause of the fault.
vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr);

// Check whether an address range is within the address space.
error_t
addrspace_check_range(addrspace_t *addrspace, vmaddr_t base, size_t size);

// Map into an addrspace.
error_t
addrspace_map(addrspace_t *addrspace, vmaddr_t vbase, size_t size, paddr_t phys,
	      pgtable_vm_memtype_t memtype, pgtable_access_t kernel_access,
	      pgtable_access_t user_access);

// Unmap from an addrspace.
error_t
addrspace_unmap(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		paddr_t phys);

// Lookup a mapping in the addrspace.
addrspace_lookup_result_t
addrspace_lookup(addrspace_t *addrspace, vmaddr_t vbase, size_t size);
