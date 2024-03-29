// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#if defined(UNIT_TESTS)

#include <assert.h>
#include <hyptypes.h>
#include <limits.h>
#include <string.h>

#include <compiler.h>
#include <cpulocal.h>
#include <log.h>
#include <memdb.h>
#include <panic.h>
#include <partition.h>
#include <partition_init.h>
#include <rcu.h>
#include <scheduler.h>
#include <spinlock.h>
#include <trace.h>
#include <util.h>

#include "event_handlers.h"

static count_t test_memdb_count;

static partition_t dummy_partition_1;
static partition_t dummy_partition_2;
static allocator_t dummy_allocator;

void
memdb_handle_tests_init(void)
{
	test_memdb_count = 0;
}

static error_t
memdb_test_add_free_range(paddr_t base, size_t size, void *arg)
{
	error_t	      ret	  = OK;
	memdb_data_t *memdb_data  = (memdb_data_t *)arg;
	bool	      first_entry = true;

	if ((size == 0U) && (util_add_overflows(base, size - 1))) {
		ret = ERROR_ARGUMENT_SIZE;
		goto error;
	}

	index_t index = memdb_data->ranges_count;

	if (index != 0U) {
		index--;
		first_entry = false;
	}

	if (!first_entry) {
		index++;
	}
	if (index >= MEMDB_RANGES_NUM) {
		LOG(ERROR, WARN, "memdb_data: no more free ranges");
	} else {
		memdb_data->ranges[index].base = base;
		memdb_data->ranges[index].size = size;
		memdb_data->ranges_count++;
		LOG(DEBUG, INFO, "range: [{:#x}..{:#x}]", base,
		    base + size - 1U);
	}

error:
	return ret;
}

static void
get_inserted_ranges(memdb_data_t *memdb_data, uintptr_t object,
		    memdb_type_t type)
{
	count_t count = memdb_data->ranges_count;

	if (memdb_walk(object, type, memdb_test_add_free_range,
		       (void *)memdb_data) != OK) {
		panic("Error doing the memory database walk");
	}

	for (index_t i = count; i < memdb_data->ranges_count; i++) {
		memdb_data->ranges[i].obj  = object;
		memdb_data->ranges[i].type = type;

		// Double check that the ranges are correct by checking if the
		// ranges are contiguous
		paddr_t start_addr = memdb_data->ranges[i].base;
		paddr_t end_addr   = memdb_data->ranges[i].base +
				   memdb_data->ranges[i].size - 1U;

		bool cont = memdb_is_ownership_contiguous(start_addr, end_addr,
							  object, type);
		if (!cont) {
			LOG(DEBUG, INFO,
			    "<<< BUG!! range {:#x}..{:#x} should be contiguouos",
			    start_addr, end_addr);
			assert(cont);
		}
	}
}

static void
check_ranges_in_memdb(memdb_data_t *memdb_data)
{
	memset(memdb_data, 0, sizeof(memdb_data_t));

	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- DUMMY PARTITION 1 --");
	get_inserted_ranges(memdb_data, (uintptr_t)&dummy_partition_1,
			    MEMDB_TYPE_PARTITION);

	LOG(DEBUG, INFO, "-- DUMMY PARTITION 2 --");
	get_inserted_ranges(memdb_data, (uintptr_t)&dummy_partition_2,
			    MEMDB_TYPE_PARTITION);

	LOG(DEBUG, INFO, "-- DUMMY ALLOCATOR --");
	get_inserted_ranges(memdb_data, (uintptr_t)&dummy_allocator,
			    MEMDB_TYPE_ALLOCATOR);
	LOG(DEBUG, INFO, "---------------------------------------------------");
}

static bool
is_range_in_memdb(memdb_data_t *memdb_data, paddr_t start_addr,
		  paddr_t end_addr)
{
	bool is_range_in_memdb = false;

	for (index_t i = 0; i < memdb_data->ranges_count; i++) {
		paddr_t start = memdb_data->ranges[i].base;
		paddr_t end   = memdb_data->ranges[i].base +
			      memdb_data->ranges[i].size - 1U;

		if (((start <= start_addr) && (end >= start_addr)) ||
		    ((start <= end_addr) && (end >= end_addr))) {
			LOG(DEBUG, INFO,
			    "Range {:#x}..{:#x} already used in {:#x}..{:#x}",
			    start_addr, end_addr, start, end);
			is_range_in_memdb = true;
			break;
		}
	}

	return is_range_in_memdb;
}

static void
memdb_test1(void)
{
	LOG(DEBUG, INFO, " Start TEST 1:");

	// Use addresses in: (0x300000000..0x5FFFFFFFFF)

	partition_t	       *hyp_partition = partition_get_private();
	paddr_t			start_addr    = 0U;
	paddr_t			end_addr      = 0U;
	uintptr_t		obj;
	memdb_type_t		type;
	uintptr_t		prev_obj;
	memdb_type_t		prev_type;
	error_t			err = OK;
	memdb_obj_type_result_t res;
	bool			cont;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	// In the tests we are going to use addresses within
	// (0x300000000..0x5FFFFFFFFF (one bit more)). We must make sure that
	// this range is not already in the memdb.

	start_addr = 0x3000000000;
	end_addr   = 0x5FFFFFFFFF;

	// Check which ranges are already occupied in the memdb so that I do not
	// use them for the tests
	check_ranges_in_memdb(memdb_data);

	bool is_range_used =
		is_range_in_memdb(memdb_data, start_addr, end_addr);
	assert(!is_range_used);

	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	start_addr = 0x3000000000;
	end_addr   = 0x300003FFFF;
	obj	   = (uintptr_t)&dummy_partition_2;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);

	start_addr = 0x3000040000;
	end_addr   = 0x5FFFFFFFFFF;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Lookup an address from &dummy_partition_1 that I know it is not
	// explicitly in an entry since it is in a skipped level due to a guard;
	paddr_t addr = 0x3000100000;
	rcu_read_start();
	res = memdb_lookup(addr);
	assert(res.e == OK);
	assert(res.r.object == obj);
	assert(res.r.type == type);
	rcu_read_finish();

	// Update ownership of range in skipped levels

	start_addr = 0x3000100000;
	end_addr   = 0x3000aFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	start_addr = 0x3010000000;
	end_addr   = 0x33FFFFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);

	// Rollback ownership to root partition and see if the levels stay there
	// or the guard is set back and the levels are removed.

	start_addr = 0x3000100000;
	end_addr   = 0x3000aFFFFF;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_allocator;
	prev_type  = MEMDB_TYPE_ALLOCATOR;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	start_addr = 0x3010000000;
	end_addr   = 0x33FFFFFFFF;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_allocator;
	prev_type  = MEMDB_TYPE_ALLOCATOR;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Give all ownership to hyp partition of the ranges inserted in the
	// test
	start_addr = 0x3000040000;
	end_addr   = 0x5FFFFFFFFFF;
	obj	   = (uintptr_t)&dummy_partition_2;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);

	// Add a range that is not correct so that it needs to rollback all
	// previous entries.
	// Make it fail in the end address path
	start_addr = 0x3040000000;
	end_addr   = 0x6FFFFFFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;
	prev_obj   = (uintptr_t)&dummy_partition_2;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == ERROR_MEMDB_NOT_OWNER);

	// Check that the rollback was done correctly
	start_addr = 0x3000000000;
	end_addr   = 0x5FFFFFFFFFF;

	cont = memdb_is_ownership_contiguous(start_addr, end_addr, prev_obj,
					     prev_type);
	assert(cont);

	// Check if rollback left everything correct
	check_ranges_in_memdb(memdb_data);

	// Change ownership of a range in the middle to then add a second range
	// that fails in the start path
	start_addr = 0x3040000000;
	end_addr   = 0x30FFFFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;
	prev_obj   = (uintptr_t)&dummy_partition_2;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Make it fail in the start path
	start_addr = 0x3000000000;
	end_addr   = 0x5FFFFFFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;
	prev_obj   = (uintptr_t)&dummy_partition_2;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == ERROR_MEMDB_NOT_OWNER);

	start_addr = 0x3000000000;
	end_addr   = 0x303FFFFFFF;

	cont = memdb_is_ownership_contiguous(start_addr, end_addr, prev_obj,
					     prev_type);
	assert(cont);

	start_addr = 0x3040000000;
	end_addr   = 0x30FFFFFFFF;
	obj	   = (uintptr_t)&dummy_allocator;
	type	   = MEMDB_TYPE_ALLOCATOR;

	cont = memdb_is_ownership_contiguous(start_addr, end_addr, obj, type);
	assert(cont);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

// This kind of tests do:
// - Success cases:
// 1. memdb_insert with range and object specified in the input arguments, but
// with type MEMDB_TYPE_TRACE as type. [needs to return OK]
// 2. memdb_update of same range to now have the type specified in input. [needs
// to return OK]
// 3. memdb_lookup and memdb_is_ownership_contiguous to check if the range has
// been added properly and every single entry has been updated with the correct
// object and type. [needs to return OK]
// - Failure cases:
// 4. memdb_insert the same range again [needs to return ERROR_MEMDB_NOT_OWNER]
// 5. memdb_update of same range of incorrect values for prev type [needs to
// return ERROR_MEMDB_NOT_OWNER]
// - Last success case:
// - memdb_is_ownership_contiguous to verify that the failure cases did not
// change the memdb
static void
memdb_test_insert_update(memdb_data_t *test_data, paddr_t start, paddr_t end)
{
	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	assert(test_data->ranges_count != 0U);

	// Check which ranges are already occupied in the memdb so that I do not
	// use them for the tests
	check_ranges_in_memdb(memdb_data);

	bool is_range_used = is_range_in_memdb(memdb_data, start, end);
	assert(!is_range_used);

	rcu_read_start();
	memdb_obj_type_result_t res = memdb_lookup(start);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	for (index_t i = 0; i < test_data->ranges_count; i++) {
		paddr_t start_addr = test_data->ranges[i].base;
		paddr_t end_addr   = test_data->ranges[i].base +
				   test_data->ranges[i].size - 1U;
		uintptr_t    obj  = test_data->ranges[i].obj;
		memdb_type_t type = test_data->ranges[i].type;

		// Do not use MEMDB_TYPE_EXTENT for the tests since we are going
		// to create a counter example with it.
		assert(type != MEMDB_TYPE_EXTENT);

		// Success cases:
		error_t err = memdb_insert(hyp_partition, start_addr, end_addr,
					   obj, MEMDB_TYPE_TRACE);
		if (err != OK) {
			LOG(DEBUG, INFO,
			    " memdb_insert ret:{%d}, should have returned: {:%d}",
			    (register_t)err, OK);
		}
		assert(err == OK);

		err = memdb_update(hyp_partition, start_addr, end_addr, obj,
				   type, obj, MEMDB_TYPE_TRACE);
		if (err != OK) {
			LOG(DEBUG, INFO,
			    " memdb_update ret:{%d}, should have returned: {:%d}",
			    (register_t)err, OK);
		}
		assert(err == OK);

		rcu_read_start();
		res = memdb_lookup(start_addr);
		assert(res.e == OK);
		assert(res.r.object == obj);
		assert(res.r.type == type);
		rcu_read_finish();

		bool cont = memdb_is_ownership_contiguous(start_addr, end_addr,
							  obj, type);
		assert(cont);

		// Failure cases:
		err = memdb_insert(hyp_partition, start_addr, end_addr, obj,
				   MEMDB_TYPE_TRACE);
		if (err != ERROR_MEMDB_NOT_OWNER) {
			LOG(DEBUG, INFO,
			    " memdb_insert ret:{%d}, should have returned: {:%d}",
			    (register_t)err, ERROR_MEMDB_NOT_OWNER);
		}
		assert(err == ERROR_MEMDB_NOT_OWNER);

		err = memdb_update(hyp_partition, start_addr, end_addr, obj,
				   type, (uintptr_t)NULL, MEMDB_TYPE_EXTENT);
		if (err != ERROR_MEMDB_NOT_OWNER) {
			LOG(DEBUG, INFO,
			    " memdb_update ret:{%d}, should have returned: {:%d}",
			    (register_t)err, ERROR_MEMDB_NOT_OWNER);
		}
		assert(err == ERROR_MEMDB_NOT_OWNER);

		// Verify that the failure cases did not modify the memdb
		cont = memdb_is_ownership_contiguous(start_addr, end_addr, obj,
						     type);
		assert(cont);
	}

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test2(void)
{
	LOG(DEBUG, INFO, " Start TEST 2:");

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *test_data;
	size_t		  test_data_size = sizeof(*test_data);

	alloc_ret = partition_alloc(hyp_partition, test_data_size,
				    alignof(*test_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	test_data = (memdb_data_t *)alloc_ret.r;
	memset(test_data, 0, test_data_size);

	test_data->ranges[0].base = 0x3000000;
	test_data->ranges[0].size = 0x0086000;
	test_data->ranges[0].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[0].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[1].base = 0x5000000;
	test_data->ranges[1].size = 0x0080000;
	test_data->ranges[1].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[1].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[2].base = 0x5100000;
	test_data->ranges[2].size = 0x0180000;
	test_data->ranges[2].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[2].type = MEMDB_TYPE_PARTITION;

	test_data->ranges_count = 3;

	memdb_test_insert_update(test_data, test_data->ranges[0].base,
				 test_data->ranges[2].base +
					 test_data->ranges[2].size - 1);
	partition_free(hyp_partition, test_data, test_data_size);
}

static void
memdb_test3(void)
{
	LOG(DEBUG, INFO, " Start TEST 3:");

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *test_data;
	size_t		  test_data_size = sizeof(*test_data);

	alloc_ret = partition_alloc(hyp_partition, test_data_size,
				    alignof(*test_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	test_data = (memdb_data_t *)alloc_ret.r;
	memset(test_data, 0, test_data_size);

	test_data->ranges[0].base = 0xB00000000;
	test_data->ranges[0].size = 0x000860000;
	test_data->ranges[0].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[0].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[1].base = 0xB08800000;
	test_data->ranges[1].size = 0x03F580000;
	test_data->ranges[1].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[1].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[2].base = 0xC00DC0000;
	test_data->ranges[2].size = 0x000002000;
	test_data->ranges[2].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[2].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[3].base = 0xC00C10000;
	test_data->ranges[3].size = 0x000002000;
	test_data->ranges[3].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[3].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[4].base = 0xC18000000;
	test_data->ranges[4].size = 0x0BE800000;
	test_data->ranges[4].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[4].type = MEMDB_TYPE_PARTITION;

	test_data->ranges_count = 5;

	memdb_test_insert_update(test_data, test_data->ranges[0].base,
				 test_data->ranges[4].base +
					 test_data->ranges[4].size - 1);
	partition_free(hyp_partition, test_data, test_data_size);
}

static void
memdb_test4(void)
{
	LOG(DEBUG, INFO, " Start TEST 4:");

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *test_data;
	size_t		  test_data_size = sizeof(*test_data);

	alloc_ret = partition_alloc(hyp_partition, test_data_size,
				    alignof(*test_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	test_data = (memdb_data_t *)alloc_ret.r;
	memset(test_data, 0, test_data_size);

	test_data->ranges[0].base = 0x80000000000;
	test_data->ranges[0].size = 0x00860000000;
	test_data->ranges[0].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[0].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[1].base = 0x0C000000000000;
	test_data->ranges[1].size = 0x14000000000000;
	test_data->ranges[1].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[1].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[2].base = 0x80DDC00000000;
	test_data->ranges[2].size = 0x0000200000000;
	test_data->ranges[2].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[2].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[3].base = 0x80DC000000000;
	test_data->ranges[3].size = 0x0000300000000;
	test_data->ranges[3].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[3].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[4].base = 0x8088000000000;
	test_data->ranges[4].size = 0x0048000000000;
	test_data->ranges[4].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[4].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[5].base = 0x8240000000000;
	test_data->ranges[5].size = 0x3D60000000000;
	test_data->ranges[5].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[5].type = MEMDB_TYPE_PARTITION;

	test_data->ranges_count = 6;

	memdb_test_insert_update(test_data, test_data->ranges[0].base,
				 test_data->ranges[1].base +
					 test_data->ranges[1].size - 1);
	partition_free(hyp_partition, test_data, test_data_size);
}

static void
memdb_test5(void)
{
	// Test adding ranges that could possible fit in a single entry.

	LOG(DEBUG, INFO, " Start TEST 5:");

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *test_data;
	size_t		  test_data_size = sizeof(*test_data);

	alloc_ret = partition_alloc(hyp_partition, test_data_size,
				    alignof(*test_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	test_data = (memdb_data_t *)alloc_ret.r;
	memset(test_data, 0, test_data_size);

	test_data->ranges[0].base = 0x0;
	test_data->ranges[0].size = 0x1000;
	test_data->ranges[0].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[0].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[1].base = 0x1000;
	test_data->ranges[1].size = 0x1000;
	test_data->ranges[1].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[1].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[2].base = 0x20000;
	test_data->ranges[2].size = 0x10000;
	test_data->ranges[2].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[2].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[3].base = 0x300000;
	test_data->ranges[3].size = 0x100000;
	test_data->ranges[3].obj  = (uintptr_t)&dummy_partition_2;
	test_data->ranges[3].type = MEMDB_TYPE_PARTITION;

	test_data->ranges[4].base = 0x17C2000;
	test_data->ranges[4].size = 0x1000;
	test_data->ranges[4].obj  = (uintptr_t)&dummy_partition_1;
	test_data->ranges[4].type = MEMDB_TYPE_PARTITION;

	test_data->ranges_count = 5;

	memdb_test_insert_update(test_data, test_data->ranges[0].base,
				 test_data->ranges[5].base +
					 test_data->ranges[5].size - 1);
	partition_free(hyp_partition, test_data, test_data_size);
}

// Insert a new range and update within these ranges
static void
memdb_test_update(memdb_data_t *test_data, paddr_t start, paddr_t end,
		  memdb_type_t initial_type, uintptr_t initial_obj)
{
	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	// Check which ranges are already occupied in the memdb so that I do not
	// use them for the tests
	check_ranges_in_memdb(memdb_data);

	bool is_range_used = is_range_in_memdb(memdb_data, start, end);
	assert(!is_range_used);

	rcu_read_start();
	memdb_obj_type_result_t res = memdb_lookup(start);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	LOG(DEBUG, INFO, "<<< Adding range: {:#x}-{:#x}", start, end);

	error_t err = memdb_insert(hyp_partition, start, end, initial_obj,
				   initial_type);
	assert(err == OK);

	assert(test_data->ranges_count != 0U);

	// Update ownership
	for (index_t i = 0; i < test_data->ranges_count; i++) {
		paddr_t start_addr = test_data->ranges[i].base;
		paddr_t end_addr   = test_data->ranges[i].base +
				   test_data->ranges[i].size - 1U;
		uintptr_t    obj  = test_data->ranges[i].obj;
		memdb_type_t type = test_data->ranges[i].type;

		err = memdb_update(hyp_partition, start_addr, end_addr, obj,
				   type, initial_obj, initial_type);
		assert(err == OK);
	}

	// Check it has been added correctly
	for (index_t i = 0; i < test_data->ranges_count; i++) {
		paddr_t start_addr = test_data->ranges[i].base;
		paddr_t end_addr   = test_data->ranges[i].base +
				   test_data->ranges[i].size - 1U;
		uintptr_t    obj  = test_data->ranges[i].obj;
		memdb_type_t type = test_data->ranges[i].type;

		rcu_read_start();
		res = memdb_lookup(start_addr);
		assert(res.e == OK);
		assert(res.r.object == obj);
		assert(res.r.type == type);
		rcu_read_finish();

		bool cont = memdb_is_ownership_contiguous(start_addr, end_addr,
							  obj, type);
		assert(cont);
	}

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);

	// Rollback ownership
	for (index_t i = 0; i < test_data->ranges_count; i++) {
		paddr_t start_addr = test_data->ranges[i].base;
		paddr_t end_addr   = test_data->ranges[i].base +
				   test_data->ranges[i].size - 1U;
		uintptr_t    obj  = test_data->ranges[i].obj;
		memdb_type_t type = test_data->ranges[i].type;

		err = memdb_update(hyp_partition, start_addr, end_addr,
				   initial_obj, initial_type, obj, type);
		assert(err == OK);
	}

	rcu_read_start();
	res = memdb_lookup(start);
	assert(res.e == OK);
	assert(res.r.object == initial_obj);
	assert(res.r.type == initial_type);
	rcu_read_finish();

	bool cont = memdb_is_ownership_contiguous(start, end, initial_obj,
						  initial_type);
	assert(cont);

	// Check all ranges in memdb to see if everything has been done
	// correctly
	check_ranges_in_memdb(memdb_data);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test0(void)
{
	// Test inserting one range and then updated ownership of smaller ranges
	// within. When these smaller ranges update their ownership back to the
	// initial owner, the levels should collapse.

	LOG(DEBUG, INFO, " Start TEST 0:");

	partition_t *hyp_partition = partition_get_private();

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *test_data;
	size_t		  test_data_size = sizeof(*test_data);

	alloc_ret = partition_alloc(hyp_partition, test_data_size,
				    alignof(*test_data));
	if (alloc_ret.e != OK) {
		panic("Allocate memdb_data_t failed");
	}

	test_data = (memdb_data_t *)alloc_ret.r;
	memset(test_data, 0, test_data_size);

	test_data->ranges[0].base = 0x410fc4000;
	test_data->ranges[0].size = 0x1000;
	test_data->ranges[0].obj  = (uintptr_t)&dummy_partition_1;
	test_data->ranges[0].type = MEMDB_TYPE_TRACE;

	test_data->ranges[1].base = 0x57FFFf000;
	test_data->ranges[1].size = 0x1000;
	test_data->ranges[1].obj  = (uintptr_t)&dummy_partition_1;
	test_data->ranges[1].type = MEMDB_TYPE_TRACE;

	test_data->ranges[2].base = 0x3D8100000;
	test_data->ranges[2].size = 0x38EC0000;
	test_data->ranges[2].obj  = (uintptr_t)&dummy_partition_1;
	test_data->ranges[2].type = MEMDB_TYPE_TRACE;

	test_data->ranges_count = 3;

	paddr_t start_addr = 0x3D5000000;
	paddr_t end_addr   = 0x57FFFFFFF;

	memdb_test_update(test_data, start_addr, end_addr, MEMDB_TYPE_PARTITION,
			  (uintptr_t)&dummy_partition_1);

	// Insert a range in same address to see if the common level is locked
	paddr_t start_addr2 = 0x580000000;
	paddr_t end_addr2   = 0x69FFFFFFF;

	error_t err = memdb_insert(hyp_partition, start_addr2, end_addr2,
				   (uintptr_t)&dummy_partition_1,
				   MEMDB_TYPE_PARTITION);
	assert(err == OK);

	rcu_read_start();
	memdb_obj_type_result_t res = memdb_lookup(start_addr);
	assert(res.e == OK);
	rcu_read_finish();

	bool cont = memdb_is_ownership_contiguous(start_addr, end_addr2,
						  (uintptr_t)&dummy_partition_1,
						  MEMDB_TYPE_PARTITION);
	assert(cont);

	paddr_t start_addr3 = 0x380000000;
	paddr_t end_addr3   = 0x3D4FFFFFF;

	err = memdb_insert(hyp_partition, start_addr3, end_addr3,
			   (uintptr_t)&dummy_partition_1, MEMDB_TYPE_PARTITION);
	assert(err == OK);

	rcu_read_start();
	res = memdb_lookup(start_addr3);
	assert(res.e == OK);
	rcu_read_finish();

	cont = memdb_is_ownership_contiguous(start_addr3, end_addr2,
					     (uintptr_t)&dummy_partition_1,
					     MEMDB_TYPE_PARTITION);
	assert(cont);
}

static void
memdb_test6(void)
{
	LOG(DEBUG, INFO, " Start TEST 6:");

	partition_t	       *hyp_partition = partition_get_private();
	paddr_t			start_addr    = 0U;
	paddr_t			end_addr      = 0U;
	uintptr_t		obj;
	memdb_type_t		type;
	uintptr_t		prev_obj;
	memdb_type_t		prev_type;
	error_t			err = OK;
	memdb_obj_type_result_t res;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate memdb_data failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	uintptr_t fake_extent = 0xffffff88e1e1e1e1U;

	start_addr = 0x2000000000000;
	end_addr   = 0x2ffffffffffff;

	bool is_range_used =
		is_range_in_memdb(memdb_data, start_addr, end_addr);
	assert(!is_range_used);

	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	start_addr = 0x2000000000000;
	end_addr   = 0x201ffffffffff;
	obj	   = fake_extent;
	type	   = MEMDB_TYPE_EXTENT;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	start_addr = 0x2080000000000;
	end_addr   = 0x2080fffffffff;
	obj	   = (uintptr_t)&dummy_partition_2;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	start_addr = 0x2090000000000;
	end_addr   = 0x213ffffffffff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump initial state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x2100020000000;
	end_addr   = 0x2100020ffffff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_TRACE;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x2100020000000;
	end_addr   = 0x2100020ffffff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_TRACE;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test7(void)
{
	LOG(DEBUG, INFO, " Start TEST 7:");

	partition_t	       *hyp_partition = partition_get_private();
	paddr_t			start_addr    = 0U;
	paddr_t			end_addr      = 0U;
	uintptr_t		obj;
	memdb_type_t		type;
	uintptr_t		prev_obj;
	memdb_type_t		prev_type;
	error_t			err = OK;
	memdb_obj_type_result_t res;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate memdb_data failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	uintptr_t fake_extent = 0xffffff88e1e1e1e1U;

	start_addr = 0x3000000000000;
	end_addr   = 0x3ffffffffffff;

	bool is_range_used =
		is_range_in_memdb(memdb_data, start_addr, end_addr);
	assert(!is_range_used);

	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	start_addr = 0x3000000000000;
	end_addr   = 0x300001fffffff;
	obj	   = fake_extent;
	type	   = MEMDB_TYPE_EXTENT;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	start_addr = 0x3000080000000;
	end_addr   = 0x3000080ffffff;
	obj	   = (uintptr_t)&dummy_partition_2;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	start_addr = 0x3000090000000;
	end_addr   = 0x300123fffffff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump initial state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x3000100020000;
	end_addr   = 0x3000100020fff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_TRACE;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x3000100020000;
	end_addr   = 0x3000100020fff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_TRACE;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test8(void)
{
	LOG(DEBUG, INFO, " Start TEST 8:");

	partition_t	       *hyp_partition = partition_get_private();
	paddr_t			start_addr    = 0U;
	paddr_t			end_addr      = 0U;
	uintptr_t		obj;
	memdb_type_t		type;
	uintptr_t		prev_obj;
	memdb_type_t		prev_type;
	error_t			err = OK;
	memdb_obj_type_result_t res;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate memdb_data failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	uintptr_t fake_extent = 0xffffff88e1e1e1e1U;

	start_addr = 0x4000000000000;
	end_addr   = 0x4ffffffffffff;

	bool is_range_used =
		is_range_in_memdb(memdb_data, start_addr, end_addr);
	assert(!is_range_used);

	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	start_addr = 0x4000000000000;
	end_addr   = 0x400001fffffff;
	obj	   = fake_extent;
	type	   = MEMDB_TYPE_EXTENT;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	start_addr = 0x4000080000000;
	end_addr   = 0x4000080ffffff;
	obj	   = (uintptr_t)&dummy_partition_2;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a0000c000;
	end_addr   = 0x4000a0000cfff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a0000d000;
	end_addr   = 0x4000cffffffff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a00000000;
	end_addr   = 0x4000a0000bfff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a00012000;
	end_addr   = 0x4000a00012fff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_TRACE;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a00013000;
	end_addr   = 0x4000a0e012fff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_TRACE;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_PARTITION;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);

	start_addr = 0x4000a00012000;
	end_addr   = 0x4000a0e012fff;
	obj	   = (uintptr_t)&dummy_partition_1;
	type	   = MEMDB_TYPE_PARTITION;
	prev_obj   = (uintptr_t)&dummy_partition_1;
	prev_type  = MEMDB_TYPE_TRACE;

	err = memdb_update(hyp_partition, start_addr, end_addr, obj, type,
			   prev_obj, prev_type);
	assert(err == OK);

	// Dump state
	check_ranges_in_memdb(memdb_data);
	memset(memdb_data, 0, sizeof(memdb_data_t));
	LOG(DEBUG, INFO, "----------------- RANGES IN MEMDB -----------------");
	LOG(DEBUG, INFO, "-- FAKE EXTENT --");
	get_inserted_ranges(memdb_data, fake_extent, MEMDB_TYPE_EXTENT);
	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test9(void)
{
	LOG(DEBUG, INFO, " Start TEST 9:");

	partition_t	       *hyp_partition = partition_get_private();
	paddr_t			start_addr    = 0U;
	paddr_t			end_addr      = 0U;
	uintptr_t		obj;
	memdb_type_t		type;
	error_t			err = OK;
	memdb_obj_type_result_t res;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate memdb_data failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	// Check initial present ranges
	check_ranges_in_memdb(memdb_data);

	// Allocate dummy region we can use to replace the allocator's freelist
#if defined(MODULE_MEM_MEMDB_GPT)
	size_t dummy_size = sizeof(memdb_level_t) * 4;
#elif defined(MODULE_MEM_MEMDB_BITMAP)
	size_t dummy_size = sizeof(memdb_level_table_t) * 4;
#else
#error Determine free heap size to cause OOM during the below memdb_insert
#endif
	allocator_node_t *dummy_heap = NULL;
	alloc_ret = partition_alloc(hyp_partition, dummy_size,
				    alignof(allocator_node_t));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate dummy region failed");
	}
	dummy_heap = alloc_ret.r;

	// Cause an out of memory error to see if the rollback is done correctly
	start_addr = 0x61234567890000;
	end_addr   = 0x62234567890fff;

	bool is_range_used =
		is_range_in_memdb(memdb_data, start_addr, end_addr);
	assert(!is_range_used);

	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	obj  = (uintptr_t)&dummy_partition_1;
	type = MEMDB_TYPE_PARTITION;

	// Make sure any outstanding RCU work has completed, so there won't
	// be any frees into the allocator while we have swapped its heap with
	// the dummy.
	rcu_sync();
	rcu_sync();

	// Swap the real heap with the dummy one
	spinlock_acquire(&hyp_partition->allocator.lock);
	allocator_node_t *saved_heap = hyp_partition->allocator.heap;
	*dummy_heap = (allocator_node_t){ .size = dummy_size, .next = NULL };
	hyp_partition->allocator.heap = dummy_heap;
	spinlock_release(&hyp_partition->allocator.lock);

	// Should run out of memory because this range needs to create several
	// levels (if they have not been created in previous tests) and there is
	// not much memory left in allocator
	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == ERROR_NOMEM);

	// Verify that the address does not exist in the memdb
	rcu_read_start();
	res = memdb_lookup(start_addr);
	assert((res.e != OK) || (res.r.type == MEMDB_TYPE_NOTYPE));
	rcu_read_finish();

	// Verify that all memory allocated during the attempted insert has
	// been freed by the time an RCU grace period has expired.
	rcu_sync();
	rcu_sync();
	spinlock_acquire(&hyp_partition->allocator.lock);
	assert(hyp_partition->allocator.heap == dummy_heap);
	assert(dummy_heap->size == dummy_size);

	// Swap the heap back and retry inserting the same range. This time it
	// should succeed.
	hyp_partition->allocator.heap = saved_heap;
	spinlock_release(&hyp_partition->allocator.lock);
	partition_free(hyp_partition, dummy_heap, dummy_size);

	err = memdb_insert(hyp_partition, start_addr, end_addr, obj, type);
	assert(err == OK);
}

static error_t
verify_range(paddr_t base, size_t size, void *arg)
{
	memdb_data_t *memdb_data = (memdb_data_t *)arg;
	index_t	      i		 = memdb_data->ranges_index;

	assert(i < memdb_data->ranges_count);
	assert(base == memdb_data->ranges[i].base);
	assert(size == memdb_data->ranges[i].size);

	memdb_data->ranges_index++;

	return OK;
}

static void
memdb_test10(void)
{
	LOG(DEBUG, INFO, " Start TEST 10:");

	error_t	     err;
	partition_t *hyp_partition  = partition_get_private();
	partition_t *fake_partition = (partition_t *)0x123124;

	void_ptr_result_t alloc_ret;
	memdb_data_t	 *memdb_data;
	size_t		  memdb_data_size = sizeof(*memdb_data);

	alloc_ret = partition_alloc(hyp_partition, memdb_data_size,
				    alignof(*memdb_data));
	if (alloc_ret.e != OK) {
		panic("memdb_test: allocate memdb_data failed");
	}

	memdb_data = (memdb_data_t *)alloc_ret.r;
	memset(memdb_data, 0, memdb_data_size);

	paddr_t base0 = 0x1082800000U;
	size_t	size0 = 0x55800000U;

	err = memdb_insert(hyp_partition, base0, base0 + size0 - 1U,
			   (uintptr_t)fake_partition, MEMDB_TYPE_PARTITION);
	assert(err == OK);

	paddr_t base1 = 0x10D8200000U;
	size_t	size1 = 0xE0000U;

	err = memdb_insert(hyp_partition, base1, base1 + size1 - 1U,
			   (uintptr_t)fake_partition, MEMDB_TYPE_PARTITION);
	assert(err == OK);

	memdb_data->ranges[0].base = base0;
	memdb_data->ranges[0].size = size0;
	memdb_data->ranges[1].base = base1;
	memdb_data->ranges[1].size = size1;
	memdb_data->ranges_count   = 2U;

	err = memdb_walk((uintptr_t)fake_partition, MEMDB_TYPE_PARTITION,
			 verify_range, memdb_data);
	assert(err == OK);

	assert(memdb_data->ranges_index == memdb_data->ranges_count);

	partition_free(hyp_partition, memdb_data, memdb_data_size);
}

static void
memdb_test11(void)
{
#if defined(MEMDB_BITMAP_OBJECTS)
	LOG(DEBUG, INFO, " Start TEST 11:");

	memdb_data_t test_data = { 0 };

	paddr_t start_addr = 0x5000a00000000;
	paddr_t end_addr   = 0x5000a00ffffff;

	static_assert(MEMDB_BITMAP_OBJECTS <= util_array_size(test_data.ranges),
		      "Test data is too small");
	for (index_t i = 0U; i < MEMDB_BITMAP_OBJECTS; i++) {
		test_data.ranges[i].base = start_addr + (0x2000 * i);
		test_data.ranges[i].size = 0x1000;
		test_data.ranges[i].obj =
			((uintptr_t)&dummy_partition_2) + (MEMDB_MIN_SIZE * i);
		test_data.ranges[i].type = MEMDB_TYPE_TRACE;
		test_data.ranges_count++;
	}

	memdb_test_update(&test_data, start_addr, end_addr,
			  MEMDB_TYPE_PARTITION, (uintptr_t)&dummy_partition_1);
#else // !defined(MEMDB_BITMAP_OBJECTS)
	LOG(DEBUG, INFO, " Skip TEST 11 (not using memdb_bitmap)");
#endif
}

bool
memdb_handle_tests_start(void)
{
	static _Atomic count_t core_start_count;
	static _Atomic bool    tests_done;
	cpu_index_t	       this_cpu = cpulocal_get_index();

	(void)atomic_fetch_add(&core_start_count, 1U);
	while (atomic_load(&core_start_count) < PLATFORM_MAX_CORES) {
		scheduler_yield();
	}

	if (this_cpu != 0U) {
		goto wait;
	}

	LOG(DEBUG, INFO, "Start memdb tests");

	// Test updates. Check that after update operations the levels collapse
	// properly when needed. If they don't then
	// memdb_is_ownership_contiguous will give the wrong result.
	memdb_test0();

	memdb_test1();

	// Test of adding/modifying guards.
	// Insert range that adds a guard and update the range ownership so that
	// we can verify that the range has been added correctly.
	memdb_test2();
	memdb_test3();
	// This test needs to create a new level (and update guard) due to a
	// mismatch in the guard when finding the common level. It should then
	// create a new level in the start path due to again a mismatch in
	// guard. This should all be successful if there is there are no bugs
	// left in the creation of intermediate level due to guards mismatch
	memdb_test4();
	memdb_test5();

	// Split/merge tests
	memdb_test6();
	memdb_test7();
	memdb_test8();

	// Test handling of out of memory error
	memdb_test9();

	// Test walk over two ranges with empty space from guard.
	memdb_test10();

	// Test conversion of bitmap levels to table levels for memdb_bitmap
	memdb_test11();

	LOG(DEBUG, INFO, "Memdb tests successfully finished ");
	atomic_store(&tests_done, true);

wait:
	(void)0;

	// Make all threads wait for test to end
	while (!atomic_load(&tests_done)) {
		scheduler_yield();
	}

	return false;
}
#else

extern char unused;

#endif
