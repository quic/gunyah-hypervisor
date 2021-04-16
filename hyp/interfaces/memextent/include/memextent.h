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

// Unmap a memory extent from a specified address space. The entire range is
// unmapped, except for any carveouts contained within the extent.
//
// There may still be in-progress EL2 operations using the removed mappings.
// These are RCU read operations and are guaranteed to complete (or fault due to
// the unmap) by the end of the next grace period.
error_t
memextent_unmap(memextent_t *me, addrspace_t *addrspace, vmaddr_t vm_base);

// Unmap a memory extent from all address spaces. The entire range is unmmaped,
// except for any carveouts contained within the extent.
//
// There may still be in-progress EL2 operations using the removed mappings.
// These are RCU read operations and are guaranteed to complete (or fault due to
// the unmap) by the end of the next grace period.
void
memextent_unmap_all(memextent_t *me);

// Update the access rights on an existing mapping.
//
// There may still be in-progress EL2 operations using the old access rights.
// These are RCU read operations and are guaranteed to complete (or fault due to
// reduced access rights) by the end of the next grace period.
error_t
memextent_update_access(memextent_t *me, addrspace_t *addrspace,
			vmaddr_t		 vm_base,
			memextent_access_attrs_t access_attrs);

// Helper function to check the combination memory type of the memory extent and
// the mapping memtype.
bool
memextent_check_memtype(memextent_memtype_t  extent_type,
			pgtable_vm_memtype_t map_type);

// Helper function to derive a new memextent from a parent and activate it.
// This function does not create a capability to the new memextent.
memextent_ptr_result_t
memextent_derive(memextent_t *parent, paddr_t offset, size_t size,
		 memextent_memtype_t memtype, pgtable_access_t access);

// FIXME: It should declare events for iterating through an extent's memory ranges
// and for handling read and write faults.
//	return PGTABLE_VM_MEMTYPE_DEVICE_NGNRNE;
