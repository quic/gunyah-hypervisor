// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Allocate a contiguous block of virtual memory of at least the specified size.
//
// The size will be rounded up to the allocation granularity, which is typically
// several megabytes. The returned address may be randomised if KASLR is in use
// and should not be assumed to be contiguous with any prior allocations for the
// same partition.
virt_range_result_t
hyp_aspace_allocate(size_t min_size);

// Free a block of virtual memory previously returned by hyp_aspace_allocate().
void
hyp_aspace_deallocate(partition_t *partition, virt_range_t virt_range);

// Create a 1:1 mapping of the given physical address range, accessible by the
// hypervisor without calling partition_phys_access_begin.
//
// This should only be used by platform-specific legacy code that assumes 1:1
// mappings.
error_t
hyp_aspace_map_direct(paddr_t phys, size_t size, pgtable_access_t access,
		      pgtable_hyp_memtype_t memtype, vmsa_shareability_t share);

// Remove a mapping created by hyp_aspace_map_direct().
error_t
hyp_aspace_unmap_direct(paddr_t phys, size_t size);

// Check for the existence of any mappings in the hypervisor address space for
// the given range. Note that when the kernel is using ARMv8.1-PAN (or an
// equivalent), there may be mappings in this range which are accessible only
// after calling partition_phys_access_begin(); this function ignores such
// mappings.
lookup_result_t
hyp_aspace_is_mapped(uintptr_t virt, size_t size, pgtable_access_t access);

error_t
hyp_aspace_va_to_pa_el2_read(void *addr, paddr_t *pa, MAIR_ATTR_t *memattr,
			     vmsa_shareability_t *shareability);

error_t
hyp_aspace_va_to_pa_el2_write(void *addr, paddr_t *pa, MAIR_ATTR_t *memattr,
			      vmsa_shareability_t *shareability);
