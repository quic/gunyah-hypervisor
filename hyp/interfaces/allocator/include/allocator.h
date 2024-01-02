// © 2021 Qualcomm Innovation Center, Inc. All rights reserved.
//
// SPDX-License-Identifier: BSD-3-Clause

// Memory allocator API functions

// Initialise an allocator. Called during partition creation.
//
// This must not allocate any memory; if the allocator needs to reserve memory
// for itself beyond what is in allocator_t, then that should be deferred until
// the first call to allocator_heap_add_memory().
error_t
allocator_init(allocator_t *allocator);

void_ptr_result_t
allocator_allocate_object(allocator_t *allocator, size_t size,
			  size_t min_alignment);

error_t
allocator_deallocate_object(allocator_t *allocator, void *object, size_t size);

error_t
allocator_heap_remove_memory(allocator_t *allocator, void *obj, size_t size);
