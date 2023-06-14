
#include <hyptypes.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <allocator.h>
#include <memdb.h>

#define MEMDB_BITS_PER_ENTRY_MASK ((1UL << MEMDB_BITS_PER_ENTRY) - 1)
#define ADDR_SIZE		  (sizeof(paddr_t) * CHAR_BIT)
// levels + 1 for root
#define MAX_LEVELS (ADDR_SIZE / MEMDB_BITS_PER_ENTRY) + 1

partition_t partition;

void
print_level(memdb_level_t *level)
{
	if (level == NULL) {
		printf("Empty database.\n");
	}

	printf("Level lock: %p\n", &level->lock);

	for (int i = 0; i < MEMDB_NUM_ENTRIES; i++) {
		memdb_entry_t tmp_entry = atomic_load_explicit(
			&level->level[i], memory_order_relaxed);
		memdb_type_t tmp_type =
			memdb_entry_info_get_type(&tmp_entry.info);
		count_t tmp_shifts =
			memdb_entry_info_get_shifts(&tmp_entry.info);
		uint64_t tmp_guard =
			memdb_entry_info_get_guard(&tmp_entry.info);

		if (tmp_type != MEMDB_TYPE_NOTYPE) {
			// printf("| %d ", tmp_type);
			uint32_t tt = tmp_entry.info.bf[0] & 0x7;
			printf("| %d ", tt);
			if (tmp_shifts != ADDR_SIZE) {
				printf("guard_shifts: %d ", tmp_shifts);
				printf("guard: %#lx ", tmp_guard);
			}
		} else {
			printf("| - ");
		}
	}
	printf("|\n");

	for (int i = 0; i < MEMDB_NUM_ENTRIES; i++) {
		memdb_entry_t tmp_entry = atomic_load_explicit(
			&level->level[i], memory_order_relaxed);
		memdb_type_t tmp_type =
			memdb_entry_info_get_type(&tmp_entry.info);
		void *next = &tmp_entry.next;

		if (tmp_type != MEMDB_TYPE_NOTYPE) {
			if (tmp_type == MEMDB_TYPE_LEVEL) {
				printf("----- Level below index: %d -----\n",
				       i);
				print_level(next);
				printf("---------------------------------\n");
			}
		}
	}
}

void
print_memdb(void)
{
#if 0
	// To print the database we need to declare memdb_t memdb in memdb.h not
	// in mem_ownership_db.c
	memdb_entry_t tmp_root =
		atomic_load_explicit(&memdb.root, memory_order_relaxed);
	uintptr_t *  next	= &tmp_root.next;
	memdb_type_t tmp_type	= memdb_entry_info_get_type(&tmp_root.info);
	count_t     tmp_shifts = memdb_entry_info_get_shifts(&tmp_root.info);
	paddr_t	     tmp_guard	= memdb_entry_info_get_guard(&tmp_root.info);

	if (next == NULL) {
		printf("Empty memory database\n");
		return;
	} else {
		printf("------- Memory Ownership Database -------\n");
		printf("root guard: %#lx\n", tmp_guard);
		printf("root guard shifts: %d\n", tmp_shifts);
		printf("root type: %d\n", tmp_type);
		printf("root lock pointer: %p\n", &memdb.lock);
	}

	memdb_level_t *level = (memdb_level_t *)next;

	printf("\n");

	if (level == NULL) {
		printf("Empty database");
	} else {
		print_level(level);
	}

	printf("\n-----------------------------------------\n\n");
#endif
}

void
print_memdb_empty(void)
{
	//	print_memdb();
}

// Insert two ranges in database
int
test1()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
	}

	size_t	     obj_size = 1024;
	memdb_type_t type     = MEMDB_TYPE_PARTITION;

	size_t	     obj_size2 = 4096;
	memdb_type_t type2     = MEMDB_TYPE_ALLOCATOR;

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size,
					alignment);
	uintptr_t object = (uintptr_t)res.r;

	if (res.e != OK) {
		printf("Object allocation failed\n");
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr = (uint64_t)object;
	uint64_t end_addr   = (uint64_t)object + (obj_size - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);
	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Insert object in database --------------------- */
	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)object,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n\n", start_addr2,
	       end_addr2);

	ret = memdb_insert(&partition, start_addr2, end_addr2,
			   (uintptr_t)object2, type2);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// Insert one range in database and do two updates.
int
test2()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size = 4096 * 100;
	size_t obj_size1 = 4096;
	size_t obj_size2 = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);
	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);
	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// One insertion, two updates and update back to the initial state
int
test3()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size = 4096 * 100;
	size_t obj_size1 = 4096;
	size_t obj_size2 = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);
	uintptr_t object1 = (uintptr_t)res.r;

	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);
	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block, type,
			   object1, type1);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);

	ret = memdb_update(&partition, start_addr2, end_addr2, block, type,
			   object2, type2);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// Two insertion, 2 updates, 2 updates back to state after insertions
int
test4()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);
	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		printf("\nmemdb_insert SUCCESS\n\n");
		print_memdb_empty();
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = (uint64_t)block2;
	uint64_t end_addrh   = (uint64_t)block2 + (pool_size2 - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addrh, end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		printf("\nmemdb_insert SUCCESS\n\n");
		print_memdb_empty();
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type1, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);

	if (!ret) {
		printf("\nmemdb_update SUCCESS\n\n");
		print_memdb_empty();
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */
	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type, type1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block, type,
			   object1, type1);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type, type2);

	ret = memdb_update(&partition, start_addr2, end_addr2, block, type,
			   object2, type2);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 1 insertion, 2 updates, 2 checks of contiguouness (1 should succeed and the
// other one fail)
int
test5()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size = 4096 * 100;
	size_t obj_size1 = 4096;
	size_t obj_size2 = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ------------------- Is contiguous ? -------------------- */
	// Should succeed:
	printf("\nIs start_addr1: %#lx, end_addr1: %#lx contiguous??\n",
	       start_addr1, end_addr1);

	bool ans = memdb_is_ownership_contiguous(start_addr1, end_addr1,
						 object1, type1);
	if (ans) {
		printf("\nmemdb_is_ownership_contiguous SUCCESS\n\n");
	} else {
		printf("\nmemdb_is_ownership_contiguous FAILED\n\n");
		goto error;
	}

	/* ------------------- Is contiguous ? -------------------- */
	// Should fail:
	printf("\nIs start_addr1: %#lx, end_addr1: %#lx contiguous??\n",
	       start_addr1 - 1, end_addr1);

	ans = memdb_is_ownership_contiguous(start_addr1 - 1, end_addr1, object1,
					    type1);
	if (ans) {
		printf("\nmemdb_is_ownership_contiguous SUCCESS\n\n");
		ret = -1;
		goto error;
	} else {
		printf("\nmemdb_is_ownership_contiguous FAILED as expected.\n\n");
		ret = 0;
	}
error:
	return ret;
}

// 1 insertion, 2 updates and 2 lookups
int
test6()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 16;

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size = 4096 * 100;
	size_t obj_size1 = 4096;
	size_t obj_size2 = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 insertions, 2 updates, 2 lookups
int
test7()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)malloc(pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = (uint64_t)block2;
	uint64_t end_addrh   = (uint64_t)block2 + (pool_size2 - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addrh, end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type1, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addrh: %#lx\n", start_addr1);
	tot_res = memdb_lookup(start_addrh);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 insertions, 2 updates, 2 updates back
// (Address ranges with 64 guard)
int
test8()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0;
	uint64_t end_addr   = 0;
	end_addr	    = (~(end_addr & 0) - 4096);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0;
	uint64_t end_addrh   = 0;

	start_addrh = (~(start_addrh & 0) - 4096) + 1;
	end_addrh   = (~(end_addrh & 0));

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addrh, end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */
	start_addr1 = start_addr + 6;
	end_addr1   = end_addr - (4096000000000000000);

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type1, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED. ret = %d\n\n", ret);
		goto error;
	}

	/* ---------------- Update database --------------------- */
	start_addr2 = end_addr1 + 1;
	end_addr2   = end_addr;

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type, type1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block, type,
			   object1, type1);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}

	/* ---------------- Update back database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type, type2);

	ret = memdb_update(&partition, start_addr2, end_addr2, block, type,
			   object2, type2);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update back SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update back FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// Rollback. (2 insertion, 2 updates (last one rolled back))
int
test9()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 139944292126720;
	uint64_t end_addr   = 139944292536319;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = (uint64_t)block2;
	uint64_t end_addrh   = (uint64_t)block2 + (pool_size2 - 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addrh, end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED: %d\n\n", ret);
		goto error;
	}

	/* ---------------- Update database --------------------- */
	start_addr1 = start_addr + (4096 * 4);
	end_addr1   = end_addr;

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", type1, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */
	start_addr2 = start_addr;
	end_addr2   = start_addr1;

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n", start_addr2,
	       end_addr2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS, should not have succeeded!!!\n\n");
		ret = -1;
		goto error;
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED as expected (rollback).\n\n");
		ret = 0;
	}

error:
	return ret;
}

// Insert a second range that has to check a guard in the end
// path. Guard matches
int
test10()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type = MEMDB_TYPE_PARTITION;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0xfffffffffffff000;
	uint64_t end_addrh   = 0xffffffffffffffff;

	printf("\nstart_addrh: %#lx, end_addrh: %#lx\n", start_addrh,
	       end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0x0;
	uint64_t end_addr   = 0xffffffffffffefff;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	uint64_t addr = 0xfffffffffffeeffe;

	printf("\nLooking for addr: %#lx\n", addr);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(addr);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

error:
	return ret;
}

// Insert a range with a root guard and then insert another range that remove
// root guard
int
test11()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);
	size_t	    pool_size = 4096 * 100;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
	}

	size_t	     obj_size = 1024;
	memdb_type_t type     = MEMDB_TYPE_PARTITION;

	size_t	     obj_size2 = 4096;
	memdb_type_t type2     = MEMDB_TYPE_ALLOCATOR;

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size,
					alignment);

	uintptr_t object = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr = (uint64_t)object;
	uint64_t end_addr   = (uint64_t)object + (obj_size - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Insert object in database --------------------- */
	start_addr = 0;
	end_addr   = 15;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)object,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	start_addr2 = ~(start_addr2 & 0) - 15;
	end_addr2   = ~(end_addr2 & 0);

	printf("\nstart_addr2: %#lx, end_addr2: %#lx\n\n", start_addr2,
	       end_addr2);

	ret = memdb_insert(&partition, start_addr2, end_addr2,
			   (uintptr_t)object2, type2);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 non contiguous insertions and 1 update that should fail due to
// contiguousness
int
test12()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type = MEMDB_TYPE_PARTITION;

	size_t pool_size = 4096 * 100;
	size_t obj_size1 = 4096;
	size_t obj_size2 = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	// uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0x4000;
	uint64_t end_addr   = 0x7fff;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		printf("\nmemdb_insert SUCCESS\n\n");
		print_memdb_empty();
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0x1380;
	uint64_t end_addrh   = 0x13ff;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addrh, end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh, (uintptr_t)block,
			   type);

	if (!ret) {
		printf("\nmemdb_insert SUCCESS\n\n");
		print_memdb_empty();
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */
	start_addr1 = 0x1380;
	end_addr1   = 0x7fff;

	printf("\nstart_addr1: %#lx, end_addr1: %#lx\n", start_addr1,
	       end_addr1);
	printf("\nnew type: %d old type: %d\n", 4, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, object1, 4,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS. should have failed!!\n\n");
		ret = -1;
		goto error;
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED as expected\n\n");
		ret = 0;
	}

error:
	return ret;
}

// Insert a second range that has to check a guard in the end
// path. Guard partially matches
int
test13()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = 4096;

	memdb_type_t type = MEMDB_TYPE_PARTITION;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)aligned_alloc(alignment, pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)aligned_alloc(alignment, pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0xfffffffffffff000;
	uint64_t end_addrh   = 0xffffffffffffffff;

	printf("\nstart_addrh: %#lx, end_addrh: %#lx\n", start_addrh,
	       end_addrh);
	printf("\nnew type: %d\n", MEMDB_TYPE_ALLOCATOR);

	ret = memdb_insert(&partition, start_addrh, end_addrh,
			   (uintptr_t)block2, MEMDB_TYPE_ALLOCATOR);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0x0;
	uint64_t end_addr   = 0xfffffffffffeeffe;

	printf("\nstart_addr: %#lx, end_addr: %#lx\n", start_addr, end_addr);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, (uintptr_t)block,
			   type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	uint64_t addr = 0xfffffffffffeeffe;

	printf("\nLooking for addr: %#lx\n", addr);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(addr);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

error:
	return ret;
}

paddr_t returned_base;
size_t	returned_size;

error_t
add_free_range(paddr_t base, size_t size, void *arg)
{
	(void)arg;
	// uint64_t end_addr = base + (size - 1);
	// printf("add_free_range: base: %#lx - size: %lu - end_addr: %#lx\n",
	// base, size, end_addr);
	printf("add_free_range: base: %#lx - size: %#lx\n", base, size);

	returned_base = base;
	returned_size = size;

	return OK;
}

// 2 insertions, 2 updates, 2 lookups, 2 memwalk with guards
int
test14()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)malloc(pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);
	uint64_t range_size = (end_addr - start_addr + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addr,
	       end_addr, range_size);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = (uint64_t)block2;
	uint64_t end_addrh   = (uint64_t)block2 + (pool_size2 - 1);
	uint64_t range_size2 = (end_addrh - start_addrh + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addrh,
	       end_addrh, (end_addrh - start_addrh + 1));
	printf("\nnew type: %d\n", type1);

	ret = memdb_insert(&partition, start_addrh, end_addrh, block2, type1);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB WALK --------------------- */
	printf("Mem walk to match type: %d\n", type);
	void   *arg  = NULL;
	error_t resl = memdb_walk(block, type, add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addr) &&
	    (returned_size == range_size)) {
		printf("memdb walk SUCCESS\n\n");
	} else {
		printf("memdb walk FAILED\n");
		ret = resl;
		if (!(returned_base == start_addr)) {
			printf("returned_base: %#lx - start_addr: %#lx\n",
			       returned_base, start_addr);
			ret = -1;
		}
		if (!(returned_size == range_size)) {
			printf("returned_size: %#lx - size: %lx\n\n",
			       returned_size, range_size);
			ret = -1;
		}
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx, size: %#lx\n",
	       start_addr1, end_addr1, obj_size1);
	printf("\nnew type: %d old type: %d\n", type1, type);
	uint64_t range_size3 = (end_addr1 - start_addr1 + 1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block2, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx, size: %#lx\n",
	       start_addr2, end_addr2, obj_size2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB WALK --------------------- */
	printf("Mem walk to match type: %d\n", type1);
	resl = memdb_walk(block2, type1, add_free_range, arg);

	if ((resl == OK) &&
	    ((returned_base == start_addrh) ||
	     (returned_base == start_addr1)) &&
	    ((returned_size == range_size2) ||
	     (returned_size == range_size3))) {
		printf("memdb walk SUCCESS\n\n");
	} else {
		printf("memdb walk FAILED\n\n");
		ret = resl;
		if (!(returned_base == start_addrh)) {
			printf("returned_base: %#lx - start_addr: %#lx\n",
			       returned_base, start_addrh);
			ret = -1;
		}
		if (!(returned_size == range_size2)) {
			printf("returned_size: %#lx - size: %#lx\n",
			       returned_size, range_size2);
			ret = -1;
		}
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addrh: %#lx\n", start_addr1);
	tot_res = memdb_lookup(start_addrh);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 insertions, 2 updates, 2 lookups, 2 memwalk without guard
int
test15()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)malloc(pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0x4;
	uint64_t end_addr   = 0xfffffffffffeeffe;
	uint64_t range_size = (end_addr - start_addr + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addr,
	       end_addr, (end_addr - start_addr + 1));
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0xffffffffffffff00;
	uint64_t end_addrh   = 0xffffffffffffffff;
	uint64_t range_size2 = (end_addrh - start_addrh + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addrh,
	       end_addrh, (end_addrh - start_addrh + 1));
	printf("\nnew type: %d\n", type1);

	ret = memdb_insert(&partition, start_addrh, end_addrh, block2, type1);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB WALK --------------------- */
	printf("Mem walk to match type: %d\n", type);
	void   *arg  = NULL;
	error_t resl = memdb_walk(block, type, add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addr) &&
	    (returned_size == range_size)) {
		printf("memdb walk SUCCESS\n\n");
	} else {
		printf("memdb walk FAILED\n\n");
		if (!(returned_base == start_addr)) {
			printf("returned_base: %#lx start_addr: %#lx\n",
			       returned_base, start_addr);
		}
		if (!(returned_size == range_size)) {
			printf("returned_size: %#lx, range_size: %#lx\n",
			       returned_size, range_size);
		}
		ret = -1;
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx, size: %#lx\n",
	       start_addr1, end_addr1, obj_size1);
	printf("\nnew type: %d old type: %d\n", type1, type);

	ret = memdb_update(&partition, start_addr1, end_addr1, block2, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx, size: %#lx\n",
	       start_addr2, end_addr2, obj_size2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB WALK --------------------- */
	printf("Mem walk to match type: %d\n", type1);
	resl = memdb_walk(block2, type1, add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addrh) &&
	    (returned_size == range_size2)) {
		printf("memdb walk SUCCESS\n\n");
	} else {
		printf("memdb walk FAILED\n\n");
		if (!(returned_base == start_addrh)) {
			printf("returned_base: %#lx start_addr: %#lx\n",
			       returned_base, start_addrh);
		}
		if (!(returned_size == range_size2)) {
			printf("returned_size: %#lx, range_size: %#lx\n",
			       returned_size, range_size2);
		}
		ret = -1;

		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addrh: %#lx\n", start_addr1);
	tot_res = memdb_lookup(start_addrh);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 insertions, 2 updates, 2 lookups, 2 memwalk RANGE with guards
int
test16()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)malloc(pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = (uint64_t)block;
	uint64_t end_addr   = (uint64_t)block + (pool_size - 1);
	uint64_t range_size = (end_addr - start_addr + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addr,
	       end_addr, range_size);
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = (uint64_t)block2;
	uint64_t end_addrh   = (uint64_t)block2 + (pool_size2 - 1);
	uint64_t range_size2 = (end_addrh - start_addrh + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addrh,
	       end_addrh, (end_addrh - start_addrh + 1));
	printf("\nnew type: %d\n", type1);

	ret = memdb_insert(&partition, start_addrh, end_addrh, block2, type1);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem range walk to match type: %d\n", type);
	void   *arg  = NULL;
	error_t resl = memdb_range_walk(block, type, start_addr, end_addr,
					add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addr) &&
	    (returned_size == range_size)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n");
		ret = resl;
		if (!(returned_base == start_addr)) {
			printf("returned_base: %#lx - start_addr: %#lx\n",
			       returned_base, start_addr);
			ret = -1;
		}
		if (!(returned_size == range_size)) {
			printf("returned_size: %#lx - size: %lx\n\n",
			       returned_size, range_size);
			ret = -1;
		}
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx, size: %#lx\n",
	       start_addr1, end_addr1, obj_size1);
	printf("\nnew type: %d old type: %d\n", type1, type);
	uint64_t range_size3 = (end_addr1 - start_addr1 + 1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block2, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx, size: %#lx\n",
	       start_addr2, end_addr2, obj_size2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem range walk to match type: %d\n", type1);
	resl = memdb_range_walk(block2, type1, start_addrh, end_addrh,
				add_free_range, arg);

	if ((resl == OK) && (returned_base == start_addrh) &&
	    (returned_size == range_size2)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n\n");
		ret = resl;
		if (!(returned_base == start_addrh)) {
			printf("returned_base: %#lx - start_addr: %#lx\n",
			       returned_base, start_addrh);
			ret = -1;
		}
		if (!(returned_size == range_size2)) {
			printf("returned_size: %#lx - size: %#lx\n",
			       returned_size, range_size2);
			ret = -1;
		}
		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem range walk to match type: %d\n", type1);
	resl = memdb_range_walk(block2, type1, start_addr1, end_addr1,
				add_free_range, arg);

	if ((resl == OK) && (returned_base == start_addr1) &&
	    (returned_size == range_size3)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n\n");
		ret = resl;
		if (!(returned_base == start_addr1)) {
			printf("returned_base: %#lx - start_addr: %#lx\n",
			       returned_base, start_addr1);
			ret = -1;
		}
		if (!(returned_size == range_size3)) {
			printf("returned_size: %#lx - size: %#lx\n",
			       returned_size, range_size3);
			ret = -1;
		}
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addrh: %#lx\n", start_addr1);
	tot_res = memdb_lookup(start_addrh);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

// 2 insertions, 2 updates, 2 lookups, 2 memwalk RANGE without guard
int
test17()
{
	partition_t partition;
	int	    ret	      = 0;
	size_t	    alignment = sizeof(void *);

	memdb_type_t type  = MEMDB_TYPE_PARTITION;
	memdb_type_t type1 = MEMDB_TYPE_ALLOCATOR;
	memdb_type_t type2 = MEMDB_TYPE_EXTENT;

	size_t pool_size  = 4096 * 100;
	size_t pool_size2 = 1024;
	size_t obj_size1  = 4096;
	size_t obj_size2  = 1024;

	partition.allocator.heap = NULL;

	/* ---------------- Giving memory to heap --------------------- */
	uintptr_t block = (uintptr_t)malloc(pool_size);

	ret = allocator_heap_add_memory(&partition.allocator, (void *)block,
					pool_size);
	if (!ret) {
		printf("Memory added to heap\n");
	}

	/* ----------------- Hypervisor memory ------------------------ */
	uintptr_t block2 = (uintptr_t)malloc(pool_size2);

	/* ---------- Allocate object. partition for example ---------------- */
	void_ptr_result_t res;
	res = allocator_allocate_object(&partition.allocator, obj_size1,
					alignment);

	uintptr_t object1 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("Object allocation failed\n");
		goto error;
	} else {
		printf("Object allocation SUCCESS\n");
	}

	uint64_t start_addr1 = (uint64_t)object1;
	uint64_t end_addr1   = (uint64_t)object1 + (obj_size1 - 1);

	/* ---------- Allocate object. hypervisor for example ----------------
	 */
	res = allocator_allocate_object(&partition.allocator, obj_size2,
					alignment);

	uintptr_t object2 = (uintptr_t)res.r;
	if (res.e != OK) {
		printf("\nObject allocation failed\n");
		goto error;
	} else {
		printf("\nObject allocation SUCCESS\n");
	}

	uint64_t start_addr2 = (uint64_t)object2;
	uint64_t end_addr2   = (uint64_t)object2 + (obj_size2 - 1);

	/* ---------------- Init memory database --------------------- */
	ret = memdb_init();

	if (!ret) {
		printf("Mem db init correct!\n");
	} else {
		printf("Error init!\n");
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addr = 0x4;
	uint64_t end_addr   = 0xfffffffffffeeffe;
	uint64_t range_size = (end_addr - start_addr + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addr,
	       end_addr, (end_addr - start_addr + 1));
	printf("\nnew type: %d\n", type);

	ret = memdb_insert(&partition, start_addr, end_addr, block, type);
	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		printf("\nmemdb_insert FAILED\n\n");
		print_memdb_empty();
		goto error;
	}

	/* ---------------- Insert object in database --------------------- */
	uint64_t start_addrh = 0xffffffffffffff00;
	uint64_t end_addrh   = 0xffffffffffffffff;
	uint64_t range_size2 = (end_addrh - start_addrh + 1);

	printf("\nstart_addr: %#lx, end_addr: %#lx, size: %#lx\n", start_addrh,
	       end_addrh, (end_addrh - start_addrh + 1));
	printf("\nnew type: %d\n", type1);

	ret = memdb_insert(&partition, start_addrh, end_addrh, block2, type1);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_insert SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_insert FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem range walk to match type: %d\n", type);
	void   *arg  = NULL;
	error_t resl = memdb_range_walk(block, type, start_addr, end_addr,
					add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addr) &&
	    (returned_size == range_size)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n\n");
		if (!(returned_base == start_addr)) {
			printf("returned_base: %#lx start_addr: %#lx\n",
			       returned_base, start_addr);
		}
		if (!(returned_size == range_size)) {
			printf("returned_size: %#lx, range_size: %#lx\n",
			       returned_size, range_size);
		}
		ret = -1;
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr1: %#lx, end_addr1: %#lx, size: %#lx\n",
	       start_addr1, end_addr1, obj_size1);
	printf("\nnew type: %d old type: %d\n", type1, type);
	uint64_t range_size3 = (end_addr1 - start_addr1 + 1);

	ret = memdb_update(&partition, start_addr1, end_addr1, block2, type1,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- Update database --------------------- */

	printf("\nstart_addr2: %#lx, end_addr2: %#lx, size: %#lx\n",
	       start_addr2, end_addr2, obj_size2);
	printf("\nnew type: %d old type: %d\n", type2, type);

	ret = memdb_update(&partition, start_addr2, end_addr2, object2, type2,
			   block, type);

	if (!ret) {
		print_memdb_empty();
		printf("\nmemdb_update SUCCESS\n\n");
	} else {
		print_memdb_empty();
		printf("\nmemdb_update FAILED\n\n");
		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem walk to match type: %d\n", type1);
	resl = memdb_range_walk(block2, type1, start_addrh, end_addrh,
				add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addrh) &&
	    (returned_size == range_size2)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n\n");
		if (!(returned_base == start_addrh)) {
			printf("returned_base: %#lx start_addr: %#lx\n",
			       returned_base, start_addrh);
		}
		if (!(returned_size == range_size2)) {
			printf("returned_size: %#lx, range_size: %#lx\n",
			       returned_size, range_size2);
		}
		ret = -1;

		goto error;
	}

	/* ---------------- MEMDB RANGE WALK --------------------- */
	printf("Mem walk to match type: %d\n", type1);
	resl = memdb_range_walk(block2, type1, start_addr1, end_addr1,
				add_free_range, arg);
	if ((resl == OK) && (returned_base == start_addr1) &&
	    (returned_size == range_size3)) {
		printf("memdb range walk SUCCESS\n\n");
	} else {
		printf("memdb range walk FAILED\n\n");
		if (!(returned_base == start_addr1)) {
			printf("returned_base: %#lx start_addr: %#lx\n",
			       returned_base, start_addr1);
		}
		if (!(returned_size == range_size3)) {
			printf("returned_size: %#lx, range_size: %#lx\n",
			       returned_size, range_size3);
		}
		ret = -1;

		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addr1: %#lx\n", start_addr1);
	memdb_obj_type_result_t tot_res;

	tot_res = memdb_lookup(start_addr1);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}

	/* ----------------- lookup --------------------- */
	printf("\nLooking for start_addrh: %#lx\n", start_addr1);
	tot_res = memdb_lookup(start_addrh);
	ret	= tot_res.e;
	if (tot_res.e == OK) {
		printf("\nmemdb_lookup SUCCESS. type: %d\n\n", tot_res.r.type);
	} else {
		printf("\nmemdb_lookup FAILED\n\n");
		goto error;
	}
error:
	return ret;
}

#define NUM_TESTS 17

int (*func_ptr[NUM_TESTS])(void) = {
	test1,	// Insert two ranges in db
	test2,	// One insertion and two updates
	test3,	// One insertion, two updates, and two updates back to state
		// after insertion
	test4,	// 2 insertions, 2 updates, 2 updates back to state after
		// insertions
	test5,	// 1 insertion, 2 updates, 2 check of contiguousness (1 must
		// succeed and the other one fail)
	test6,	// 1 insertion, 2 updates, 2 lookups
	test7,	// 2 insertions, 2 updates, 2 lookups
	test8,	// Address ranges with 64 guard (2 insertions, 2 updates, 2
		// updates back)
	test9,	// Rollback. (2 insertion, 2 updates (last one rolled back))
	test10, // Insert a second range that has to check a guard in the end
		// path. Guard matches
	test11, // Insert a range with a root guard and then insert another
		// range that remove root guard
	test12, // 2 non contiguous insertions and 1 update that should fail due
		// to contiguousness
	test13, // Insert a second range that has to check a guard in the end
		// path. Guard partially matches
	test14, // 2 insertions, 2 updates, 2 lookups, 2 mem walk with GUARDS
	test15, // 2 insertions, 2 updates, 2 lookups, 2 mem walk without guards
	test16, // 2 insertions, 2 updates, 2 lookups, 2 mem RANGE walk with
		// GUARDS
	test17, // 2 insertions, 2 updates, 2 lookups, 2 mem RANGE walk without
		// guards
};

int
main()
{
	int test = 0;
	int ret	 = 0;

	switch (test) {
	case 0:
		for (int i = 0; i < NUM_TESTS; i++) {
			printf("\n\n_____________________________________________________ TEST %d ________________________________________________\n\n",
			       i + 1);
			ret = func_ptr[i]();
			if (ret != 0) {
				printf("FAILED test: %d\n", i + 1);
				break;
			}
		}
		if (ret == 0) {
			printf("All %d tests passed!\n", NUM_TESTS);
		}
		break;

	case 1:
		test1();
		break;
	case 2:
		test2();
		break;
	case 3:
		test3();
		break;
	case 4:
		test4();
		break;
	case 5:
		// is memory contiguous test
		test5();
		break;
	case 6:
		// lookup test
		test6();
		break;
	case 7:
		// lookup test
		test7();
		break;
	case 8:
		// Address ranges with 64 guard
		test8();
		break;
	case 9:
		// Rollback test
		test9();
		break;
	case 10:
		// Insert a second range that has to check a guard in the end
		// path. Guard matches
		test10();
		break;
	case 11:
		test11();
		break;
	case 12:
		test12();
		break;
	case 13:
		// Insert a second range that has to check a guard in the end
		// path. Guard partially matches
		test13();
		break;
	}
}
