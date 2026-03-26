// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef register_t

#include <allocator.h>
#include <memdb.h>

// MEMDB alignment requirements
#define MEMDB_ALIGNMENT_MASK ((1 << MEMDB_MIN_BITS) - 1)

// Forward declarations for stub functions
error_t
memdb_init(void);
error_t
allocator_heap_add_memory(allocator_t *allocator, void *base, size_t size);
void
memdb_set_test_partition(partition_t *partition);

// Test framework globals
static int tests_passed = 0;
static int tests_failed = 0;

// Test result tracking
#define TEST_ASSERT(condition, message)                                        \
	do {                                                                   \
		if (condition) {                                               \
			printf("✓ PASS: %s\n", message);                       \
			tests_passed++;                                        \
		} else {                                                       \
			printf("✗ FAIL: %s\n", message);                       \
			tests_failed++;                                        \
		}                                                              \
	} while (0)

// Helper function for memory walk callback
static paddr_t returned_base;
static size_t  returned_size;
static int     walk_call_count;

static error_t
test_walk_callback(paddr_t base, size_t size, void *arg)
{
	(void)arg;
	printf("  Walk callback: base=0x%lx, size=0x%lx\n", base, size);
	returned_base = base;
	returned_size = size;
	return OK;
}

static error_t
counting_walk_callback(paddr_t base, size_t size, void *arg)
{
	int *count = (int *)arg;
	(*count)++;
	printf("  Counting callback #%d: base=0x%lx, size=0x%lx\n", *count,
	       base, size);
	return OK;
}

// Test 1: Basic insertion of two ranges
static int
test_basic_insertion(void)
{
	printf("\n=== Test 1: Basic Insertion ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Allocate test objects
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res1 = allocator_allocate_object(
		&partition.allocator, 1024, alignment, hint);
	TEST_ASSERT(res1.e == OK, "First object allocation");

	void_ptr_result_t res2 = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res2.e == OK, "Second object allocation");

	// Insert first range
	uint64_t start_addr1 = (uint64_t)res1.r;
	uint64_t end_addr1   = start_addr1 + 1023;
	ret = memdb_insert(&partition, start_addr1, end_addr1,
			   (uintptr_t)res1.r, MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "First range insertion");

	// Insert second range
	uint64_t start_addr2 = (uint64_t)res2.r;
	uint64_t end_addr2   = start_addr2 + 4095;
	ret = memdb_insert(&partition, start_addr2, end_addr2,
			   (uintptr_t)res2.r, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Second range insertion");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 2: Update operations
static int
test_update_operations(void)
{
	printf("\n=== Test 2: Update Operations ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Insert initial range
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = start_addr + pool_size - 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Initial range insertion");

	// Allocate objects for updates
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res1 = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res1.e == OK, "First update object allocation");

	void_ptr_result_t res2 = allocator_allocate_object(
		&partition.allocator, 1024, alignment, hint);
	TEST_ASSERT(res2.e == OK, "Second update object allocation");

	// Update first sub-range
	uint64_t update_start1 = (uint64_t)res1.r;
	uint64_t update_end1   = update_start1 + 4095;
	ret = memdb_update(&partition, update_start1, update_end1,
			   (uintptr_t)res1.r, MEMDB_TYPE_ALLOCATOR, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "First range update");

	// Update second sub-range
	uint64_t update_start2 = (uint64_t)res2.r;
	uint64_t update_end2   = update_start2 + 1023;
	ret = memdb_update(&partition, update_start2, update_end2,
			   (uintptr_t)res2.r, MEMDB_TYPE_EXTENT, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Second range update");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 3: Lookup operations
static int
test_lookup_operations(void)
{
	printf("\n=== Test 3: Lookup Operations ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 16;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Insert initial range
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = start_addr + pool_size - 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Initial range insertion");

	// Allocate and update a sub-range
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res  = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res.e == OK, "Object allocation for lookup test");

	uint64_t update_start = (uint64_t)res.r;
	uint64_t update_end   = update_start + 4095;
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res.r, MEMDB_TYPE_ALLOCATOR, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Range update for lookup test");

	// Test lookup in updated range
	memdb_obj_type_result_t lookup_result = memdb_lookup(update_start);
	TEST_ASSERT(lookup_result.e == OK, "Lookup in updated range");
	TEST_ASSERT(lookup_result.r.type == MEMDB_TYPE_ALLOCATOR,
		    "Lookup returns correct type");
	TEST_ASSERT(lookup_result.r.object == (uintptr_t)res.r,
		    "Lookup returns correct object");

	// Test lookup in original range (use an address definitely outside the
	// updated range)
	uint64_t original_range_addr = update_end + 4096; // Well beyond the
							  // updated range
	lookup_result = memdb_lookup(original_range_addr);
	TEST_ASSERT(lookup_result.e == OK, "Lookup in original range");
	TEST_ASSERT(lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Original range has correct type");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 4: Contiguity checking
static int
test_contiguity_checking(void)
{
	printf("\n=== Test 4: Contiguity Checking ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Insert initial range
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = start_addr + pool_size - 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Initial range insertion");

	// Allocate and update a sub-range
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res  = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res.e == OK, "Object allocation for contiguity test");

	uint64_t update_start = (uint64_t)res.r;
	uint64_t update_end   = update_start + 4095;
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res.r, MEMDB_TYPE_ALLOCATOR, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Range update for contiguity test");

	// Test contiguity - should succeed for exact range
	bool is_contiguous = memdb_is_ownership_contiguous(
		update_start, update_end, (uintptr_t)res.r,
		MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(is_contiguous, "Contiguity check for exact range");

	// Test contiguity - should fail for extended range
	is_contiguous = memdb_is_ownership_contiguous(update_start - 1,
						      update_end,
						      (uintptr_t)res.r,
						      MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(!is_contiguous,
		    "Contiguity check fails for extended range");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 5: Memory walk operations
static int
test_memory_walk(void)
{
	printf("\n=== Test 5: Memory Walk Operations ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Insert initial range
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = start_addr + pool_size - 1;
	uint64_t range_size = end_addr - start_addr + 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Initial range insertion");

	// Test memory walk
	printf("  Testing memdb_walk for PARTITION type...\n");
	error_t walk_result = memdb_walk(block, MEMDB_TYPE_PARTITION,
					 test_walk_callback, NULL);
	TEST_ASSERT(walk_result == OK, "Memory walk operation");
	TEST_ASSERT(returned_base == start_addr,
		    "Walk returns correct base address");
	TEST_ASSERT(returned_size == range_size, "Walk returns correct size");

	// Allocate and update a sub-range
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res  = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res.e == OK, "Object allocation for walk test");

	uint64_t update_start = (uint64_t)res.r;
	uint64_t update_end   = update_start + 4095;
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res.r, MEMDB_TYPE_ALLOCATOR, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Range update for walk test");

	// Test range walk
	printf("  Testing memdb_range_walk for ALLOCATOR type...\n");
	walk_result = memdb_range_walk((uintptr_t)res.r, MEMDB_TYPE_ALLOCATOR,
				       update_start, update_end,
				       test_walk_callback, NULL);
	TEST_ASSERT(walk_result == OK, "Range walk operation");
	TEST_ASSERT(returned_base == update_start,
		    "Range walk returns correct base");
	TEST_ASSERT(returned_size == (update_end - update_start + 1),
		    "Range walk returns correct size");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 6: Error conditions
static int
test_error_conditions(void)
{
	printf("\n=== Test 6: Error Conditions ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	// Initialize memdb
	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	// Set the test partition for bitmap implementation
	memdb_set_test_partition(&partition);

	// Insert initial range
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = start_addr + pool_size - 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Initial range insertion");

	// Try to insert overlapping range - should fail
	ret = memdb_insert(&partition, start_addr + 1000, end_addr - 1000,
			   0xdeadbeef, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret != 0, "Overlapping insertion should fail");

	// Try to update with wrong previous owner - should fail
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res  = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res.e == OK, "Object allocation for error test");

	uint64_t update_start = (uint64_t)res.r;
	uint64_t update_end   = update_start + 4095;
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res.r, MEMDB_TYPE_ALLOCATOR, 0xdeadbeef,
			   MEMDB_TYPE_EXTENT);
	TEST_ASSERT(ret != 0, "Update with wrong previous owner should fail");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 7: Boundary conditions and alignment
static int
test_boundary_conditions(void)
{
	printf("\n=== Test 7: Boundary Conditions and Alignment ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096; // Use page alignment
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	// Setup memory pool
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Test 7a: Minimum size range (single page)
	uint64_t min_start = block;
	uint64_t min_end   = min_start + 4095; // Single page
	ret		   = memdb_insert(&partition, min_start, min_end, block,
					  MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert minimum size range (4KB)");

	// Test 7b: Invalid range (start >= end)
	ret = memdb_insert(&partition, block + 8192, block + 8191, 0xbadcafe,
			   MEMDB_TYPE_TRACE);
	TEST_ASSERT(ret != 0, "Invalid range (start >= end) should fail");

	// Test 7c: Zero-sized range
	ret = memdb_insert(&partition, block + 8192, block + 8192, 0xfeedface,
			   MEMDB_TYPE_EXTENT);
	TEST_ASSERT(ret != 0, "Zero-sized range should fail");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 8: Multiple object types
static int
test_multiple_object_types(void)
{
	printf("\n=== Test 8: Multiple Object Types ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Insert ranges with different types
	uint64_t base_addr = block;

	// PARTITION type
	ret = memdb_insert(&partition, base_addr, base_addr + 4095, 0x1000,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert PARTITION type");

	// ALLOCATOR type
	ret = memdb_insert(&partition, base_addr + 4096, base_addr + 8191,
			   0x2000, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Insert ALLOCATOR type");

	// EXTENT type
	ret = memdb_insert(&partition, base_addr + 8192, base_addr + 12287,
			   0x3000, MEMDB_TYPE_EXTENT);
	TEST_ASSERT(ret == 0, "Insert EXTENT type");

	// TRACE type
	ret = memdb_insert(&partition, base_addr + 12288, base_addr + 16383,
			   0x4000, MEMDB_TYPE_TRACE);
	TEST_ASSERT(ret == 0, "Insert TRACE type");

	// Verify lookups return correct types
	memdb_obj_type_result_t lookup_result;

	lookup_result = memdb_lookup(base_addr + 2048);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Lookup PARTITION type");

	lookup_result = memdb_lookup(base_addr + 6144);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_ALLOCATOR,
		    "Lookup ALLOCATOR type");

	lookup_result = memdb_lookup(base_addr + 10240);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_EXTENT,
		    "Lookup EXTENT type");

	lookup_result = memdb_lookup(base_addr + 14336);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_TRACE,
		    "Lookup TRACE type");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 9: Update rollback scenarios
static int
test_update_rollback(void)
{
	printf("\n=== Test 9: Update Rollback Scenarios ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Insert two non-contiguous ranges
	uint64_t range1_start = block;
	uint64_t range1_end   = block + 4095;
	uint64_t range2_start = block + 8192;
	uint64_t range2_end   = block + 12287;

	ret = memdb_insert(&partition, range1_start, range1_end, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert first range");

	ret = memdb_insert(&partition, range2_start, range2_end, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert second range");

	// Try to update across the gap (should fail due to non-contiguity)
	uint64_t update_start = range1_start;
	uint64_t update_end   = range2_end;
	ret = memdb_update(&partition, update_start, update_end, 0xdeadbeef,
			   MEMDB_TYPE_ALLOCATOR, block, MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret != 0,
		    "Update across non-contiguous ranges should fail");

	// Verify original ranges are still intact
	memdb_obj_type_result_t lookup_result;
	lookup_result = memdb_lookup(range1_start + 2048);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "First range still has original type after failed update");

	lookup_result = memdb_lookup(range2_start + 2048);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Second range still has original type after failed update");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 10: Large address ranges and guards
static int
test_large_address_ranges(void)
{
	printf("\n=== Test 10: Large Address Ranges and Guards ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Test with large address ranges that would require guards
	// Note: Addresses must satisfy alignment requirements:
	// - start_addr must be 16-byte aligned
	// - end_addr + 1 must be 16-byte aligned
	uint64_t large_start = 0x0000000000000000ULL; // 16-byte aligned
	uint64_t large_end   = 0x0000000FFFFFFFEFULL; // end_addr + 1 =
						      // 0x0000000FFFFFF0F0
						      // (16-byte aligned)

	ret = memdb_insert(&partition, large_start, large_end, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert large address range");

	// Test lookup at various points in the large range
	memdb_obj_type_result_t lookup_result;

	lookup_result = memdb_lookup(0x0000000000001000ULL);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Lookup at start of large range");

	lookup_result = memdb_lookup(0x0000000080000000ULL);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Lookup at middle of large range");

	lookup_result = memdb_lookup(0x0000000FFFFEF000ULL);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Lookup near end of large range");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 11: Complex update sequences
static int
test_complex_update_sequences(void)
{
	printf("\n=== Test 11: Complex Update Sequences ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Insert large initial range
	uint64_t start_addr = block;
	uint64_t end_addr   = start_addr + (20 * 4096) - 1; // 20 pages
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert large initial range");

	// Allocate objects for complex updates
	allocator_hint_t  hint = allocator_hint_default();
	void_ptr_result_t res1 = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	void_ptr_result_t res2 = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	void_ptr_result_t res3 = allocator_allocate_object(
		&partition.allocator, 4096, alignment, hint);
	TEST_ASSERT(res1.e == OK && res2.e == OK && res3.e == OK,
		    "Object allocations for complex updates");

	// Update sequence: partition -> allocator -> extent -> back to
	// partition
	uint64_t update_start = (uint64_t)res1.r;
	uint64_t update_end   = update_start + 4095;

	// Step 1: partition -> allocator
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res1.r, MEMDB_TYPE_ALLOCATOR, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Update partition -> allocator");

	// Step 2: allocator -> extent
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res2.r, MEMDB_TYPE_EXTENT,
			   (uintptr_t)res1.r, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Update allocator -> extent");

	// Step 3: extent -> trace
	ret = memdb_update(&partition, update_start, update_end,
			   (uintptr_t)res3.r, MEMDB_TYPE_TRACE,
			   (uintptr_t)res2.r, MEMDB_TYPE_EXTENT);
	TEST_ASSERT(ret == 0, "Update extent -> trace");

	// Step 4: trace -> back to partition
	ret = memdb_update(&partition, update_start, update_end, block,
			   MEMDB_TYPE_PARTITION, (uintptr_t)res3.r,
			   MEMDB_TYPE_TRACE);
	TEST_ASSERT(ret == 0, "Update trace -> partition");

	// Verify final state
	memdb_obj_type_result_t lookup_result =
		memdb_lookup(update_start + 2048);
	TEST_ASSERT(lookup_result.e == OK &&
			    lookup_result.r.type == MEMDB_TYPE_PARTITION,
		    "Final state is partition type");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 12: Memory walk with fragmented ranges
static int
test_fragmented_memory_walk(void)
{
	printf("\n=== Test 12: Memory Walk with Fragmented Ranges ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Create fragmented ranges with same object/type
	uint64_t  base_addr   = block;
	uintptr_t test_object = 0xcafebabe;

	// Insert multiple non-contiguous ranges with same object
	ret = memdb_insert(&partition, base_addr, base_addr + 4095, test_object,
			   MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Insert first fragment");

	ret = memdb_insert(&partition, base_addr + 8192, base_addr + 12287,
			   test_object, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Insert second fragment");

	ret = memdb_insert(&partition, base_addr + 16384, base_addr + 20479,
			   test_object, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(ret == 0, "Insert third fragment");

	// Insert different object in gaps
	ret = memdb_insert(&partition, base_addr + 4096, base_addr + 8191,
			   0xdeadbeef, MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert gap with different object");

	// Test memory walk - should find all fragments of the same object
	int	walk_count  = 0;
	error_t walk_result = memdb_walk(test_object, MEMDB_TYPE_ALLOCATOR,
					 counting_walk_callback, &walk_count);
	TEST_ASSERT(walk_result == OK, "Fragmented memory walk operation");
	TEST_ASSERT(walk_count == 3, "Walk found all three fragments");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 13: Range walk boundary conditions
static int
test_range_walk_boundaries(void)
{
	printf("\n=== Test 13: Range Walk Boundary Conditions ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Insert a large range
	uint64_t start_addr = block;
	uint64_t end_addr   = start_addr + (10 * 4096) - 1;
	ret = memdb_insert(&partition, start_addr, end_addr, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert large range for boundary testing");

	// Test range walk with exact boundaries
	walk_call_count	    = 0;
	error_t walk_result = memdb_range_walk(block, MEMDB_TYPE_PARTITION,
					       start_addr, end_addr,
					       test_walk_callback, NULL);
	TEST_ASSERT(walk_result == OK, "Range walk with exact boundaries");
	TEST_ASSERT(returned_base == start_addr &&
			    returned_size == (end_addr - start_addr + 1),
		    "Range walk returns correct range");

	// Test range walk with subset
	uint64_t subset_start = start_addr + 4096;
	uint64_t subset_end   = start_addr + (3 * 4096) - 1;
	walk_result	      = memdb_range_walk(block, MEMDB_TYPE_PARTITION,
						 subset_start, subset_end,
						 test_walk_callback, NULL);
	TEST_ASSERT(walk_result == OK, "Range walk with subset");
	TEST_ASSERT(returned_base == subset_start &&
			    returned_size == (subset_end - subset_start + 1),
		    "Range walk returns correct subset");

	// Test range walk beyond boundaries (should find partial match)
	uint64_t extended_start = start_addr - 4096;
	uint64_t extended_end	= end_addr + 4096;
	walk_result		= memdb_range_walk(block, MEMDB_TYPE_PARTITION,
						   extended_start, extended_end,
						   test_walk_callback, NULL);
	TEST_ASSERT(walk_result == OK, "Range walk beyond boundaries");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 14: Stress test with many small ranges
static int
test_stress_many_ranges(void)
{
	printf("\n=== Test 14: Stress Test with Many Small Ranges ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Insert many small ranges (page-sized)
	// Use constant address ranges separate from allocator memory to avoid
	// conflicts Objects must be aligned to sizeof(uintptr_t) = 8 bytes
	const int num_ranges = 50;
	uint64_t  base_addr = 0x10000000ULL; // Start at 256MB, properly aligned

	for (int i = 0; i < num_ranges; i++) {
		uint64_t  range_start = base_addr + (i * 4096);
		uint64_t  range_end   = range_start + 4095;
		uintptr_t object = 0x1000 + (i * 8); // 8-byte aligned objects

		ret = memdb_insert(&partition, range_start, range_end, object,
				   MEMDB_TYPE_PARTITION);
		if (ret != OK) {
			printf("  Failed to insert range %d at 0x%lx (object=0x%lx)\n",
			       i, range_start, object);
			printf("    Range: 0x%lx - 0x%lx\n", range_start,
			       range_end);
			printf("    Error code: %d\n", ret);
			break;
		}
	}
	TEST_ASSERT(ret == 0, "Insert many small ranges");

	// Verify random lookups
	bool all_lookups_ok = true;
	for (int i = 0; i < (num_ranges / 5); i++) {
		int	  range_idx = i * 5; // Test every 5th range
		uintptr_t expected  = 0x1000 + (range_idx * 8); // Match the
							       // 8-byte aligned
							       // objects
		uint64_t test_addr = base_addr + (range_idx * 4096) +
				     2048; // Middle of range

		memdb_obj_type_result_t lookup_result = memdb_lookup(test_addr);
		if (lookup_result.e != OK ||
		    lookup_result.r.type != MEMDB_TYPE_PARTITION ||
		    lookup_result.r.object != expected) {
			all_lookups_ok = false;
			printf("  Lookup failed for range %d at addr 0x%lx\n",
			       range_idx, test_addr);
			printf("    Expected: object=0x%lx, type=%d\n",
			       expected, MEMDB_TYPE_PARTITION);
			if (lookup_result.e == OK) {
				printf("    Got: object=0x%lx, type=%d\n",
				       lookup_result.r.object,
				       lookup_result.r.type);
			} else {
				printf("    Lookup returned error: %d\n",
				       lookup_result.e);
			}
			break;
		}
	}
	TEST_ASSERT(all_lookups_ok, "Random lookups in stress test");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Test 15: Edge cases and corner conditions
static int
test_edge_cases(void)
{
	printf("\n=== Test 15: Edge Cases and Corner Conditions ===\n");

	partition_t partition;
	int	    ret	      = 0;
	size_t	    pool_size = 4096 * 100;

	memset(&partition, 0, sizeof(partition));

	uintptr_t block = (uintptr_t)malloc(pool_size);
	TEST_ASSERT(block != 0, "Memory allocation for pool");

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	TEST_ASSERT(ret == 0, "Adding memory to heap");

	ret = memdb_init();
	TEST_ASSERT(ret == 0, "Memory database initialization");

	memdb_set_test_partition(&partition);

	// Test 15a: Lookup in empty database
	memdb_obj_type_result_t lookup_result = memdb_lookup(0x1000);
	TEST_ASSERT(lookup_result.e != OK,
		    "Lookup in empty database should fail");

	// Test 15b: Walk with invalid object type
	error_t walk_result =
		memdb_walk(0x1000, MEMDB_TYPE_NOTYPE, test_walk_callback, NULL);
	TEST_ASSERT(walk_result != OK, "Walk with NOTYPE should fail");

	// Test 15c: Insert with same start and end (after alignment)
	uint64_t aligned_addr = (block + 4095) & ~4095ULL; // Align to 4K
	ret = memdb_insert(&partition, aligned_addr, aligned_addr + 4095, block,
			   MEMDB_TYPE_PARTITION);
	TEST_ASSERT(ret == 0, "Insert single page range");

	// Test 15d: Contiguity check with wrong object
	bool is_contiguous =
		memdb_is_ownership_contiguous(aligned_addr, aligned_addr + 4095,
					      0xbadcafe, MEMDB_TYPE_PARTITION);
	TEST_ASSERT(!is_contiguous,
		    "Contiguity check with wrong object should fail");

	// Test 15e: Contiguity check with wrong type
	is_contiguous = memdb_is_ownership_contiguous(
		aligned_addr, aligned_addr + 4095, block, MEMDB_TYPE_ALLOCATOR);
	TEST_ASSERT(!is_contiguous,
		    "Contiguity check with wrong type should fail");

	free((void *)block);
	return (tests_failed == 0) ? 0 : -1;
}

// Main test runner
int
main(void)
{
	printf("=== MEMDB Comprehensive Host Tests ===\n");
	printf("Testing memory database functionality with extensive coverage...\n");

	// Run all tests - original 6 tests
	test_basic_insertion();
	test_update_operations();
	test_lookup_operations();
	test_contiguity_checking();
	test_memory_walk();
	test_error_conditions();

	// Run additional comprehensive tests - 9 new tests
	test_boundary_conditions();
	test_multiple_object_types();
	test_update_rollback();
	test_large_address_ranges();
	test_complex_update_sequences();
	test_fragmented_memory_walk();
	test_range_walk_boundaries();
	test_stress_many_ranges();
	test_edge_cases();

	// Print summary
	printf("\n=== Test Summary ===\n");
	printf("Tests passed: %d\n", tests_passed);
	printf("Tests failed: %d\n", tests_failed);
	printf("Total tests run: 15 (6 original + 9 comprehensive)\n");

	if (tests_failed == 0) {
		printf("SUCCESS: All MEMDB tests completed successfully!\n");
		return 0;
	} else {
		printf("FAILURE: %d test(s) failed.\n", tests_failed);
		return 1;
	}
}
