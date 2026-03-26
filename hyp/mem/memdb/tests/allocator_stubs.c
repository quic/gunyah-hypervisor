// Copyright © Qualcomm Technologies, Inc. and/or its subsidiaries.
//
// SPDX-License-Identifier: BSD-3-Clause

// Stub implementations for allocator functions for host testing

#define timer_t hyp_timer_t
#include <hyptypes.h>
#undef timer_t

#define register_t std_register_t
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef register_t

#include <allocator.h>

// Simple heap allocator implementation for testing
#define MAX_HEAP_BLOCKS 100

typedef struct heap_block {
	void  *base;
	size_t size;
	bool   allocated;
} heap_block_t;

typedef struct heap_info {
	void	    *heap_base;
	size_t	     heap_size;
	heap_block_t blocks[MAX_HEAP_BLOCKS];
	size_t	     block_count;
} heap_info_t;

// Add memory to the heap allocator
error_t
allocator_heap_add_memory(allocator_t *allocator, void *base, size_t size)
{
	if (allocator == NULL || base == NULL || size == 0) {
		return ERROR_ARGUMENT_INVALID;
	}

	// For stub purposes, we'll store heap info in the allocator's hyp field
	// Initialize heap info if not already done
	if (allocator->hyp.heap == NULL) {
		heap_info_t *heap_info = malloc(sizeof(heap_info_t));
		if (heap_info == NULL) {
			return ERROR_NOMEM;
		}
		memset(heap_info, 0, sizeof(heap_info_t));
		allocator->hyp.heap = (allocator_node_t *)heap_info;
	}

	heap_info_t *heap = (heap_info_t *)allocator->hyp.heap;

	// Store the heap base and size
	heap->heap_base = base;
	heap->heap_size = size;

	// Initialize with one large free block
	heap->blocks[0].base	  = base;
	heap->blocks[0].size	  = size;
	heap->blocks[0].allocated = false;
	heap->block_count	  = 1;

	return OK;
}

// Allocate an object from the heap
void_ptr_result_t
allocator_allocate_object(allocator_t *allocator, size_t size, size_t alignment,
			  allocator_hint_t hint)
{
	(void)hint; // Unused in stub
	void_ptr_result_t result = { .e = ERROR_NOMEM, .r = NULL };

	if (allocator == NULL || size == 0) {
		result.e = ERROR_ARGUMENT_INVALID;
		return result;
	}

	// For stub purposes, we'll store heap info in the allocator's hyp field
	heap_info_t *heap = (heap_info_t *)allocator->hyp.heap;
	if (heap == NULL) {
		result.e = ERROR_OBJECT_STATE;
		return result;
	}

	// Find a suitable free block
	for (size_t i = 0; i < heap->block_count; i++) {
		if (heap->blocks[i].allocated) {
			continue;
		}

		// Check if this block is large enough
		uintptr_t base_addr    = (uintptr_t)heap->blocks[i].base;
		uintptr_t aligned_addr = (base_addr + alignment - 1) &
					 ~(alignment - 1);
		size_t	  offset       = aligned_addr - base_addr;

		if (heap->blocks[i].size >= size + offset) {
			// Allocate from this block
			heap->blocks[i].allocated = true;

			// If there's remaining space, create a new free block
			if (heap->blocks[i].size > size + offset &&
			    heap->block_count < MAX_HEAP_BLOCKS) {
				heap->blocks[heap->block_count].base =
					(void *)(aligned_addr + size);
				heap->blocks[heap->block_count].size =
					heap->blocks[i].size - size - offset;
				heap->blocks[heap->block_count].allocated =
					false;
				heap->block_count++;
			}

			// Update the allocated block
			heap->blocks[i].base = (void *)aligned_addr;
			heap->blocks[i].size = size;

			result.e = OK;
			result.r = (void *)aligned_addr;
			return result;
		}
	}

	// No suitable block found
	result.e = ERROR_NOMEM;
	return result;
}

// Deallocate an object (stub - not implemented for simplicity)
void
allocator_deallocate_object(allocator_t *allocator, void *object, size_t size,
			    allocator_memattr_t attr)
{
	(void)allocator;
	(void)object;
	(void)size;
	(void)attr;

	// For testing purposes, we don't implement deallocation
}
