// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Low-level page table manipulation routines.
//
// Functions in this interface generally should only be called by modules that
// are responsible for managing address spaces. Don't call them directly from
// more general modules (syscall handlers, etc).
//
// The map operation return errors in cases of inconsistent mappings where
// there are existing mappings present in the range or allocation failures.
// Unmap does not return error for non-existing mappings.
//
// The caller will generally need to operate on some higher-level model of the
// address space first, and hold locks on that model (possibly fine-grained)
// to prevent conflicting updates. The update operations defined here are not
// required to be thread-safe with respect to updates affecting overlapping
// address ranges.
// FIXME: The current implementation is not thread-safe and the caller must
// ensure that the address space being operated on is locked. This may possibly
// require different modules directly operating on a page-table to share a
// lock.
//
// A caller must always flag the start of a set of one or more map and unmap
// operations by calling the start function.  If synchronisation of updates
// with the page-table walkers (either locally or on other CPUs) can be
// deferred, then it will be deferred until a call is made to the corresponding
// commit function. The caller must always call the commit function before
// relying in any way on the updates having taken effect.
//
// In multi-processor systems, remote CPUs or IOMMU-protected devices using
// an affected address space might either continue to see the old mapping, or
// see a temporarily invalid mapping (which may extend outside the specified
// address range), especially if the mapping change has caused a page to
// change sizes. This will not occur for any memory access that the commit()
// call _inter-thread happens before_ (as defined by C18), or for any call
// to lookup() or lookup_range() that completes after an RCU grace period has
// elapsed after the commit function returns.

//
// Hypervisor page table management.
//
// In order to correctly attribute ownership of page table levels, the caller
// must avoid allocating page table levels in one partition if they might be
// subsequently freed into another partition. This can be done by selecting
// some allocation block size that mappings will never cross, and pre-
// allocating page table levels down to that block size from a global pool.
//
// Mappings with NONE access type may be used to indicate that the hypervisor
// should only be permitted to access the mapping on behalf of a VM, and will
// take specific action to enable and disable such accesses (e.g. clearing and
// setting PAN on ARMv8.1). Not all architectures support this; a different
// technique must be used for useracces on those that do not.
//

// Returns false if the specified address is unmapped.
bool
pgtable_hyp_lookup(uintptr_t virt, paddr_t *mapped_base, size_t *mapped_size,
		   pgtable_hyp_memtype_t *mapped_memtype,
		   pgtable_access_t *	  mapped_access);

// Returns false if there is no mapping in the specified range. If a mapping
// is found and can be efficiently determined to be the last mapping in the
// range, the boolean *remainder_unmapped will be set to true; otherwise it
// will be unchanged. Note that the returned mapping may extend beyond the
// specified range.
bool
pgtable_hyp_lookup_range(uintptr_t virt_base, size_t virt_size,
			 uintptr_t *mapped_virt, paddr_t *mapped_phys,
			 size_t *		mapped_size,
			 pgtable_hyp_memtype_t *mapped_memtype,
			 pgtable_access_t *	mapped_access,
			 bool *			remainder_unmapped);

// Creates page table levels owned by the given partition which are able to
// directly map entries covering the given size, but don't actually map
// anything. This is intended for preallocating levels using the hypervisor's
// private allocator, but might be more generally useful.
error_t
pgtable_hyp_preallocate(partition_t *partition, uintptr_t virt, size_t size);

// Flag the start of one of more map or unmap calls.
void
pgtable_hyp_start(void);

// Creates a new mapping, assuming no existing mappings in the range. May
// use the given partition to allocate levels if needed.
error_t
pgtable_hyp_map(partition_t *partition, uintptr_t virt, size_t size,
		paddr_t phys, pgtable_hyp_memtype_t memtype,
		pgtable_access_t access, vmsa_shareability_t shareability);
error_t
pgtable_hyp_remap(partition_t *partition, uintptr_t virt, size_t size,
		  paddr_t phys, pgtable_hyp_memtype_t memtype,
		  pgtable_access_t access, vmsa_shareability_t shareability);

// Removes all mappings in the given range. Frees levels into the specified
// partition's allocators, but only if they cannot be used to create mappings
// of the size preserved_prealloc.  The preserved_prealloc field can therefore
// be used to prevent freeing of levels created by a previous hyp_preallocate
// call to the specified partition.
void
pgtable_hyp_unmap(partition_t *partition, uintptr_t virt, size_t size,
		  size_t preserved_prealloc);
#define PGTABLE_HYP_UNMAP_PRESERVE_ALL	0U
#define PGTABLE_HYP_UNMAP_PRESERVE_NONE util_bit((sizeof(uintptr_t) * 8U) - 1U)

// Ensure that all previous hypervisor map and unmap calls are complete.
void
pgtable_hyp_commit(void);

//
// VM page table management.
//
// VM page tables don't have the same constraints for level preallocation &
// freeing because they are always entirely owned by one partition.
//
error_t
pgtable_vm_init(partition_t *partition, pgtable_vm_t *pgtable, vmid_t vmid);

// Free all resources for page table
void
pgtable_vm_destroy(partition_t *partition, pgtable_vm_t *pgtable);

// Returns false if the specified address is unmapped.
bool
pgtable_vm_lookup(pgtable_vm_t *pgtable, vmaddr_t virt, paddr_t *mapped_base,
		  size_t *mapped_size, pgtable_vm_memtype_t *mapped_memtype,
		  pgtable_access_t *mapped_vm_kernel_access,
		  pgtable_access_t *mapped_vm_user_access);

// Returns false if there is no mapping in the specified range. If a mapping
// is found and can be efficiently determined to be the last mapping in the
// range, the boolean *remainder_unmapped will be set to true; otherwise it
// will be unchanged. Note that the returned mapping may extend beyond the
// specified range.
bool
pgtable_vm_lookup_range(pgtable_vm_t *pgtable, vmaddr_t virt_base,
			size_t virt_size, vmaddr_t *mapped_virt,
			paddr_t *mapped_phys, size_t *mapped_size,
			pgtable_vm_memtype_t *mapped_memtype,
			pgtable_access_t *    mapped_vm_kernel_access,
			pgtable_access_t *    mapped_vm_user_access,
			bool *		      remainder_unmapped);

// Flag the start of one of more map or unmap calls.
void
pgtable_vm_start(pgtable_vm_t *pgtable);

// Creates a new mapping. If try_map is set, it returns an error if any existing
// mappings are present in the range. If try_map is false, any existing mappings
// in the specified range are removed or updated.
// pgtable_vm_start() must have been called before this call.
error_t
pgtable_vm_map(partition_t *partition, pgtable_vm_t *pgtable, vmaddr_t virt,
	       size_t size, paddr_t phys, pgtable_vm_memtype_t memtype,
	       pgtable_access_t vm_kernel_access,
	       pgtable_access_t vm_user_access, bool try_map);

// Removes all mappings in the given range. pgtable_vm_start() must have been
// called before this call.
void
pgtable_vm_unmap(partition_t *partition, pgtable_vm_t *pgtable, vmaddr_t virt,
		 size_t size);

// Remove only mappings that match the physical address within the specified
// range
void
pgtable_vm_unmap_matching(partition_t *partition, pgtable_vm_t *pgtable,
			  vmaddr_t virt, paddr_t phys, size_t size);

// Ensure that all previous VM map and unmap calls are complete.
void
pgtable_vm_commit(pgtable_vm_t *pgtable);

// Set VTCR and VTTBR registers with page table vtcr and vttbr bitfields values.
void
pgtable_vm_load_regs(pgtable_vm_t *vm_pgtable);
