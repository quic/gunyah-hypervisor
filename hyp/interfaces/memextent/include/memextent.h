// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Memory extents.
//
// These are ranges of memory that can be mapped, whole, into VM address
// spaces. Basic memory extents, which are always implemented, are contiguous
// ranges of memory with no special semantics. Other memory extent types that
// may be implemented include scatter-gather extents and virtual devices.

// Configure a memory extent without parent.
//
// A memory extent without parent can only be of type basic. The object's header
// lock must be held held and object state must be OBJECT_STATE_INIT.
error_t
memextent_configure(memextent_t *me, paddr_t phys_base, size_t size,
		    memextent_attrs_t attributes);

// Configure derived basic memory extent.
//
// The object's header lock must be held and object state must be
// OBJECT_STATE_INIT.
error_t
memextent_configure_derive(memextent_t *me, memextent_t *parent, size_t offset,
			   size_t size, memextent_attrs_t attributes);

// Returns true if the memextent supports donation.
bool
memextent_supports_donation(memextent_t *me);

// Donate memory to a child extent.
//
// This can only be used for sparse memory extents. If the memory extent has
// been derived the memory is taken from the parent extent, otherwise the memory
// is taken from the extent's partition. If reverse is true, the memory is
// donated back to the parent instead.
error_t
memextent_donate_child(memextent_t *me, size_t offset, size_t size,
		       bool reverse);

// Donate memory to a sibling extent.
//
// This can only be used for sparse memory extents. Both from and to memory
// extents must have the same parent memory extent.
error_t
memextent_donate_sibling(memextent_t *from, memextent_t *to, size_t offset,
			 size_t size);

// Map a memory extent into a specified address space. The entire range is
// mapped, except for any carveouts contained within the extent.
//
// If there are any existing mappings in the affected region, they are replaced
// with the new mapping. There may still be in-progress EL2 operations using old
// mappings. These are RCU read operations and are guaranteed to complete (or
// start using the new mapping) by the end of the next grace period.
error_t
memextent_map(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base,
	      memextent_mapping_attrs_t map_attrs);

// Map a portion of a memory extent into a specified address space. Only the
// range specified by offset and size is mapped, excluding any carveouts
// contained within this range.
//
// If there are any existing mappings in the affected region, they are replaced
// with the new mapping. There may still be in-progress EL2 operations using old
// mappings. These are RCU read operations and are guaranteed to complete (or
// start using the new mapping) by the end of the next grace period.
error_t
memextent_map_partial(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base,
		      size_t offset, size_t size,
		      memextent_mapping_attrs_t map_attrs);

// Unmap a memory extent from a specified address space. The entire range is
// unmapped, except for any carveouts contained within the extent.
//
// There may still be in-progress EL2 operations using the removed mappings.
// These are RCU read operations and are guaranteed to complete (or fault due to
// the unmap) by the end of the next grace period.
error_t
memextent_unmap(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base);

// Unmap a portion of a memory extent from a specified address space. Only the
// range specified by offset and size is unmapped, except for any carveouts
// contained within this range.
//
// There may still be in-progress EL2 operations using the removed mappings.
// These are RCU read operations and are guaranteed to complete (or fault due to
// the unmap) by the end of the next grace period.
error_t
memextent_unmap_partial(memextent_t *me, addrspace_t *addrspace,
			vmaddr_t vm_base, size_t offset, size_t size);

// Unmap a memory extent from all address spaces. The entire range is unmapped,
// except for any carveouts contained within the extent.
//
// There may still be in-progress EL2 operations using the removed mappings.
// These are RCU read operations and are guaranteed to complete (or fault due to
// the unmap) by the end of the next grace period.
void
memextent_unmap_all(memextent_t *me);

// Zero all owned regions of a memory extent in the given range.
error_t
memextent_zero_range(memextent_t *me, size_t offset, size_t size);

// Cache clean all owned regions of a memory extent in the given range.
error_t
memextent_cache_clean_range(memextent_t *me, size_t offset, size_t size);

// Cache flush all owned regions of a memory extent in the given range.
error_t
memextent_cache_flush_range(memextent_t *me, size_t offset, size_t size);

// Update the access rights on an existing mapping.
//
// There may still be in-progress EL2 operations using the old access rights.
// These are RCU read operations and are guaranteed to complete (or fault due to
// reduced access rights) by the end of the next grace period.
error_t
memextent_update_access(memextent_t *me, addrspace_t *addrspace,
			vmaddr_t		 vm_base,
			memextent_access_attrs_t access_attrs);

// Update the access rights on part of an existing mapping.
//
// There may still be in-progress EL2 operations using the old access rights.
// These are RCU read operations and are guaranteed to complete (or fault due to
// reduced access rights) by the end of the next grace period.
error_t
memextent_update_access_partial(memextent_t *me, addrspace_t *addrspace,
				vmaddr_t vm_base, size_t offset, size_t size,
				memextent_access_attrs_t access_attrs);

// Returns true if a memextent is mapped in an addrspace.
//
// If exclusive is true, this function will return false if the memextent is
// mapped in any other addrspace, regardless of whether it is mapped in the
// given addrspace.
bool
memextent_is_mapped(memextent_t *me, addrspace_t *addrspace, bool exclusive);

// Helper function to check the combination memory type of the memory extent and
// the mapping memtype.
bool
memextent_check_memtype(memextent_memtype_t  extent_type,
			pgtable_vm_memtype_t map_type);

// Helper function to derive a new memextent from a parent and activate it.
// This function does not create a capability to the new memextent.
memextent_ptr_result_t
memextent_derive(memextent_t *parent, paddr_t offset, size_t size,
		 memextent_memtype_t memtype, pgtable_access_t access,
		 memextent_type_t type);

// Temporarily retain all of the memextent's mappings.
//
// This acquires references to all addrspaces the memextent has mappings in,
// preventing the addrspace from being destroyed while looking up mappings.
void
memextent_retain_mappings(memextent_t *me) REQUIRE_LOCK(me->lock)
	ACQUIRE_LOCK(me->mappings);

// Release a memextent's retained mappings.
//
// This frees any references acquired in memextent_retain_mappings().
// If clear is true, all mappings will be cleared as well.
void
memextent_release_mappings(memextent_t *me, bool clear) REQUIRE_LOCK(me->lock)
	RELEASE_LOCK(me->mappings);

// Lookup a mapping in a memextent.
//
// The memextent's mappings must be retained when this is called. The supplied
// physical address and size must lie within the memextent, and the index must
// be less than MEMEXTENT_MAX_MAPS.
//
// If the returned addrspace is not NULL, the memextent has a mapping in the
// given range; otherwise the memextent is not mapped for this range. The
// returned size is valid for both of these cases, dictating the size of the
// mapping (or lack thereof). This size may be smaller than the given size if
// the range is only partially mapped.
memextent_mapping_t
memextent_lookup_mapping(memextent_t *me, paddr_t phys, size_t size, index_t i)
	REQUIRE_LOCK(me->lock) REQUIRE_LOCK(me->mappings);

// Claim and map a memextent for access in the hypervisor.
//
// The specified partition must be the owner of the object that the memextent
// is being attached to. Typically this is an address space or virtual device
// object.
//
// The specified extent must be a basic (not sparse) extent with normal memory
// type, no children, RW access, and no existing hypervisor attachment. While it
// remains attached, derivation of children or donation of memory to or from
// this extent will not be permitted. It may, however, be mapped in VM address
// spaces while attached, and existing VM address space mappings will not be
// removed by attachement.
//
// The specified virtual address range must be within a region allocated to the
// specified partition by hyp_aspace_allocate(). The specified size must be a
// nonzero multiple of the page size and no greater than the size of the
// memextent.
//
// If the memextent is provided through a hypervisor API, the caller should
// possess the MEMEXTENT_ATTACH right.
//
// If the call is successful, the entire specified address range will be mapped
// to the memextent.
error_t
memextent_attach(partition_t *owner, memextent_t *extent, uintptr_t hyp_va,
		 size_t size);

// Detach a memextent from the hypervisor.
//
// The specified owner and extent must match a previous successful call to
// memextent_attach().
void
memextent_detach(partition_t *owner, memextent_t *extent);

// Find the offset of a specified physical access within a memextent.
//
// This is intended to be used for handling faults. If any part of the specified
// physical address range is outside the memextent (including in a gap in a
// sparse memextent), it will return an error.
size_result_t
memextent_get_offset_for_pa(memextent_t *me, paddr_t pa, size_t size);
