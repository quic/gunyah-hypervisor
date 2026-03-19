// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
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
addrspace_add_fault_range(addrspace_t *addrspace, vmaddr_t base, size_t size,
			  bool vmmio);

// Remove an address range from the ranges handled by an unprivileged VMM.
//
// The range must match one that was previously added to the address space by
// calling addrspace_add_fault_range().
error_t
addrspace_remove_fault_range(addrspace_t *addrspace, vmaddr_t base, size_t size,
			     bool vmmio);

// Translate a VA to PA in the current guest address space.
//
// This function will automatically retry the lookup if it fails for any reason
// that would have been automatically resolved by the addrspace module for an
// access directly from the VM. This includes, for example, transient faults
// caused by concurrent address space updates on other cores.
//
// The following errors may be returned:
//
// - ERROR_DENIED if the lookup is a write access and the stage 2 mapping is
//   read-only,
// - ERROR_ADDR_INVALID for any other non-transient fault in stage 2, or
// - ERROR_ARGUMENT_INVALID for any fault in stage 1, regardless of the cause of
//   the fault.
//
// If successful, the result is a structure containing:
// - phys: The PA corresponding to the specified guest VA address.
// - size: The minimum size of the guest mapping starting from the returned PA.
// - coherent: : flag indicating whether the effective cache attribute of the
//   guest's mapping is cache-coherent with the hypervisor's direct mapping of
//   the PA (if accessed via partition_phys_map()). If this flag is false, the
//   caller must perform appropriate cache maintenance before or after accessing
//   the PA via the direct mapping.
//
// Requires the RCU read lock, because the returned physical address might be
// unmapped concurrently and then reused after an RCU grace period.
addrspace_va_lookup_result_t
addrspace_va_to_pa(gvaddr_t addr, bool write) REQUIRE_RCU_READ;

// Translate a VA to PA in the current guest address space, without retrying.
//
// This is the same as addrspace_va_to_pa(), except that it will return
// ERROR_RETRY instead of retrying if a transient translation fault occurs.
// Generally this should be used in fault handlers, where a successful retry
// might not fault at all.
addrspace_va_lookup_result_t
addrspace_va_to_pa_noretry(gvaddr_t addr, bool write) REQUIRE_RCU_READ;

// Translate a VA to IPA in the current guest address space.
//
// This function is only used to obtain an IPA for an access that is known to
// have taken a stage 2 permission fault, so there is no need to check stage 1
// permissions. Therefore, it always simulates a read access, and does not
// automatically handle TLB conflicts or break-before-make.
//
// Returns ERROR_DENIED if the lookup faults in stage 2 (during the stage 1 page
// table walk), or ERROR_ADDR_INVALID if it faults in stage 1, regardless of the
// cause of the fault.
vmaddr_result_t
addrspace_va_to_ipa_read(gvaddr_t addr);

// Check whether an address range is within the address space.
error_t
addrspace_check_range(addrspace_t *addrspace, vmaddr_t base, size_t size);

extern opaque_lock_t pgtable_vm_map_lock;

// Acquire the lock for mapping or unmapping in a specified address space, and
// specify the map flags that will be applied to those map and unmap operations.
void
addrspace_start(addrspace_t *addrspace, addrspace_map_flags_t map_flags)
	ACQUIRE_ADDRSPACE_LOCK(addrspace);

// Map into an addrspace. The map flags must be the same as those passed to
// addrspace_start().
error_t
addrspace_map_locked(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		     paddr_t phys, pgtable_vm_memtype_t memtype,
		     pgtable_access_t	   kernel_access,
		     pgtable_access_t	   user_access,
		     addrspace_map_flags_t map_flags)
	REQUIRE_ADDRSPACE_LOCK(addrspace);

// Unmap from an addrspace. The map flags must be the same as those passed to
// addrspace_start().
error_t
addrspace_unmap_locked(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		       paddr_t phys, addrspace_map_flags_t map_flags)
	REQUIRE_ADDRSPACE_LOCK(addrspace);

// Commit an address space update and release the lock. The map flags must be
// the same as those passed to addrspace_start().
void
addrspace_commit(addrspace_t *addrspace, addrspace_map_flags_t map_flags)
	RELEASE_ADDRSPACE_LOCK(addrspace);

// Simple map and unmap operations with integrated locking.
static inline error_t
addrspace_map(addrspace_t *addrspace, vmaddr_t vbase, size_t size, paddr_t phys,
	      pgtable_vm_memtype_t memtype, pgtable_access_t kernel_access,
	      pgtable_access_t user_access, addrspace_map_flags_t map_flags)
{
	addrspace_start(addrspace, map_flags);
	error_t ret = addrspace_map_locked(addrspace, vbase, size, phys,
					   memtype, kernel_access, user_access,
					   map_flags);
	addrspace_commit(addrspace, map_flags);
	return ret;
}

// Simple map and unmap operations with integrated locking.
static inline error_t
addrspace_unmap(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		paddr_t phys, addrspace_map_flags_t map_flags)
{
	addrspace_start(addrspace, map_flags);
	error_t ret =
		addrspace_unmap_locked(addrspace, vbase, size, phys, map_flags);
	addrspace_commit(addrspace, map_flags);
	return ret;
}

// Synchronise any unmaps which were previously done by an addrspace_unmap()
// call while the no_sync flag was set in the map flags.
error_t
addrspace_unmap_sync(addrspace_t *addrspace) EXCLUDE_ADDRSPACE_LOCK(addrspace);

// Unlock and/or sanitise protected pages in an addrspace.
size_result_t
addrspace_modify_pages(addrspace_t *addrspace, vmaddr_t vbase, size_t size,
		       addrspace_modify_pages_flags_t flags)
	EXCLUDE_ADDRSPACE_LOCK(addrspace);

// Lookup a mapping in the addrspace.
addrspace_lookup_result_t
addrspace_lookup(addrspace_t *addrspace, vmaddr_t vbase, size_t size)
	EXCLUDE_ADDRSPACE_LOCK(addrspace);

// Allocate some memory in the info page.
//
// This generally should only be used prior to starting the VM, because the
// allocation will become visible to the VM immediately. If this is not
// possible, the data format should contain a validity bit or flag which can be
// checked by the VM before reading.
//
// The allocated memory is guaranteed to have been zero-initialised prior to the
// startup of the VM. It is mapped cacheable in the hypervisor address space. It
// may be necessary to clean the data cache to make written data visible to the
// VM.
//
// It is not possible to free info page memory once it is allocated. It will be
// freed when the addrspace object is released.
//
// If the alloc_desc argument is true, a descriptor will be allocated and
// returned; it will initially be invalid and must be enabled by calling
// addrspace_enable_info_area(). Otherwise, the descriptor will be NULL and the
// caller must use some other mechanism to inform the VM of the allocated IPA.
//
// Modules calling this API should set alloc_desc to false. It is expected that
// info area entries are only allocated using the hypercall interface.
addrspace_alloc_info_area_result_t
addrspace_alloc_info_area(addrspace_t *addrspace, size_t size, size_t alignment,
			  bool alloc_desc);

// Lookup an entry by index
addrspace_info_area_entry_result_t
addrspace_info_area_get_by_index(addrspace_t *addrspace, index_t index);

// Lookup an entry by its type, returns the first match
addrspace_info_area_entry_result_t
addrspace_info_area_get_by_type(addrspace_t			*addrspace,
				addrspace_info_area_entry_type_t type);

uintptr_t
addrspace_info_area_get_data_addr(addrspace_t *addrspace, size_t offset);

// Enable an info area descriptor.
//
// This should only be called after successfully initialising the payload of the
// info area.
void
addrspace_enable_info_area(addrspace_info_area_entry_t	   *descriptor,
			   addrspace_info_area_entry_type_t type);

// Simulate a virtual MSI write.
//
// This is a 32-bit write of an arbitrary value to an arbitrary address in the
// address space. It may be routed either to a virtual device, or to a real
// mapping in the address space.
//
// Any write that would be permitted for a kernel mode access by a VCPU
// attached to this address space may also be permitted by this function;
// therefore, the caller must only pass an address that has been explicitly
// programmed by a VCPU. For example, for a virtual PCIe endpoint, the address
// must come from the endpoint's emulated MSI-X table.
//
// Note that event handlers and the hypercall of the same name only permit
// writes that actually assert MSIs, not arbitrary writes. That restriction is
// necessary to prevent an untrusted VMM interfering with a protected VM, but
// cannot be applied to hypervisor-backed devices because certain kernel
// drivers assume that MSIs can write to normal memory (notably the Linux
// SMMUv3 driver). Support for writing to addresses other than MSI mailboxes
// is implemented only in this function, which is not called by the hypercall.
//
// Some IRQ controllers allow MSI writes to be tagged with out-of-band data
// identifying the source device; for example, this is supported by the GIC
// ITS. The device_id parameter may be set to a value that is not controllable
// by the VM, to emulate this behaviour. If no device ID is defined, it should
// be set to ~0U.
error_t
addrspace_trigger_msi(addrspace_t *addrspace, vmaddr_t mailbox,
		      uint32_t message, index_t device_id);
