// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>

#include "allocator.h"
#include "freelist.h"

#define MEM_POOL_SIZE (1024 * 1024) // 1MB

// Maximum supported heap allocation size or alignment size. We filter out
// really large allocations so we can avoid having to think about corner-cases
// causing overflow.
#define MAX_ALLOC_SIZE	   (256UL * 1024UL * 1024UL)
#define MAX_ALIGNMENT_SIZE (16UL * 1024UL * 1024UL)

#define NODE_HEADER_SIZE ((size_t)sizeof(allocator_node_t))

// Minimum allocation size from the heap.
#define HEAP_MIN_ALLOC NODE_HEADER_SIZE
#define HEAP_MIN_ALIGN NODE_HEADER_SIZE

// -------------- DEBUGGING --------------
// FIXME: remove eventually
#define HEAP_DEBUG
#define OVERFLOW_DEBUG
// #define DEBUG_PRINT
//  ---------------------------------------

#if defined(HEAP_DEBUG)
#define CHECK_HEAP(x) check_heap_consistency(x)
#else
#define CHECK_HEAP(x)
#endif

void
print_free_blocks(struct node *head)
{
	struct node *current = head;
	int	     count   = 0;

	printf("\n----------- FREE BLOCKS ----------\n");

	while (current != NULL) {
		printf("%d pointer: %p, size %zu\n", count, current,
		       current->size);
		current = current->next;
		count++;
	}

	printf("----------------------------------\n\n");
}

void *
give_mem_to_heap(allocator_t *allocator, size_t size, size_t alignment)
{
	int   ret   = 0;
	void *block = malloc(size);

	printf("Give memory to heap from block, pointer: %p, size %zu\n", block,
	       size);
	ret = allocator_heap_add_memory(allocator, block, size);
	if (!ret) {
		printf("Memory added to heap, pointer: %p, size %zu\n",
		       *&allocator->heap, allocator->heap->size);
	}

	print_free_blocks(allocator->heap);

	return block;
}

void *
alloc_obj(allocator_t *allocator, size_t size, size_t alignment)
{
	void *object = allocator_allocate_object(allocator, size, alignment);

	print_free_blocks(allocator->heap);

	return object;
}

void
dealloc_objs(allocator_t *allocator, int order, void *object, size_t size,
	     void *object2, size_t size2, void *object3, size_t size3)
{
	switch (order) {
	case 0:
		// 1st -> 2nd -> 3rd
		if (object != NULL) {
			allocator_deallocate_object(allocator, object, size);
			print_free_blocks(allocator->heap);
		}

		if (object2 != NULL) {
			allocator_deallocate_object(allocator, object2, size2);
			print_free_blocks(allocator->heap);
		}

		if (object3 != NULL) {
			allocator_deallocate_object(allocator, object3, size3);
			print_free_blocks(allocator->heap);
		}
		break;

	case 1:
		// 3rd -> 2nd -> 1st
		if (object3 != NULL) {
			allocator_deallocate_object(allocator, object3, size3);
			print_free_blocks(allocator->heap);
		}

		if (object2 != NULL) {
			allocator_deallocate_object(allocator, object2, size2);
			print_free_blocks(allocator->heap);
		}

		if (object != NULL) {
			allocator_deallocate_object(allocator, object, size);
			print_free_blocks(allocator->heap);
		}
		break;

	case 2:
	default:
		// 1st -> 3rd -> 2nd
		if (object != NULL) {
			printf("Free: %p, size: %zu\n", object, size);
			allocator_deallocate_object(allocator, object, size);
			print_free_blocks(allocator->heap);
		}

		if (object3 != NULL) {
			printf("Free: %p, size: %zu\n", object3, size3);
			allocator_deallocate_object(allocator, object3, size3);
			print_free_blocks(allocator->heap);
		}

		if (object2 != NULL) {
			printf("Free: %p, size: %zu\n", object2, size2);
			allocator_deallocate_object(allocator, object2, size2);
			print_free_blocks(allocator->heap);
		}
		break;
	}
}

void
remove_from_heap(allocator_t *allocator, void *block, size_t size)
{
	int ret;

	if (block != NULL) {
		ret = allocator_heap_remove_memory(allocator, block, size);
	}

	if (!ret) {
		printf("Memory removed from heap. size: %zu\n", size);
	}
	print_free_blocks(allocator->heap);
}

// Test 1:
// - Give 1 chunk of memory to the heap of pool_size passed
// - Allocate objects of passed sizes
// - Free all the objects in order specied in 'order' variable.
// - Remove pool from heap
static void
test1(int order, size_t alignment, size_t pool_size, size_t size, size_t size2,
      size_t size3)
{
	allocator_t allocator;

	allocator.heap = NULL;

	// ---------------- Giving memory to heap ---------------------
	void *block = give_mem_to_heap(&allocator, pool_size, alignment);

	// ---------------- Allocating object from heap ---------------
	void *object  = alloc_obj(&allocator, size, alignment);
	void *object2 = alloc_obj(&allocator, size2, alignment);
	void *object3 = alloc_obj(&allocator, size3, alignment);

	// ---------------- Deallocating object to heap ----------------
	dealloc_objs(&allocator, order, object, size, object2, size2, object3,
		     size3);

	// ---------------- Removing memory to heap --------------------
	remove_from_heap(&allocator, block, pool_size);
}

// Test 2:
// - Give 3 chunks of memory to the heap of pool_size passed
// - Allocate objects of passed sizes
// - Free all the objects in order specied in 'order' variable.
// - Remove all pools from heap
static void
test2(int order, size_t alignment, size_t pool_size, size_t pool_size2,
      size_t pool_size3, size_t size, size_t size2, size_t size3)
{
	allocator_t allocator;

	allocator.heap = NULL;

	// ---------------- Giving memory to heap ---------------------
	void *block  = give_mem_to_heap(&allocator, pool_size, alignment);
	void *block2 = give_mem_to_heap(&allocator, pool_size2, alignment);
	void *block3 = give_mem_to_heap(&allocator, pool_size3, alignment);

	// ---------------- Allocating object from heap ---------------
	void *object  = alloc_obj(&allocator, size, alignment);
	void *object2 = alloc_obj(&allocator, size2, alignment);
	void *object3 = alloc_obj(&allocator, size3, alignment);

	// ---------------- Deallocating object to heap ----------------
	dealloc_objs(&allocator, order, object, size, object2, size2, object3,
		     size3);

	// ---------------- Removing memory to heap --------------------
	remove_from_heap(&allocator, block, pool_size);
	remove_from_heap(&allocator, block2, pool_size2);
	remove_from_heap(&allocator, block3, pool_size3);
}

void
values_selection(int order, size_t alignment, size_t pool_size,
		 size_t pool_size2, size_t pool_size3, size_t size,
		 size_t size2, size_t size3, int test)
{
}

void
tests_choice(int test)
{
	int    order	   = 2;
	size_t alignment   = sizeof(void *);
	size_t pool_size   = MEM_POOL_SIZE;
	size_t pool_size2  = 2UL * NODE_HEADER_SIZE;
	size_t pool_size3  = 4UL * NODE_HEADER_SIZE;
	size_t size	   = MEM_POOL_SIZE / 2;
	size_t size2	   = 48;
	size_t size3	   = MEM_POOL_SIZE / 2 - 48;
	int    default_val = 1;

	printf("Do you want to you the fault test? Yes(1), No(0): ");
	scanf("%d", &default_val);

	if (test == 1) {
		// Default:
		// Allocate 3 objects emptying the free list
		// Deallocate in this order: 1st -> 3rd -> 2nd
		// so that we can check that when the 2nd object is
		// freed there is a merge of all free blocks
		printf("--------- Test 1 ---------\n");
		if (!default_val) {
			printf("Order 0) 1->2->3 | 1) 3->2->1 | 2) 1->3->2: ");
			scanf("%d", &order);
			printf("Alignment: ");
			scanf("%zu", &alignment);
			printf("Pool size: ");
			scanf("%zu", &pool_size);
			printf("Size: ");
			scanf("%zu", &size);
			printf("Size2: ");
			scanf("%zu", &size2);
			printf("Size3: ");
			scanf("%zu", &size3);
		}

#if defined(OVERFLOW_DEBUG)
		// Extra 2*NODE_HEADER_SIZE for overflow checks
		pool_size = pool_size + (6 * NODE_HEADER_SIZE);
#endif
		test1(order, alignment, pool_size, size, size2, size3);

	} else {
		// Default:
		// - Allocate an object that consumes 1st pool
		// - Allocate an object that does not fit in next pool
		//   but has go the 3rd one
		// - Allocate a smaller object from 2nd pool
		//   (now first) and needs alignment
		order	  = 1;
		pool_size = MEM_POOL_SIZE;
		size	  = MEM_POOL_SIZE;
		size2	  = 36;
		size3	  = 10;

		printf("--------- Test 2 ---------\n");
		if (!default_val) {
			printf("Order 0) 1->2->3 | 1) 3->2->1 | 2) 1->3->2: ");
			scanf("%d", &order);
			printf("Alignment: ");
			scanf("%zu", &alignment);
			printf("Pool size: ");
			scanf("%zu", &pool_size);
			printf("Pool size2: ");
			scanf("%zu", &pool_size2);
			printf("Pool size3: ");
			scanf("%zu", &pool_size3);
			printf("Size: ");
			scanf("%zu", &size);
			printf("Size2: ");
			scanf("%zu", &size2);
			printf("Size3: ");
			scanf("%zu", &size3);
		}

#if defined(OVERFLOW_DEBUG)
		// Extra 2*NODE_HEADER_SIZE for overflow checks
		pool_size  = pool_size + (2 * NODE_HEADER_SIZE);
		pool_size2 = pool_size2 + (2 * NODE_HEADER_SIZE);
		pool_size3 = pool_size3 + (2 * NODE_HEADER_SIZE);
#endif
		test2(order, alignment, pool_size, pool_size2, pool_size3, size,
		      size2, size3);
	}
}

int
main()
{
	int option = 0;

	while (option != 3) {
		printf("Test 1: ");
		printf("- Allocates 3 objects from one pool\n");
		printf("        - Deallocates in specified order)\n");

		printf("Test 2: ");
		printf("- Allocates 3 objects from three pools\n");
		printf("        - Deallocates in specified order:\n");

		printf("Choose which test to execute 1 or 2 or exit (3): ");

		scanf("%d", &option);

		if (option != 3) {
			tests_choice(option);
		}
	}

	return 0;
}
