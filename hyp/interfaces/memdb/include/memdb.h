// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// FIXME: replace with a selector event
typedef error_t (*memdb_fnptr)(paddr_t, size_t, void *);

// Populate the memory database. If any entry from the range already has an
// owner, return error and do not update the database.
//
// The partition argument is the partition to use for memdb node allocations,
// and must always be the hypervisor private partition.
error_t
memdb_insert(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type);

// Change the ownership of the input address range. Checks if all entries of
// range were pointing to previous object. If so, update all entries to point to
// the new object. If not, return error.
//
// The partition argument is the partition to use for memdb node allocations,
// and must always be the hypervisor private partition.
error_t
memdb_update(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type, uintptr_t prev_object,
	     memdb_type_t prev_type);

// Find the entry corresponding to the input address and return the object and
// type the entry is pointing to.
//
// This function returns an RCU-protected reference and therefore, it needs to
// be called in a RCU critical section and maintain it until we finish using the
// returned object.
memdb_obj_type_result_t
memdb_lookup(paddr_t addr) REQUIRE_RCU_READ;

// Check if all the entries from the input address range point to the object
// passed as an argument
bool
memdb_is_ownership_contiguous(paddr_t start_addr, paddr_t end_addr,
			      uintptr_t object, memdb_type_t type);

// Walk through the entire database and add the address ranges that are owned
// by the object passed as argument.
error_t
memdb_walk(uintptr_t object, memdb_type_t type, memdb_fnptr fn, void *arg);

// Walk through a range of the database and add the address ranges that are
// owned by the object passed as argument.
error_t
memdb_range_walk(uintptr_t object, memdb_type_t type, paddr_t start,
		 paddr_t end, memdb_fnptr fn, void *arg);
