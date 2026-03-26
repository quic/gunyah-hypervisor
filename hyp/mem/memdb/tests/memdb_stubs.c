// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Stub implementations for memdb functions for host testing

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef register_t

#include <allocator.h>
#include <memdb.h>

// Simple memory database implementation for testing
// This is a simplified version that maintains a list of memory ranges

#define MAX_MEMDB_ENTRIES 1000

// Use a different name to avoid conflict with the generated memdb_entry_t
typedef struct memdb_test_entry {
	paddr_t	     start_addr;
	paddr_t	     end_addr;
	uintptr_t    object;
	memdb_type_t type;
	bool	     valid;
} memdb_test_entry_t;

static memdb_test_entry_t memdb_entries[MAX_MEMDB_ENTRIES];
static size_t		  memdb_entry_count = 0;
static bool		  memdb_initialized = false;

// Initialize the memory database
error_t
memdb_init(void)
{
	memdb_entry_count = 0;
	memdb_initialized = true;
	memset(memdb_entries, 0, sizeof(memdb_entries));
	return OK;
}

// Insert a new range into the database
error_t
memdb_insert(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type)
{
	(void)partition; // Unused in stub

	if (!memdb_initialized) {
		return ERROR_OBJECT_STATE;
	}

	if (memdb_entry_count >= MAX_MEMDB_ENTRIES) {
		return ERROR_NOMEM;
	}

	// Check for overlaps with existing entries
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		// Check if ranges overlap
		if (!(end_addr < memdb_entries[i].start_addr ||
		      start_addr > memdb_entries[i].end_addr)) {
			return ERROR_BUSY; // Range already occupied
		}
	}

	// Add new entry
	memdb_entries[memdb_entry_count].start_addr = start_addr;
	memdb_entries[memdb_entry_count].end_addr   = end_addr;
	memdb_entries[memdb_entry_count].object	    = object;
	memdb_entries[memdb_entry_count].type	    = obj_type;
	memdb_entries[memdb_entry_count].valid	    = true;
	memdb_entry_count++;

	return OK;
}

// Update ownership of a range
error_t
memdb_update(partition_t *partition, paddr_t start_addr, paddr_t end_addr,
	     uintptr_t object, memdb_type_t obj_type, uintptr_t prev_object,
	     memdb_type_t prev_type)
{
	(void)partition; // Unused in stub

	if (!memdb_initialized) {
		return ERROR_OBJECT_STATE;
	}

	// Find entries that cover this range and verify they match
	// prev_object/prev_type
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		// Check if this entry overlaps with the update range
		if (!(end_addr < memdb_entries[i].start_addr ||
		      start_addr > memdb_entries[i].end_addr)) {
			// Verify the existing entry matches the expected
			// previous owner
			if (memdb_entries[i].object != prev_object ||
			    memdb_entries[i].type != prev_type) {
				return ERROR_MEMDB_NOT_OWNER;
			}

			// For simplicity, we'll just update the entire
			// overlapping entry In a real implementation, this
			// would need to handle partial updates
			if (start_addr <= memdb_entries[i].start_addr &&
			    end_addr >= memdb_entries[i].end_addr) {
				// Complete overlap - update the entry
				memdb_entries[i].object = object;
				memdb_entries[i].type	= obj_type;
			} else {
				// Partial overlap - for simplicity, we'll
				// create a new entry and mark the old one as
				// invalid
				if (memdb_entry_count >= MAX_MEMDB_ENTRIES) {
					return ERROR_NOMEM;
				}

				memdb_entries[memdb_entry_count].start_addr =
					start_addr;
				memdb_entries[memdb_entry_count].end_addr =
					end_addr;
				memdb_entries[memdb_entry_count].object =
					object;
				memdb_entries[memdb_entry_count].type =
					obj_type;
				memdb_entries[memdb_entry_count].valid = true;
				memdb_entry_count++;
			}
		}
	}

	return OK;
}

// Lookup the owner of a specific address
memdb_obj_type_result_t
memdb_lookup(paddr_t addr)
{
	memdb_obj_type_result_t result = { .e = ERROR_MEMDB_EMPTY };

	if (!memdb_initialized) {
		result.e = ERROR_OBJECT_STATE;
		return result;
	}

	// Find the entry that contains this address
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		if (addr >= memdb_entries[i].start_addr &&
		    addr <= memdb_entries[i].end_addr) {
			result.e	= OK;
			result.r.object = memdb_entries[i].object;
			result.r.type	= memdb_entries[i].type;
			return result;
		}
	}

	result.e = ERROR_MEMDB_EMPTY;
	return result;
}

// Check if a range is contiguously owned by the specified object
bool
memdb_is_ownership_contiguous(paddr_t start_addr, paddr_t end_addr,
			      uintptr_t object, memdb_type_t type)
{
	if (!memdb_initialized) {
		return false;
	}

	// For simplicity, check if there's exactly one entry that covers the
	// entire range
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		if (memdb_entries[i].start_addr <= start_addr &&
		    memdb_entries[i].end_addr >= end_addr &&
		    memdb_entries[i].object == object &&
		    memdb_entries[i].type == type) {
			return true;
		}
	}

	return false;
}

// Walk through all ranges owned by the specified object
error_t
memdb_walk(uintptr_t object, memdb_type_t type, memdb_fnptr fn, void *arg)
{
	if (!memdb_initialized) {
		return ERROR_OBJECT_STATE;
	}

	if (fn == NULL) {
		return ERROR_ARGUMENT_INVALID;
	}

	// Call the callback for each matching entry
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		if (memdb_entries[i].object == object &&
		    memdb_entries[i].type == type) {
			size_t	size = memdb_entries[i].end_addr -
				       memdb_entries[i].start_addr + 1;
			error_t result =
				fn(memdb_entries[i].start_addr, size, arg);
			if (result != OK) {
				return result;
			}
		}
	}

	return OK;
}

// Walk through ranges owned by the specified object within a given range
error_t
memdb_range_walk(uintptr_t object, memdb_type_t type, paddr_t start_addr,
		 paddr_t end_addr, memdb_fnptr fn, void *arg)
{
	if (!memdb_initialized) {
		return ERROR_OBJECT_STATE;
	}

	if (fn == NULL) {
		return ERROR_ARGUMENT_INVALID;
	}

	// Call the callback for each matching entry that intersects with the
	// range
	for (size_t i = 0; i < memdb_entry_count; i++) {
		if (!memdb_entries[i].valid) {
			continue;
		}

		if (memdb_entries[i].object == object &&
		    memdb_entries[i].type == type) {
			// Check if this entry intersects with the specified
			// range
			paddr_t intersect_start =
				(memdb_entries[i].start_addr > start_addr)
					? memdb_entries[i].start_addr
					: start_addr;
			paddr_t intersect_end =
				(memdb_entries[i].end_addr < end_addr)
					? memdb_entries[i].end_addr
					: end_addr;

			if (intersect_start <= intersect_end) {
				size_t size =
					intersect_end - intersect_start + 1;
				error_t result = fn(intersect_start, size, arg);
				if (result != OK) {
					return result;
				}
			}
		}
	}

	return OK;
}

// Function to set the test partition (called from memdb functions)
void
memdb_set_test_partition(partition_t *partition)
{
	// No op
}
